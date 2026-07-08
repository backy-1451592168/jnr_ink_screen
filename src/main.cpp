// 水墨屏纪念日相框 —— 配网入口
//
// 本阶段实现「自建配网」：
//   1. 上电读取 NVS 里的 WiFi 凭证；
//   2. 有凭证则尝试连接路由器，连上即进入日常态（后续轮询逻辑另行实现）；
//   3. 无凭证 / 连接失败 → 开热点 DayIJoy-心选日，墨屏刷出「设备配网」画面，
//      手机连热点后扫码 / 自动弹出网页(192.168.8.1)填写 WiFi 完成配网。
//
// 引脚由 platformio.ini 的 -D 宏注入；E6 时序见 lib/epd。
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WiFi.h>

#include "epd.h"
#include "wifi_setup.h"

#ifndef PIN_RGB_LED
#define PIN_RGB_LED 48
#endif

static Adafruit_NeoPixel rgb(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
static void led(uint8_t r, uint8_t g, uint8_t b) {
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

static bool g_provisioning = false;

// 用已存凭证尝试连接路由器
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
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[main] 已连接，IP=%s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    led(40, 40, 0);  // 黄灯慢闪：联网中
    delay(250);
    led(0, 0, 0);
    delay(250);
  }
  Serial.println("[main] 连接超时");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[main] 启动");

  rgb.begin();
  rgb.setBrightness(60);
  led(40, 40, 0);  // 黄灯：初始化中

  if (!epd::begin()) {
    led(60, 0, 0);  // 红灯：墨屏帧缓冲分配失败
    return;
  }

  if (tryConnectSaved(15000)) {
    led(0, 60, 0);  // 绿灯常亮：已联网（小程序推送模式待实现）
    return;
  }

  // 无凭证 / 连接失败 → 进入配网
  g_provisioning = true;
  wifi_setup::startAP();
}

void loop() {
  if (!g_provisioning) {
    delay(1000);
    return;
  }
  wifi_setup::loop();

  // 橙灯慢闪：AP 配网中（硬件方案 6.2）
  static uint32_t t = 0;
  static bool on = false;
  if (millis() - t > 600) {
    t = millis();
    on = !on;
    if (on) led(60, 26, 0); else led(0, 0, 0);
  }
}
