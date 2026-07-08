// 7.3" E6 六色墨水屏（GDEP073E01 / 800x480）驱动 —— 竖屏帧缓冲版
//
// 设计稿是竖屏（480 宽 × 800 高），面板原生是横屏 800×480；本驱动内部维护一块
// 竖屏帧缓冲，绘制全部用竖屏坐标，flush 时再旋转 90° 写入面板。
//
// 时序 / 初始化序列沿用 src/main.cpp 中已验证可用的 E6 裸驱动，只是把「六色竖条」
// 换成「整帧帧缓冲」。引脚由 platformio.ini 的 -D 宏注入。
#pragma once

#include <Arduino.h>

namespace epd {

// 竖屏逻辑分辨率（绘制坐标系）
static const int W = 480;
static const int H = 800;

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

// 用单色填充整块帧缓冲
void clear(uint8_t color = WHITE);

// 竖屏坐标单像素写入（越界忽略）
void drawPixel(int x, int y, uint8_t color);

// 把 RLE (count,value) 点阵还原进帧缓冲；数据须为 480x800、4bit 打包（240B/行）
void drawImageRle(const uint8_t* rle, uint32_t rleLen);

// 用 8x16 ASCII 字体绘制字符串（scale 为整数放大倍数）
void drawText(int x, int y, const char* s, uint8_t color, uint8_t scale = 1);

// 面板上电初始化（每次刷屏前调用）
void panelInit();

// 把帧缓冲旋转 90° 刷到面板并下电。返回 false 表示 BUSY 超时。
bool flush();

}  // namespace epd
