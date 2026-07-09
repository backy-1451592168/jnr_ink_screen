// 自建配网 + 局域网设备管理页
//
// 配网：设备开热点 DayIJoy-心选日 + Captive Portal(192.168.8.1)
//   → 填 WiFi → 保持 AP 连上路由器 → /device 查看信息并设置服务地址 →「完成」重启
// 日常：STA 下本地 HTTP 长驻，可用固定局域网 IP 随时打开 /device 查看/修改
// 详见 docs/需求文档/硬件方案.md 第六节。
#pragma once

#include <Arduino.h>

namespace wifi_setup {

// 默认小程序/设备 API 根地址（路径写死在固件，仅域名端口等可配）
constexpr const char* kDefaultApiBase = "https://s1.z100.vip:7659/api";

// 在墨水屏上刷出「设备配网」画面（静态点阵 + 运行时 MAC）。
void showConfigScreen();

// 联网成功后的简要信息屏：局域网 IP / MAC / 浏览器地址（ASCII）。
void showReadyScreen();

// sync 失败时的 ASCII 提示屏（含当前 apiBase），避免白屏不知原因。
void showSyncFailScreen();

// 启动 AP + DNS 劫持 + HTTP 配网服务，并刷出配网画面。
void startAP();

// STA 已联网后启动局域网管理页（/device），不启 AP。
void startLocalAdmin();

// 主服务根地址；空则返回默认值。轮询时先试主，失败再试备，下次仍先主。
String apiBase();

// 备用服务根地址；未配置时返回空字符串。
String apiBaseBackup();

// 每日自动同步整点（0–23）；未配置时返回默认 0（午夜）。
uint8_t syncHour();

// 若 NVS 有上次成功的局域网地址，在 WiFi.begin 前调用以尽量固定 IP。
void applySavedStaticIp();

// 连接成功后把当前 DHCP/静态地址写入 NVS，供下次固定使用。
void saveCurrentStaIp();

// 在配网或本地管理期间的主循环里反复调用，处理 DNS 与 HTTP。
void loop();

// 当前是否有本地 HTTP 在跑（配网 AP 或 STA 管理页）。
bool httpActive();

}  // namespace wifi_setup
