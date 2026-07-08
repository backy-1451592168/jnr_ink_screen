// 自建配网：设备开热点 DayIJoy-心选日 + Captive Portal 网页(192.168.8.1)
// 用户连热点 → 扫码/自动弹出配网页 → 填写路由器 WiFi → 保存到 NVS → 重启联网。
// 详见 docs/需求文档/硬件方案.md 第六节。
#pragma once

#include <Arduino.h>

namespace wifi_setup {

// 在墨水屏上刷出「设备配网」画面（静态点阵 + 运行时 MAC）。
void showConfigScreen();

// 启动 AP + DNS 劫持 + HTTP 配网服务，并刷出配网画面。
void startAP();

// 在配网期间的主循环里反复调用，处理 DNS 与 HTTP 请求。
void loop();

}  // namespace wifi_setup
