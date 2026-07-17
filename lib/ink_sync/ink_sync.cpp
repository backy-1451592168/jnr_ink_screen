#include "ink_sync.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "epd.h"
#include "frame_store.h"
#include "wifi_setup.h"

#ifndef EPD_MIN_REFRESH_INTERVAL
#define EPD_MIN_REFRESH_INTERVAL 60
#endif
#ifndef EPD_DEBOUNCE_MS
#define EPD_DEBOUNCE_MS 3000
#endif

namespace ink_sync {

namespace {

volatile bool g_cancel = false;
bool g_busy = false;
ActivityHook g_activityHook = nullptr;

void tickActivity() {
  if (g_activityHook) g_activityHook();
}

// 与 Node zlib.crc32 一致：初值/终值均异或 0xFFFFFFFF
uint32_t crc32Buf(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

uint32_t parseCrcHex(const String& hex) {
  uint32_t v = 0;
  for (size_t i = 0; i < hex.length(); i++) {
    char c = hex[i];
    uint8_t n = 0;
    if (c >= '0' && c <= '9') n = c - '0';
    else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') n = c - 'A' + 10;
    else continue;
    v = (v << 4) | n;
  }
  return v;
}

String jsonStr(const String& body, const char* key) {
  String pat = String("\"") + key + "\":\"";
  int i = body.indexOf(pat);
  if (i < 0) return "";
  i += pat.length();
  int j = body.indexOf('"', i);
  if (j < 0) return "";
  return body.substring(i, j);
}

int64_t jsonNum(const String& body, const char* key) {
  String pat = String("\"") + key + "\":";
  int i = body.indexOf(pat);
  if (i < 0) return -1;
  i += pat.length();
  while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
  return strtoll(body.c_str() + i, nullptr, 10);
}

bool jsonBoolTrue(const String& body, const char* key) {
  String pat = String("\"") + key + "\":";
  int i = body.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  while (i < (int)body.length() && body[i] == ' ') i++;
  return body.substring(i, i + 4) == "true";
}

String macAddress() {
  return WiFi.macAddress();  // AA:BB:CC:DD:EE:FF
}

void prepareClient(WiFiClientSecure& client) {
  client.setInsecure();
  client.setTimeout(20);
}

void applyDeviceSecretHeader(HTTPClient& http) {
  String secret = frame_store::deviceSecret();
  if (!secret.isEmpty()) {
    http.addHeader("X-Ink-Device-Secret", secret);
  }
}

bool httpGet(const String& url, String& outBody, int& outCode) {
  outBody = "";
  outCode = -1;
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    prepareClient(client);
    HTTPClient http;
    http.setTimeout(20000);
    if (!http.begin(client, url)) return false;
    applyDeviceSecretHeader(http);
    outCode = http.GET();
    if (outCode > 0) outBody = http.getString();
    http.end();
    return outCode > 0;
  }
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(url)) return false;
  applyDeviceSecretHeader(http);
  outCode = http.GET();
  if (outCode > 0) outBody = http.getString();
  http.end();
  return outCode > 0;
}

bool readHttpBinaryBody(HTTPClient& http, uint8_t* dest, size_t maxLen, size_t& got) {
  got = 0;
  applyDeviceSecretHeader(http);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  int len = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  while (http.connected() && (got < maxLen) && (len < 0 || (int)got < len)) {
    if (g_cancel) {
      http.end();
      return false;
    }
    tickActivity();
    size_t avail = stream->available();
    if (!avail) {
      delay(1);
      continue;
    }
    size_t n = stream->readBytes(dest + got, min(avail, maxLen - got));
    got += n;
  }
  http.end();
  return got > 0;
}

/** GET 二进制到 dest；下载过程可取消并 tick 紫灯 */
bool httpGetBinary(const String& url, uint8_t* dest, size_t maxLen, size_t& got) {
  // 整帧 ~192KB，隧道上留足时间
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    prepareClient(client);
    HTTPClient http;
    http.setTimeout(60000);
    if (!http.begin(client, url)) return false;
    return readHttpBinaryBody(http, dest, maxLen, got);
  }
  HTTPClient http;
  http.setTimeout(60000);
  if (!http.begin(url)) return false;
  return readHttpBinaryBody(http, dest, maxLen, got);
}

bool httpPostJson(const String& url, const String& json) {
  if (url.startsWith("https://")) {
    WiFiClientSecure client;
    prepareClient(client);
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    applyDeviceSecretHeader(http);
    int code = http.POST(json);
    http.end();
    return code == 200;
  }
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");
  applyDeviceSecretHeader(http);
  int code = http.POST(json);
  http.end();
  return code == 200;
}

