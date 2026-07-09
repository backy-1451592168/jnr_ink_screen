// NVS 状态 + LittleFS 整帧缓存（/last.bin）
#pragma once

#include <Arduino.h>

namespace frame_store {

enum WorkMode : uint8_t {
  MODE_MINIPROG = 0,  // 小程序推送（绿灯）
  MODE_LAN = 1,       // 局域网传图（青灯；本轮仅切模式停轮询）
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

uint32_t lastRefreshTs();
void setLastRefreshTs(uint32_t ts);

// 整帧读写；len 须为 epd::frameBytes()
bool saveLastFrame(const uint8_t* data, size_t len, uint32_t crc);
bool loadLastFrame(uint8_t* out, size_t len, uint32_t* outCrc);
bool hasValidLastFrame();
void clearLastFrame();

// 仅清 WiFi 凭证（系统键 3s 重配网）
void clearWifiCreds();

// 出厂：清 WiFi + 模式/版本/绑定 + 删帧文件
void factoryReset();

}  // namespace frame_store
