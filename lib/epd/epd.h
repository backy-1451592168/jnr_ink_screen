// 7.3" E6 六色墨水屏（GDEP073E01 / 800x480）驱动
//
// 面板原生是横屏 800×480。逻辑分辨率支持两种（像素数相同，帧均为 192000 字节）：
//   - 竖屏 480×800：设计稿坐标系；flush 时旋转 90° 写入面板
//   - 横屏 800×480：与面板同向；flush 时 1:1 写入（竖屏才套 EPD_MIRROR_H）
//
// 时序 / 初始化序列沿用已验证可用的 E6 裸驱动。引脚由 platformio.ini 的 -D 宏注入。
#pragma once

#include <Arduino.h>

namespace epd {

// 白名单逻辑分辨率（与 Node / 小程序屏参一致）
constexpr int kPortraitW = 480;
constexpr int kPortraitH = 800;
constexpr int kLandscapeW = 800;
constexpr int kLandscapeH = 480;

// E6 面板 4bit 颜色索引（见 docs 硬件方案 3.1，须与图像打包一致）
enum : uint8_t {
  BLACK = 0x00,
  WHITE = 0x01,
  YELLOW = 0x02,
  RED = 0x03,
  BLUE = 0x05,
  GREEN = 0x06,
};

// 初始化 SPI + 分配 PSRAM 帧缓冲。返回 false 表示帧缓冲分配失败。
bool begin();

// 当前逻辑宽高（绘制坐标系）
int width();
int height();
bool isLandscape();

// 切换逻辑分辨率：仅接受 480×800 或 800×480。像素数不变，不重分配缓冲。
// 返回 false 表示非法尺寸。不写 NVS（持久化见 frame_store）。
bool setLogicalSize(int w, int h);

// 用单色填充整块帧缓冲
void clear(uint8_t color = WHITE);

// 逻辑坐标单像素写入（越界忽略）
void drawPixel(int x, int y, uint8_t color);

// 把 RLE (count,value) 点阵还原进帧缓冲；数据须匹配当前逻辑分辨率的行跨度
void drawImageRle(const uint8_t* rle, uint32_t rleLen);

// 写入原始 4bit 打包帧（须恰好 frameBytes()=192000，与 Node packE6 同序）
bool drawImageRaw(const uint8_t* data, size_t len);

// 整帧字节数（480×800 与 800×480 六色均为 192000）
uint32_t frameBytes();

// 用 12×24 ASCII + 24×24 中文子集绘制 UTF-8 字符串（scale 为整数放大倍数）
void drawText(int x, int y, const char* s, uint8_t color, uint8_t scale = 1);

// 面板上电初始化（每次刷屏前调用）
void panelInit();

// 把帧缓冲按当前方向映射到面板并下电。返回 false 表示 BUSY 超时。
bool flush();

// BUSY 等待 / 写显存循环中周期性回调（紫灯呼吸）；传 nullptr 清除
using BusyPollHook = void (*)();
void setBusyPollHook(BusyPollHook fn);

}  // namespace epd
