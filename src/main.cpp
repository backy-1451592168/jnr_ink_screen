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
static uint32_t g_wifiLostAt = 0;
static bool g_wifiStatusShown = false;
static uint32_t g_wifiRetryAt = 0;
static uint32_t g_lanRemindAt = 0;

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

// 忙时主循环进不来：在 hook 里扫执行键，下载阶段可取消（全刷波形无法中断）
static void busyTick() {
  ledPurpleBreatheTick();
  if (buttons::poll(true) != buttons::Event::ActionCancel) return;
  // 未绑定查绑定：忽略忙时单击取消，避免连点把刚发起的 sync 停掉
  if (!frame_store::bound()) return;
  ink_sync::requestCancel();
  wifi_setup::requestLanCancel();
  // 黄闪两下确认按键；若已进入全刷则无法中断，至少有反馈
  led(60, 40, 0);
  delay(70);
  led(0, 0, 0);
  delay(70);
  led(60, 40, 0);
  delay(70);
}

static void beginRenderLed() {
  ink_sync::setActivityHook(busyTick);
  epd::setBusyPollHook(busyTick);
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

  // 有旧图：先保持双稳态，慢连再刷状态；无旧图：尽快刷一次反馈。
  // 墨屏全刷 12–20s 且阻塞，但 WiFi 栈在后台仍会关联——刷屏时间绝不能计入超时预算。
  const bool keepOldFrame = frame_store::hasValidLastFrame();
  constexpr uint32_t kStatusAfterMs = 2500;

  auto pollUntil = [&](uint32_t budgetMs) -> bool {
    uint32_t waited = 0;
    while (waited < budgetMs) {
      if (WiFi.status() == WL_CONNECTED) return true;
      led(40, 40, 0);
      delay(250);
      led(0, 0, 0);
      delay(250);
      waited += 500;
    }
    return WiFi.status() == WL_CONNECTED;
  };

  auto onConnected = [&](const char* how) {
    wifi_setup::saveCurrentStaIp();
    Serial.printf("[main] %s，IP=%s apiBase=%s\n", how,
                  WiFi.localIP().toString().c_str(),
                  wifi_setup::apiBase().c_str());
  };

  Serial.printf("[main] 尝试连接已存 WiFi: %s\n", ssid.c_str());
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  wifi_setup::applySavedStaticIp();
  WiFi.begin(ssid.c_str(), pass.c_str());

  const uint32_t graceMs = keepOldFrame ? kStatusAfterMs : 0;
  const uint32_t grace = graceMs > timeoutMs ? timeoutMs : graceMs;
  if (grace > 0 && pollUntil(grace)) {
    onConnected("已连接");
    return true;
  }

  // 整次开机只全刷这一次状态屏；DHCP 回退不再二次刷。
  if (WiFi.status() != WL_CONNECTED) {
    wifi_setup::showStatusScreen("WiFi连接中...", ssid.c_str());
    if (WiFi.status() == WL_CONNECTED) {
      onConnected("刷屏期间已连接");
      return true;
    }
  }

  const uint32_t remain = timeoutMs - grace;
  if (remain > 0 && pollUntil(remain)) {
    onConnected("已连接");
    return true;
  }

  Serial.println("[main] 首次连接超时，回退 DHCP 重试");
  WiFi.disconnect(false, false);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(ssid.c_str(), pass.c_str());
  if (pollUntil(timeoutMs)) {
    onConnected("DHCP 已连接");
    return true;
  }

  Serial.println("[main] 连接超时");
  return false;
}

static void startNtp() {
  // UTC + POSIX TZ（CST-8 = 东八区）；比单靠 configTime 偏移更稳
  configTime(0, 0, "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CST-8", 1);
  tzset();
  time_t now = time(nullptr);
  if (now > 1700000000) {
    g_ntpOk = true;
    Serial.printf("[main] NTP OK %ld\n", (long)now);
  } else {
    Serial.println("[main] NTP 已发起，稍后就绪");
  }
}

/** NTP 就绪后：若本地时间已过今日 syncHour，把开机那次 sync 记成今日已同步 */
static void markDailySyncDoneIfPastHour() {
  if (!frame_store::bound()) return;
  time_t now = time(nullptr);
  if (now <= 1700000000) return;
  g_ntpOk = true;
  struct tm ti;
  localtime_r(&now, &ti);
  uint8_t hour = wifi_setup::syncHour();
  if (ti.tm_hour >= hour) {
    g_lastSyncDay = ti.tm_yday;
    Serial.printf("[main] 开机已过 syncHour=%u，记今日已同步 yday=%d\n", hour, g_lastSyncDay);
  }
}

