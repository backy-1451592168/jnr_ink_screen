// 水墨屏纪念日相框 —— 配网 + 小程序推送拉图
//
//   1. 上电读 NVS WiFi；连上 → 就绪屏 + 本地 /device + NTP + 按键 + sync 调度
//   2. 无凭证 / 连接失败 → AP 配网
//   3. 小程序推送模式：syncHour / 未绑定 60s / 执行键长按 → HTTP 拉图刷屏
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "buttons.h"
#include "epd.h"
#include "frame_store.h"
#include "ink_sync.h"
#include "wifi_setup.h"

#ifndef PIN_RGB_LED
#define PIN_RGB_LED 21
#endif

static Adafruit_NeoPixel rgb(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

static bool g_provisioning = false;
static bool g_localAdmin = false;
static bool g_ntpOk = false;
static uint32_t g_lastUnboundPollMs = 0;
static int g_lastSyncDay = -1;  // 已绑定：当日是否已按 syncHour 同步

static void led(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

static void ledModeIdle() {
  if (frame_store::workMode() == frame_store::MODE_LAN) {
    led(0, 50, 50);  // 青
  } else {
    led(0, 60, 0);  // 绿
  }
}

static void ledFail() {
  for (int i = 0; i < 3; i++) {
    led(60, 0, 0);
    delay(150);
    led(0, 0, 0);
    delay(150);
  }
  ledModeIdle();
}

static void ledFlashModeFast(bool on) {
  if (frame_store::workMode() == frame_store::MODE_LAN) {
    if (on) led(0, 50, 50);
    else led(0, 0, 0);
  } else {
    if (on) led(0, 60, 0);
    else led(0, 0, 0);
  }
}

// 下载/刷屏期间紫色呼吸（约 1.5s 周期）
static void ledPurpleBreatheTick() {
  const uint32_t period = 1500;
  uint32_t phase = millis() % period;
  uint8_t bri;
  if (phase < period / 2) {
    bri = (uint8_t)(8 + (phase * 72) / (period / 2));
  } else {
    bri = (uint8_t)(80 - ((phase - period / 2) * 72) / (period / 2));
  }
  led(bri, 0, bri);
}

static void beginRenderLed() {
  ink_sync::setActivityHook(ledPurpleBreatheTick);
  epd::setBusyPollHook(ledPurpleBreatheTick);
}

static void endRenderLed() {
  ink_sync::setActivityHook(nullptr);
  epd::setBusyPollHook(nullptr);
}

static bool tryConnectSaved(uint32_t timeoutMs) {
  Preferences prefs;
  prefs.begin("jnr", true);
  String ssid = prefs.getString("wifiSsid", "");
  String pass = prefs.getString("wifiPass", "");
  prefs.end();
  if (ssid.isEmpty()) {
    Serial.println("[main] NVS 无 WiFi 凭证");
    return false;
  }

  Serial.printf("[main] 尝试连接已存 WiFi: %s\n", ssid.c_str());
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  wifi_setup::applySavedStaticIp();
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_setup::saveCurrentStaIp();
      Serial.printf("[main] 已连接，IP=%s apiBase=%s\n",
                    WiFi.localIP().toString().c_str(),
                    wifi_setup::apiBase().c_str());
      return true;
    }
    led(40, 40, 0);
    delay(250);
    led(0, 0, 0);
    delay(250);
  }

  Serial.println("[main] 首次连接超时，回退 DHCP 重试");
  WiFi.disconnect(false, false);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(ssid.c_str(), pass.c_str());
  start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_setup::saveCurrentStaIp();
      Serial.printf("[main] DHCP 已连接，IP=%s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    led(40, 40, 0);
    delay(250);
    led(0, 0, 0);
    delay(250);
  }

  Serial.println("[main] 连接超时");
  return false;
}

static void startNtp() {
  // 北京时间 UTC+8；不阻塞等待，scheduleSync 里会再认
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  if (now > 1700000000) {
    g_ntpOk = true;
    Serial.printf("[main] NTP OK %ld\n", (long)now);
  } else {
    Serial.println("[main] NTP 已发起，稍后就绪");
  }
}

// restoreLocalIfNoUpdate：开机用。就绪屏会盖住上次画面，若 sync 无新帧则强制刷回 /last.bin
static void runSyncWithLed(bool restoreLocalIfNoUpdate = false) {
  beginRenderLed();
  ink_sync::Result r = ink_sync::runOnce();
  if (restoreLocalIfNoUpdate && r == ink_sync::Result::OkNoUpdate &&
      frame_store::hasValidLastFrame()) {
    Serial.println("[main] sync 无新帧，恢复本地缓存画面");
    r = ink_sync::refreshLocal(true);
  }
  endRenderLed();
  if (r == ink_sync::Result::OkUpdated) {
    ledModeIdle();
  } else if (r == ink_sync::Result::OkNoUpdate) {
    // 无更新 / 绑定页已显示跳过：绿灯闪一下提示已查询
    led(0, 60, 0);
    delay(120);
    ledModeIdle();
  } else if (r == ink_sync::Result::Cancelled) {
    ledModeIdle();
  } else {
    ledFail();
    // 失败时刷 ASCII 提示，避免只剩白屏不知原因
    wifi_setup::showSyncFailScreen();
  }
}

