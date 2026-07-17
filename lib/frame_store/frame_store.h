// NVS 状态 + LittleFS 整帧缓存（/last.bin）
#pragma once

#include <Arduino.h>

namespace frame_store {

enum WorkMode : uint8_t {
  MODE_MINIPROG = 0,  // 小程序推送（绿灯）
  MODE_LAN = 1,       // 局域网传图（青灯 + /upload）
};

bool begin();

WorkMode workMode();
void setWorkMode(WorkMode m);

uint32_t contentVersion();
void setContentVersion(uint32_t v);

uint32_t frameCrc();
void setFrameCrc(uint32_t crc);

bool bound();
void setBound(bool v);

// 云端签发的设备凭证（sync 响应 deviceSecret；请求头 X-Ink-Device-Secret）
String deviceSecret();
void setDeviceSecret(const String& secret);
void clearDeviceSecret();

uint32_t lastRefreshTs();
void setLastRefreshTs(uint32_t ts);

// 屏参（逻辑分辨率）：默认竖屏 480×800；可选横屏 800×480
int screenWidth();
int screenHeight();
// 白名单校验 → 写 NVS → epd::setLogicalSize；方向变化时清 /last.bin（旧帧布局失效）
bool setScreenSize(int w, int h);
// 开机：从 NVS 读屏参应用到 epd（须在 epd::begin 之后调用）
void applyStoredScreenSize();

// 整帧读写；len 须为 epd::frameBytes()
bool saveLastFrame(const uint8_t* data, size_t len, uint32_t crc);
bool loadLastFrame(uint8_t* out, size_t len, uint32_t* outCrc);
bool hasValidLastFrame();
void clearLastFrame();

// 仅清 WiFi 凭证（系统键 3s 重配网）
void clearWifiCreds();

// 出厂：清 WiFi + 模式/版本/绑定/屏参 + 删帧文件
void factoryReset();

}  // namespace frame_store