// restoreLocalIfNoUpdate：开机用。就绪屏会盖住上次画面，若 sync 无新帧则强制刷回 /last.bin
// 返回本次 sync 结果，供开机决定是否记「今日已同步」。
static ink_sync::Result runSyncWithLed(bool restoreLocalIfNoUpdate = false) {
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
    // 有本地画面时只闪红灯，避免失败页盖掉纪念日/绑定二维码
    if (!frame_store::hasValidLastFrame()) {
      wifi_setup::showSyncFailScreen();
    }
  }
  return r;
}

static void handleButtons() {
  bool busy = ink_sync::isBusy() || wifi_setup::lanBusy();
  buttons::Event ev = buttons::poll(busy);
  if (ev == buttons::Event::None) return;

  bool unbound = !frame_store::bound();

  switch (ev) {
    case buttons::Event::ModeDouble:
      if (unbound || g_provisioning) break;
      {
        auto m = frame_store::workMode();
        auto next = (m == frame_store::MODE_MINIPROG) ? frame_store::MODE_LAN
                                                      : frame_store::MODE_MINIPROG;
        frame_store::setWorkMode(next);
        wifi_setup::setLanUploadEnabled(next == frame_store::MODE_LAN);
        g_lanRemindAt = millis();  // 进入局域网模式后重新计时提醒
        // 连闪三次，避免只改常亮色看不清
        for (int i = 0; i < 3; i++) {
          ledFlashModeFast(true);
          delay(120);
          ledFlashModeFast(false);
          delay(120);
        }
        ledModeIdle();
        Serial.printf("[main] workMode=%d lanUpload=%d\n", (int)next,
                      (int)(next == frame_store::MODE_LAN));
        // 只改 LED；传图地址仍用执行键长按刷出，避免无谓全刷
      }
      break;

    case buttons::Event::ActionCancel:
      if (!frame_store::bound()) break;
      ink_sync::requestCancel();
      wifi_setup::requestLanCancel();
      led(60, 40, 0);
      delay(70);
      led(0, 0, 0);
      delay(70);
      led(60, 40, 0);
      delay(70);
      break;

    case buttons::Event::ActionShort:
      // 未绑定单击：立即查是否已绑定；成功且有纪念日则拉图，否则继续 60s 轮询
      if (g_provisioning) break;
      if (unbound) {
        g_lastUnboundPollMs = millis();
        Serial.println("[main] 未绑定：单击执行键，立即查绑定");
        runSyncWithLed();
      } else {
        // 已绑定单击无业务：短闪提示「已收到」，避免误以为按键失灵
        ledFlashModeFast(false);
        delay(60);
        ledModeIdle();
      }
      break;

    case buttons::Event::ActionDouble:
      if (g_provisioning || unbound) break;
      if (frame_store::workMode() == frame_store::MODE_MINIPROG ||
          frame_store::workMode() == frame_store::MODE_LAN) {
        beginRenderLed();
        // 用户主动重刷：忽略最小间隔（间隔只约束自动路径）
        auto r = ink_sync::refreshLocal(true);
        endRenderLed();
        if (r == ink_sync::Result::OkUpdated) {
          ledModeIdle();
        } else if (r == ink_sync::Result::Failed) {
          // 无缓存等：闪一下提示
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
        // 长按：显示/退出传图地址页
        if (wifi_setup::toggleUploadAddressScreen()) {
          ledModeIdle();
        } else {
          beginRenderLed();
          auto r = ink_sync::refreshLocal(true);
          endRenderLed();
          if (r != ink_sync::Result::OkUpdated) ledFail();
          else ledModeIdle();
        }
      } else {
        runSyncWithLed();
      }
      break;

    case buttons::Event::SystemReconfig:
      Serial.println("[main] 系统键 3s：重配 WiFi");
      led(60, 26, 0);
      delay(200);
      frame_store::clearWifiCreds();
      delay(200);
      ESP.restart();
      break;

    case buttons::Event::SystemFactory:
      Serial.println("[main] 系统键 8s：出厂重置");
      // 出厂只清本地；云端绑定需小程序解绑，先刷提示再清机
      wifi_setup::showStatusScreen("出厂重置", "请先小程序解绑");
      for (int i = 0; i < 4; i++) {
        led(60, 0, 0);
        delay(80);
        led(0, 0, 0);
        delay(80);
      }
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

  // 已绑定：每日 syncHour 起可触发一次；错过整点也会在当天补跑
  if (!g_ntpOk) {
    time_t t = time(nullptr);
    if (t > 1700000000) g_ntpOk = true;
    else return;
  }

  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  if (g_lastSyncDay == ti.tm_yday) return;

  uint8_t hour = wifi_setup::syncHour();
  if (ti.tm_hour < hour) return;

  g_lastSyncDay = ti.tm_yday;
  Serial.printf("[main] 每日 sync syncHour=%u %02d:%02d yday=%d\n", hour,
                ti.tm_hour, ti.tm_min, ti.tm_yday);
  runSyncWithLed();
}

/** 系统键按住过程灯提示：橙→黄闪（可重配）→红快闪（将出厂） */
static void tickSystemHoldLed() {
  uint8_t lv = buttons::systemHoldLevel();
  static uint8_t last = 0;
  static uint32_t blinkAt = 0;
  static bool on = false;
  if (lv == 0) {
    if (last != 0) ledModeIdle();
    last = 0;
    return;
  }
  uint32_t now = millis();
  if (lv == 1) {
    led(60, 26, 0);  // 橙：未满 3s
  } else if (lv == 2) {
    if (now - blinkAt > 400) {
      blinkAt = now;
      on = !on;
      if (on) led(60, 45, 0);  // 黄闪：松手即重配
      else led(0, 0, 0);
    }
  } else {
    if (now - blinkAt > 120) {
      blinkAt = now;
      on = !on;
      if (on) led(60, 0, 0);  // 红快闪：即将出厂
      else led(0, 0, 0);
    }
  }
  last = lv;
}

/** STA 运行中断网：黄闪 + 必要时刷「WiFi连接中」并周期性 WiFi.begin */
static void maintainWifi() {
  if (!g_localAdmin || g_provisioning) return;
  if (ink_sync::isBusy() || wifi_setup::lanBusy()) return;
  // 系统键按住时不抢灯、不刷屏
  if (buttons::systemHoldLevel() != 0) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (g_wifiLostAt == 0) return;
    Serial.println("[main] WiFi 已恢复");
    wifi_setup::saveCurrentStaIp();
    if (g_wifiStatusShown && frame_store::hasValidLastFrame()) {
      beginRenderLed();
      auto r = ink_sync::refreshLocal(true);
      endRenderLed();
      if (r != ink_sync::Result::OkUpdated) ledFail();
      else ledModeIdle();
    } else {
      ledModeIdle();
    }
    g_wifiLostAt = 0;
    g_wifiStatusShown = false;
    return;
  }

  uint32_t now = millis();
  if (g_wifiLostAt == 0) {
    g_wifiLostAt = now;
    g_wifiRetryAt = now;
    Serial.println("[main] WiFi 断开，准备重连");
    return;
  }

  if (!g_wifiStatusShown && (now - g_wifiLostAt) >= 2500) {
    Preferences prefs;
    prefs.begin("jnr", true);
    String ssid = prefs.getString("wifiSsid", "");
    prefs.end();
    wifi_setup::showStatusScreen("WiFi连接中...",
                                 ssid.isEmpty() ? nullptr : ssid.c_str());
    g_wifiStatusShown = true;
  }

  if (now - g_wifiRetryAt >= 15000) {
    g_wifiRetryAt = now;
    Preferences prefs;
    prefs.begin("jnr", true);
    String ssid = prefs.getString("wifiSsid", "");
    String pass = prefs.getString("wifiPass", "");
    prefs.end();
    if (!ssid.isEmpty()) {
      Serial.printf("[main] WiFi 重连尝试 %s\n", ssid.c_str());
      wifi_setup::applySavedStaticIp();
      WiFi.disconnect(false, false);
      delay(50);
      WiFi.begin(ssid.c_str(), pass.c_str());
    }
  }

  // 断约 5 分钟仍失败：清凭证重启进配网（密码变更等）
  constexpr uint32_t kWifiGiveUpMs = 300000;
  if (now - g_wifiLostAt >= kWifiGiveUpMs) {
    Preferences prefs;
    prefs.begin("jnr", true);
    String ssid = prefs.getString("wifiSsid", "");
    prefs.end();
    Serial.println("[main] WiFi 长时间失败，清除凭证并进配网");
    wifi_setup::showStatusScreen("连接失败进入配网",
                                 ssid.isEmpty() ? nullptr : ssid.c_str());
    frame_store::clearWifiCreds();
    delay(300);
    ESP.restart();
  }

  static uint32_t blinkAt = 0;
  static bool on = false;
  if (now - blinkAt > 500) {
    blinkAt = now;
    on = !on;
    if (on) led(40, 40, 0);
    else led(0, 0, 0);
  }
}

/** 局域网模式停留过久：每 10 分钟青灯连闪，提醒可双击模式键切回推送 */
static void tickLanModeRemind() {
  if (!g_localAdmin || g_provisioning) return;
  if (frame_store::workMode() != frame_store::MODE_LAN) {
    g_lanRemindAt = 0;
    return;
  }
  if (ink_sync::isBusy() || wifi_setup::lanBusy()) return;
  if (buttons::systemHoldLevel() != 0) return;
  if (g_wifiLostAt != 0) return;

  uint32_t now = millis();
  if (g_lanRemindAt == 0) {
    g_lanRemindAt = now;
    return;
  }
  if (now - g_lanRemindAt < 600000) return;  // 10 分钟
  g_lanRemindAt = now;
  Serial.println("[main] 局域网模式提醒：仍为青灯，可双击模式键切回");
  for (int i = 0; i < 5; i++) {
    led(0, 50, 50);
    delay(160);
    led(0, 0, 0);
    delay(160);
  }
  ledModeIdle();
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
  // NVS 屏参 → epd 逻辑分辨率（须在 begin 之后）
  frame_store::applyStoredScreenSize();

  buttons::begin();

  // 有凭证时立刻 WiFi.begin（见 tryConnectSaved）；勿在此前全刷「初始化中」，
  // 否则白白推迟关联，且多一次 12–20s 全刷。无凭证则很快落入 startAP 配网屏。
  if (tryConnectSaved(20000)) {
    ledModeIdle();
    wifi_setup::startLocalAdmin();
    g_localAdmin = true;
    startNtp();

    // 先 sync：有新帧只全刷一次；无新帧再恢复 /last.bin（避免「先出旧图再清屏出新图」双刷）
    // 等待期间墨屏双稳态仍保留断电前画面，不必先刷就绪屏
    g_lastUnboundPollMs = millis();
    wifi_setup::setLanUploadEnabled(frame_store::workMode() == frame_store::MODE_LAN);
    ink_sync::Result bootSync;
    if (frame_store::hasValidLastFrame()) {
      bootSync = runSyncWithLed(true);
    } else {
      wifi_setup::showReadyScreen();
      bootSync = runSyncWithLed(false);
    }
    // 仅开机 sync 成功（含无更新）才记今日已同步；失败则允许当天自动补拉
    if (bootSync == ink_sync::Result::OkUpdated ||
        bootSync == ink_sync::Result::OkNoUpdate) {
      markDailySyncDoneIfPastHour();
    }
    return;
  }

  g_provisioning = true;
  // 刷配网屏期间 setup() 未返回，loop 橙闪还没跑；BUSY 等待时闪橙，避免一直停在开机黄灯
  led(60, 26, 0);
  epd::setBusyPollHook([]() {
    static uint32_t t = 0;
    static bool on = false;
    if (millis() - t > 400) {
      t = millis();
      on = !on;
      if (on) led(60, 26, 0);
      else led(0, 0, 0);
    }
  });
  wifi_setup::startAP();
  epd::setBusyPollHook(nullptr);
  led(60, 26, 0);
}

void loop() {
  if (g_provisioning || g_localAdmin) {
    wifi_setup::loop();
  }

  handleButtons();
  tickSystemHoldLed();

  if (g_provisioning) {
    if (WiFi.status() == WL_CONNECTED) {
      led(0, 60, 0);
      delay(50);
      return;
    }
    // 系统键按住时 tickSystemHoldLed 已接管灯色
    if (buttons::systemHoldLevel() == 0) {
      static uint32_t t = 0;
      static bool on = false;
      if (millis() - t > 600) {
        t = millis();
        on = !on;
        if (on) led(60, 26, 0);
        else led(0, 0, 0);
      }
    }
    return;
  }

  if (g_localAdmin) {
    maintainWifi();
    tickLanModeRemind();
    int lanR = wifi_setup::pollLanUploadApply(busyTick);
    if (lanR == 1) ledModeIdle();
    else if (lanR < 0) ledFail();
    scheduleSync();
    delay(20);
    return;
  }

  delay(1000);
}
