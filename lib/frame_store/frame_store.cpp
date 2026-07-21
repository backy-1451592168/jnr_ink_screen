#include "frame_store.h"

#include <LittleFS.h>
#include <Preferences.h>

#include "epd.h"

namespace frame_store {

namespace {

constexpr const char* kNvs = "jnr";
constexpr const char* kKeyMode = "workMode";
constexpr const char* kKeyVer = "contentVersion";
constexpr const char* kKeyCrc = "frameCrc";
constexpr const char* kKeyBound = "bound";
constexpr const char* kKeySecret = "deviceSecret";
constexpr const char* kKeyRefresh = "lastRefreshTs";
constexpr const char* kKeySsid = "wifiSsid";
constexpr const char* kKeyPass = "wifiPass";
constexpr const char* kKeyScreenW = "screenW";
constexpr const char* kKeyScreenH = "screenH";
constexpr const char* kFramePath = "/last.bin";

bool isAllowedSize(int w, int h) {
  return (w == epd::kPortraitW && h == epd::kPortraitH) ||
         (w == epd::kLandscapeW && h == epd::kLandscapeH);
}

// 每次读写开关 NVS，避免与 wifi_setup 长期占用的 Preferences 冲突
class Nvs {
 public:
  explicit Nvs(bool ro) { ok_ = p_.begin(kNvs, ro); }
  ~Nvs() {
    if (ok_) p_.end();
  }
  Preferences* operator->() { return &p_; }
  bool ok() const { return ok_; }

 private:
  Preferences p_;
  bool ok_;
};

// 与 Node zlib.crc32 / ink_sync 一致
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

}  // namespace

bool begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[frame_store] LittleFS 挂载失败");
    return false;
  }
  // 首次上电创建 NVS 命名空间，避免只读 open 刷 Preferences NOT_FOUND
  {
    Preferences p;
    if (p.begin(kNvs, false)) p.end();
  }
  return true;
}

WorkMode workMode() {
  Nvs n(true);
  if (!n.ok()) return MODE_MINIPROG;
  return static_cast<WorkMode>(n->getUChar(kKeyMode, MODE_MINIPROG));
}

void setWorkMode(WorkMode m) {
  Nvs n(false);
  if (n.ok()) n->putUChar(kKeyMode, static_cast<uint8_t>(m));
}

uint32_t contentVersion() {
  Nvs n(true);
  return n.ok() ? n->getUInt(kKeyVer, 0) : 0;
}

void setContentVersion(uint32_t v) {
  Nvs n(false);
  if (n.ok()) n->putUInt(kKeyVer, v);
}

uint32_t frameCrc() {
  Nvs n(true);
  return n.ok() ? n->getUInt(kKeyCrc, 0) : 0;
}

void setFrameCrc(uint32_t crc) {
  Nvs n(false);
  if (n.ok()) n->putUInt(kKeyCrc, crc);
}

bool bound() {
  Nvs n(true);
  return n.ok() && n->getUChar(kKeyBound, 0) != 0;
}

void setBound(bool v) {
  Nvs n(false);
  if (n.ok()) n->putUChar(kKeyBound, v ? 1 : 0);
}

String deviceSecret() {
  Nvs n(true);
  if (!n.ok()) return "";
  return n->getString(kKeySecret, "");
}

void setDeviceSecret(const String& secret) {
  Nvs n(false);
  if (!n.ok()) return;
  if (secret.isEmpty()) n->remove(kKeySecret);
  else n->putString(kKeySecret, secret);
}

void clearDeviceSecret() {
  setDeviceSecret("");
}

uint32_t lastRefreshTs() {
  Nvs n(true);
  return n.ok() ? n->getUInt(kKeyRefresh, 0) : 0;
}

void setLastRefreshTs(uint32_t ts) {
  Nvs n(false);
  if (n.ok()) n->putUInt(kKeyRefresh, ts);
}

int screenWidth() {
  Nvs n(true);
  if (!n.ok()) return epd::kPortraitW;
  return (int)n->getUShort(kKeyScreenW, epd::kPortraitW);
}

int screenHeight() {
  Nvs n(true);
  if (!n.ok()) return epd::kPortraitH;
  return (int)n->getUShort(kKeyScreenH, epd::kPortraitH);
}

bool setScreenSize(int w, int h) {
  if (!isAllowedSize(w, h)) return false;
  const int oldW = screenWidth();
  const int oldH = screenHeight();
  {
    Nvs n(false);
    if (!n.ok()) return false;
    n->putUShort(kKeyScreenW, (uint16_t)w);
    n->putUShort(kKeyScreenH, (uint16_t)h);
  }
  if (!epd::setLogicalSize(w, h)) return false;
  // 方向变了：旧 /last.bin 行跨度不同，本地重刷会花屏
  if (oldW != w || oldH != h) {
    clearLastFrame();
    Serial.printf("[frame_store] 屏参 %dx%d → %dx%d，已清本地帧缓存\n", oldW, oldH, w, h);
  }
  return true;
}

void applyStoredScreenSize() {
  int w = screenWidth();
  int h = screenHeight();
  if (!isAllowedSize(w, h)) {
    w = epd::kPortraitW;
    h = epd::kPortraitH;
  }
  epd::setLogicalSize(w, h);
}

bool saveLastFrame(const uint8_t* data, size_t len, uint32_t crc) {
  if (!data || len != epd::frameBytes()) return false;
  File f = LittleFS.open(kFramePath, "w");
  if (!f) {
    Serial.println("[frame_store] 写 /last.bin 失败");
    return false;
  }
  size_t written = f.write(data, len);
  f.close();
  if (written != len) {
    LittleFS.remove(kFramePath);
    return false;
  }
  setFrameCrc(crc);
  return true;
}

bool loadLastFrame(uint8_t* out, size_t len, uint32_t* outCrc) {
  if (!out || len != epd::frameBytes()) return false;
  if (!LittleFS.exists(kFramePath)) return false;
  File f = LittleFS.open(kFramePath, "r");
  if (!f || f.size() != (int)len) {
    if (f) f.close();
    return false;
  }
  size_t n = f.read(out, len);
  f.close();
  if (n != len) return false;
  uint32_t crc = crc32Buf(out, len);
  if (crc != frameCrc()) {
    Serial.printf("[frame_store] CRC 不匹配 expect=%08x got=%08x\n", frameCrc(), crc);
    return false;
  }
  if (outCrc) *outCrc = crc;
  return true;
}

bool hasValidLastFrame() {
  if (!LittleFS.exists(kFramePath)) return false;
  File f = LittleFS.open(kFramePath, "r");
  if (!f) return false;
  bool ok = f.size() == (int)epd::frameBytes();
  f.close();
  return ok && frameCrc() != 0;
}

void clearLastFrame() {
  LittleFS.remove(kFramePath);
  setFrameCrc(0);
}

void clearWifiCreds() {
  Nvs n(false);
  if (!n.ok()) return;
  n->remove(kKeySsid);
  n->remove(kKeyPass);
}

void factoryReset() {
  clearWifiCreds();
  setWorkMode(MODE_MINIPROG);
  setContentVersion(0);
  setBound(false);
  clearDeviceSecret();
  setLastRefreshTs(0);
  clearLastFrame();
  {
    Nvs n(false);
    if (n.ok()) {
      n->remove(kKeyScreenW);
      n->remove(kKeyScreenH);
    }
  }
  epd::setLogicalSize(epd::kPortraitW, epd::kPortraitH);
}

}  // namespace frame_store