bool canRefreshNow() {
  uint32_t last = frame_store::lastRefreshTs();
  if (last == 0) return true;
  uint32_t now = (uint32_t)time(nullptr);
  if (now > 1700000000 && last > 1700000000) {
    return (now - last) >= (uint32_t)EPD_MIN_REFRESH_INTERVAL;
  }
  // NTP 未就绪：用开机秒数粗略判断（lastRefreshTs 存的是 millis/1000）
  uint32_t sec = millis() / 1000;
  return (sec - last) >= (uint32_t)EPD_MIN_REFRESH_INTERVAL;
}

void markRefreshed() {
  uint32_t now = (uint32_t)time(nullptr);
  if (now < 1700000000) now = millis() / 1000;
  frame_store::setLastRefreshTs(now);
}

// 远程 sync 下发的新帧：拉取模型每次只有一帧，无需内容防抖窗口（避免下完空等再刷）
bool applyFrame(const uint8_t* buf, size_t len, uint32_t crc, uint32_t version, bool isBound) {
  if (!epd::drawImageRaw(buf, len)) return false;
  tickActivity();
  if (!epd::flush()) return false;
  frame_store::saveLastFrame(buf, len, crc);
  frame_store::setContentVersion(version);
  frame_store::setBound(isBound);
  markRefreshed();
  return true;
}

bool postAck(const String& base, const String& mac, uint32_t version, bool success) {
  String url = base + "/device/ink_frame/ack";
  String body = String("{\"mac\":\"") + mac + "\",\"contentVersion\":" + String(version) +
                ",\"success\":" + (success ? "true" : "false") + "}";
  return httpPostJson(url, body);
}

Result syncAgainstBase(const String& base) {
  if (base.isEmpty()) return Result::Failed;
  String mac = macAddress();
  uint32_t localVer = frame_store::contentVersion();
  String url = base + "/device/ink_frame/sync?mac=" + mac +
               "&content_version=" + String(localVer);
  // 上报局域网 IP，供小程序展示 /upload、/device
  if (WiFi.status() == WL_CONNECTED) {
    url += "&ip=" + WiFi.localIP().toString();
  }

  String body;
  int code = 0;
  Serial.printf("[ink_sync] GET %s\n", url.c_str());
  if (!httpGet(url, body, code)) {
    Serial.printf("[ink_sync] sync HTTP 失败 code=%d\n", code);
    return Result::Failed;
  }
  // 凭证无效：清本地 secret（解绑后下次可 bootstrap；仍绑定时需先小程序解绑）
  if (code == 401 || body.indexOf("\"state\":401") >= 0 ||
      body.indexOf("\"state\": 401") >= 0) {
    Serial.println("[ink_sync] 设备凭证无效，清除本地 secret");
    frame_store::clearDeviceSecret();
    return Result::Failed;
  }
  if (code != 200) {
    Serial.printf("[ink_sync] sync HTTP 失败 code=%d\n", code);
    return Result::Failed;
  }

  // 响应在 data 对象内
  String type = jsonStr(body, "type");
  if (type.isEmpty()) {
    // 兼容嵌套：找 "type":
    int t = body.indexOf("\"type\":\"");
    if (t >= 0) {
      t += 8;
      int e = body.indexOf('"', t);
      type = body.substring(t, e);
    }
  }

  // 首次 / 解绑后 bootstrap：服务端下发 deviceSecret，写入 NVS
  String issued = jsonStr(body, "deviceSecret");
  if (!issued.isEmpty()) {
    frame_store::setDeviceSecret(issued);
    Serial.println("[ink_sync] 已保存 deviceSecret");
  }

  if (type == "sync_ok") {
    bool bound = jsonBoolTrue(body, "bound") || body.indexOf("\"bound\":true") >= 0;
    int64_t cv = jsonNum(body, "contentVersion");
    int64_t sw = jsonNum(body, "screenWidth");
    int64_t sh = jsonNum(body, "screenHeight");
    if (sw > 0 && sh > 0) {
      frame_store::setScreenSize((int)sw, (int)sh);
    }
    // 云端已绑但尚无纪念日新帧（本地仍是绑定页 version=1）：不置 bound，继续 60s 轮询
    if (bound && frame_store::contentVersion() <= 1) {
      Serial.println("[ink_sync] sync_ok 已绑定但无纪念日画面，继续等待");
      return Result::OkNoUpdate;
    }
    frame_store::setBound(bound);
    if (cv > 0) frame_store::setContentVersion((uint32_t)cv);
    Serial.println("[ink_sync] sync_ok");
    return Result::OkNoUpdate;
  }

  if (type != "has_update" && type != "bind_qr") {
    Serial.printf("[ink_sync] 未知 type=%s\n", type.c_str());
    return Result::Failed;
  }

  int64_t frameSize = jsonNum(body, "frameSize");
  int64_t contentVersion = jsonNum(body, "contentVersion");
  String frameCrcHex = jsonStr(body, "frameCrc");
  String downloadUrl = jsonStr(body, "downloadUrl");

  // 服务端下发屏参时先切换（横/竖行跨度不同，须在 drawImageRaw 前生效）
  int64_t sw = jsonNum(body, "screenWidth");
  int64_t sh = jsonNum(body, "screenHeight");
  if (sw > 0 && sh > 0) {
    if (!frame_store::setScreenSize((int)sw, (int)sh)) {
      Serial.printf("[ink_sync] 屏参非法 %lldx%lld\n", (long long)sw, (long long)sh);
      return Result::Failed;
    }
  }

  // 未绑定轮询：屏上已是同一绑定页则只确认状态，不下载不刷屏
  if (type == "bind_qr") {
    uint32_t expectCrc = parseCrcHex(frameCrcHex);
    if (!frame_store::bound() && frame_store::hasValidLastFrame() &&
        expectCrc != 0 && frame_store::frameCrc() == expectCrc) {
      Serial.println("[ink_sync] bind_qr 已显示，跳过刷屏");
      return Result::OkNoUpdate;
    }
  }

  if (frameSize <= 0 || downloadUrl.isEmpty()) {
    Serial.println("[ink_sync] 帧元信息缺失");
    return Result::Failed;
  }
  if ((uint32_t)frameSize != epd::frameBytes()) {
    Serial.printf("[ink_sync] frameSize 不匹配 expect=%u got=%lld\n",
                  epd::frameBytes(), (long long)frameSize);
    return Result::Failed;
  }

  tickActivity();  // 开始下载：紫灯呼吸

  uint8_t* buf = (uint8_t*)ps_malloc((size_t)frameSize);
  if (!buf) buf = (uint8_t*)malloc((size_t)frameSize);
  if (!buf) {
    Serial.println("[ink_sync] 分配帧缓冲失败");
    return Result::Failed;
  }

  Serial.printf("[ink_sync] 整帧下载 size=%lld\n", (long long)frameSize);
  size_t got = 0;
  if (!httpGetBinary(downloadUrl, buf, (size_t)frameSize, got) || got != (size_t)frameSize) {
    Serial.printf("[ink_sync] 整帧下载失败 got=%u expect=%lld\n",
                  (unsigned)got, (long long)frameSize);
    free(buf);
    return g_cancel ? Result::Cancelled : Result::Failed;
  }

  uint32_t crc = crc32Buf(buf, (size_t)frameSize);
  uint32_t expect = parseCrcHex(frameCrcHex);
  if (crc != expect) {
    Serial.printf("[ink_sync] CRC 失败 expect=%08x got=%08x\n", expect, crc);
    free(buf);
    return Result::Failed;
  }

  bool isBound = (type == "has_update");
  uint32_t ver = (type == "bind_qr") ? 1 : (uint32_t)contentVersion;
  bool ok = applyFrame(buf, (size_t)frameSize, crc, ver, isBound);
  free(buf);
  if (!ok) {
    if (g_cancel) return Result::Cancelled;
    return Result::Failed;
  }

  postAck(base, mac, ver, true);
  Serial.printf("[ink_sync] 刷屏成功 version=%u type=%s\n", ver, type.c_str());
  return Result::OkUpdated;
}

}  // namespace