static void handleButtons() {
  bool busy = ink_sync::isBusy();
  buttons::Event ev = buttons::poll(busy);
  if (ev == buttons::Event::None) return;

  bool unbound = !frame_store::bound();

  switch (ev) {
    case buttons::Event::ModeShort:
      if (unbound || g_provisioning) break;
      {
        auto m = frame_store::workMode();
        auto next = (m == frame_store::MODE_MINIPROG) ? frame_store::MODE_LAN
                                                      : frame_store::MODE_MINIPROG;
        frame_store::setWorkMode(next);
        ledModeIdle();
        Serial.printf("[main] workMode=%d\n", (int)next);
      }
      break;

    case buttons::Event::ActionCancel:
      ink_sync::requestCancel();
      break;

    case buttons::Event::ActionShort:
      // 未绑定单击：立即查是否已绑定；成功且有纪念日则拉图，否则继续 60s 轮询
      if (g_provisioning) break;
      if (unbound) {
        g_lastUnboundPollMs = millis();
        Serial.println("[main] 未绑定：单击执行键，立即查绑定");
        runSyncWithLed();
      }
      break;

    case buttons::Event::ActionDouble:
      if (g_provisioning || unbound) break;
      if (frame_store::workMode() == frame_store::MODE_MINIPROG ||
          frame_store::workMode() == frame_store::MODE_LAN) {
        beginRenderLed();
        auto r = ink_sync::refreshLocal();
        endRenderLed();
        if (r == ink_sync::Result::OkUpdated) {
          ledModeIdle();
        } else if (r == ink_sync::Result::Failed) {
          // 间隔未到或无缓存：闪一下提示
          ledFlashModeFast(false);
          delay(80);
          ledFlashModeFast(true);
          delay(80);
          ledModeIdle();
        } else {
          ledModeIdle();
        }
      }
      break;

    case buttons::Event::ActionLong:
      if (g_provisioning) break;
      if (frame_store::workMode() == frame_store::MODE_LAN && !unbound) {
        // 局域网模式长按：文档是显示传图地址；本轮未做 /upload，改为立即 sync 恢复正式画面
        runSyncWithLed();
      } else {
        runSyncWithLed();
      }
      break;

    case buttons::Event::SystemReconfig:
      Serial.println("[main] 系统键 3s：重配 WiFi");
      frame_store::clearWifiCreds();
      delay(200);
      ESP.restart();
      break;

    case buttons::Event::SystemFactory:
      Serial.println("[main] 系统键 8s：出厂重置");
      led(60, 0, 0);
      delay(300);
      frame_store::factoryReset();
      delay(200);
      ESP.restart();
      break;

    default:
      break;
  }
}

static void scheduleSync() {
  if (!g_localAdmin) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (frame_store::workMode() != frame_store::MODE_MINIPROG) return;
  if (ink_sync::isBusy()) return;

  if (!frame_store::bound()) {
    // 未绑定：约 60s 轮询一次
    if (millis() - g_lastUnboundPollMs >= 60000) {
      g_lastUnboundPollMs = millis();
      runSyncWithLed();
    }
    return;
  }

  // 已绑定：每日 syncHour 整点附近一次
  if (!g_ntpOk) {
    time_t now = time(nullptr);
    if (now > 1700000000) g_ntpOk = true;
    else return;
  }

  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  int yday = ti.tm_yday;
  uint8_t hour = wifi_setup::syncHour();
  if (ti.tm_hour == hour && ti.tm_min < 2 && g_lastSyncDay != yday) {
    g_lastSyncDay = yday;
    runSyncWithLed();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[main] 启动");

  rgb.begin();
  rgb.setBrightness(60);
  led(40, 40, 0);

  if (!epd::begin()) {
    led(60, 0, 0);
    return;
  }

  if (!frame_store::begin()) {
    Serial.println("[main] frame_store 失败，继续（无本地帧缓存）");
  }

  buttons::begin();

  if (tryConnectSaved(15000)) {
    ledModeIdle();
    wifi_setup::startLocalAdmin();
    g_localAdmin = true;
    startNtp();

    // 先 sync：有新帧只全刷一次；无新帧再恢复 /last.bin（避免「先出旧图再清屏出新图」双刷）
    // 等待期间墨屏双稳态仍保留断电前画面，不必先刷就绪屏
    g_lastUnboundPollMs = millis();
    if (frame_store::hasValidLastFrame()) {
      runSyncWithLed(true);
    } else {
      wifi_setup::showReadyScreen();
      runSyncWithLed(false);
    }
    return;
  }

  g_provisioning = true;
  wifi_setup::startAP();
}

void loop() {
  if (g_provisioning || g_localAdmin) {
    wifi_setup::loop();
  }

  handleButtons();

  if (g_provisioning) {
    if (WiFi.status() == WL_CONNECTED) {
      led(0, 60, 0);
      delay(50);
      return;
    }
    static uint32_t t = 0;
    static bool on = false;
    if (millis() - t > 600) {
      t = millis();
      on = !on;
      if (on) led(60, 26, 0);
      else led(0, 0, 0);
    }
    return;
  }

  if (g_localAdmin) {
    scheduleSync();
    delay(20);
    return;
  }

  delay(1000);
}