void requestCancel() { g_cancel = true; }
void clearCancel() { g_cancel = false; }
bool isBusy() { return g_busy; }

void setActivityHook(ActivityHook fn) { g_activityHook = fn; }

Result runOnce() {
  if (g_busy) return Result::Failed;
  g_busy = true;
  clearCancel();

  Result r = Result::Failed;
  String primary = wifi_setup::apiBase();
  r = syncAgainstBase(primary);
  if (r == Result::Failed) {
    String bak = wifi_setup::apiBaseBackup();
    if (!bak.isEmpty() && bak != primary) {
      Serial.println("[ink_sync] 主地址失败，试备用");
      r = syncAgainstBase(bak);
    }
  }

  g_busy = false;
  return r;
}

Result refreshLocal(bool force) {
  if (g_busy) return Result::Failed;
  if (!frame_store::hasValidLastFrame()) return Result::Failed;
  if (!force && !canRefreshNow()) return Result::Failed;

  g_busy = true;
  clearCancel();
  size_t len = epd::frameBytes();
  uint8_t* buf = (uint8_t*)ps_malloc(len);
  if (!buf) buf = (uint8_t*)malloc(len);
  if (!buf) {
    g_busy = false;
    return Result::Failed;
  }
  uint32_t crc = 0;
  if (!frame_store::loadLastFrame(buf, len, &crc)) {
    free(buf);
    g_busy = false;
    return Result::Failed;
  }
  if (EPD_DEBOUNCE_MS > 0) delay(50);  // 本地重刷不做长防抖
  tickActivity();
  bool ok = epd::drawImageRaw(buf, len) && epd::flush();
  free(buf);
  if (ok) markRefreshed();
  g_busy = false;
  return ok ? Result::OkUpdated : Result::Failed;
}

}  // namespace ink_sync
