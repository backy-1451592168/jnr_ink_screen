#include "epd.h"

#include <SPI.h>

#include "font12x24.h"

// 引脚宏未注入时给出与硬件方案一致的默认值
#ifndef PIN_EPD_DIN
#define PIN_EPD_DIN 9
#endif
#ifndef PIN_EPD_SCLK
#define PIN_EPD_SCLK 10
#endif
#ifndef PIN_EPD_CS
#define PIN_EPD_CS 11
#endif
#ifndef PIN_EPD_DC
#define PIN_EPD_DC 12
#endif
#ifndef PIN_EPD_RST
#define PIN_EPD_RST 13
#endif
#ifndef PIN_EPD_BUSY
#define PIN_EPD_BUSY 14
#endif

// 面板原生分辨率（横屏）
#define PANEL_W 800
#define PANEL_H 480

// 面板安装方向校正（竖屏帧缓冲 -> 800x480 横屏面板）
#ifndef EPD_MIRROR_H
#define EPD_MIRROR_H 1  // 实测需水平镜像，否则文字左右颠倒
#endif
#ifndef EPD_FLIP_V
#define EPD_FLIP_V 0    // 若上下颠倒，改为 1
#endif

namespace epd {

namespace {

const int STRIDE = W / 2;              // 每行字节数（2 像素/字节）= 240
const uint32_t FB_BYTES = (uint32_t)STRIDE * H;  // 192000
uint8_t* g_fb = nullptr;               // 竖屏帧缓冲，4bit 打包

SPISettings kSpi(4000000, MSBFIRST, SPI_MODE0);

void cmd(uint8_t c) {
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(c);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void data(uint8_t d) {
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transfer(d);
  digitalWrite(PIN_EPD_CS, HIGH);
}

void dataBulk(const uint8_t* buf, size_t len) {
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_CS, LOW);
  SPI.transferBytes(buf, nullptr, len);
  digitalWrite(PIN_EPD_CS, HIGH);
}

bool waitIdle(uint32_t timeoutMs = 30000) {
  uint32_t start = millis();
  while (digitalRead(PIN_EPD_BUSY) == LOW) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  return true;
}

// 竖屏 (x,y) -> 帧缓冲取色
inline uint8_t getPixel(int x, int y) {
  uint8_t b = g_fb[(uint32_t)y * STRIDE + (x >> 1)];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}

}  // namespace

bool begin() {
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);
  digitalWrite(PIN_EPD_CS, HIGH);

  SPI.begin(PIN_EPD_SCLK, -1, PIN_EPD_DIN, -1);

  if (!g_fb) g_fb = (uint8_t*)ps_malloc(FB_BYTES);   // 优先 PSRAM
  if (!g_fb) g_fb = (uint8_t*)malloc(FB_BYTES);
  if (!g_fb) {
    Serial.println("[epd] 帧缓冲分配失败");
    return false;
  }
  clear(WHITE);
  return true;
}

void clear(uint8_t color) {
  if (!g_fb) return;
  memset(g_fb, (color << 4) | color, FB_BYTES);
}

void drawPixel(int x, int y, uint8_t color) {
  if (!g_fb || x < 0 || y < 0 || x >= W || y >= H) return;
  uint32_t i = (uint32_t)y * STRIDE + (x >> 1);
  if (x & 1) {
    g_fb[i] = (g_fb[i] & 0xF0) | (color & 0x0F);
  } else {
    g_fb[i] = (g_fb[i] & 0x0F) | (color << 4);
  }
}

void drawImageRle(const uint8_t* rle, uint32_t rleLen) {
  if (!g_fb) return;
  uint32_t out = 0;
  for (uint32_t i = 0; i + 1 < rleLen && out < FB_BYTES; i += 2) {
    uint8_t count = pgm_read_byte(&rle[i]);
    uint8_t value = pgm_read_byte(&rle[i + 1]);
    while (count-- && out < FB_BYTES) g_fb[out++] = value;
  }
}

bool drawImageRaw(const uint8_t* data, size_t len) {
  if (!g_fb || !data || len != FB_BYTES) return false;
  memcpy(g_fb, data, FB_BYTES);
  return true;
}

uint32_t frameBytes() { return FB_BYTES; }

void drawText(int x, int y, const char* s, uint8_t color, uint8_t scale) {
  if (scale < 1) scale = 1;
  int cx = x;
  for (const char* p = s; *p; ++p) {
    uint8_t ch = (uint8_t)*p;
    if (ch < FONT12X24_FIRST || ch > FONT12X24_LAST) {
      cx += 12 * scale;
      continue;
    }
    // 每字符 FONT12X24_BYTES_PER_GLYPH 字节（24 行 x 2 字节/行）
    const uint8_t* glyph = &kFont12x24[(ch - FONT12X24_FIRST) * FONT12X24_BYTES_PER_GLYPH];
    for (int row = 0; row < 24; ++row) {
      uint8_t b0 = pgm_read_byte(&glyph[row * 2]);
      uint8_t b1 = pgm_read_byte(&glyph[row * 2 + 1]);
      for (int col = 0; col < 12; ++col) {
        uint8_t bit = (col < 8) ? (b0 & (0x80 >> col)) : (b1 & (0x80 >> (col - 8)));
        if (bit) {
          for (int sy = 0; sy < scale; ++sy)
            for (int sx = 0; sx < scale; ++sx)
              drawPixel(cx + col * scale + sx, y + row * scale + sy, color);
        }
      }
    }
    cx += 12 * scale;
  }
}

void panelInit() {
  digitalWrite(PIN_EPD_RST, LOW);
  delay(10);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(10);

  cmd(0xAA);
  data(0x49); data(0x55); data(0x20); data(0x08); data(0x09); data(0x18);
  cmd(0x01); data(0x3F);
  cmd(0x00); data(0x5F); data(0x69);
  cmd(0x03); data(0x00); data(0x54); data(0x00); data(0x44);
  cmd(0x05); data(0x40); data(0x1F); data(0x1F); data(0x2C);
  cmd(0x06); data(0x6F); data(0x1F); data(0x17); data(0x49);
  cmd(0x08); data(0x6F); data(0x1F); data(0x1F); data(0x22);
  cmd(0x30); data(0x08);
  cmd(0x50); data(0x3F);
  cmd(0x60); data(0x02); data(0x00);
  cmd(0x61); data(0x03); data(0x20); data(0x01); data(0xE0);
  cmd(0x84); data(0x01);
  cmd(0xE3); data(0x2F);
  cmd(0x04);  // PWR ON
  waitIdle();
}

bool flush() {
  if (!g_fb) return false;
  SPI.beginTransaction(kSpi);
  panelInit();

  static uint8_t rowBuf[PANEL_W / 2];  // 面板一行 800px = 400 字节

  cmd(0x10);  // DTM 进入写显存
  // 竖屏(u,v) 转置到面板横屏：u=y2(0..479), v=x2(0..799)
  for (int y2 = 0; y2 < PANEL_H; ++y2) {
    int pu = EPD_FLIP_V ? (W - 1 - y2) : y2;
    for (int x2 = 0; x2 < PANEL_W; x2 += 2) {
      int v0 = EPD_MIRROR_H ? (H - 1 - x2) : x2;
      int v1 = EPD_MIRROR_H ? (H - 1 - (x2 + 1)) : (x2 + 1);
      uint8_t cHi = getPixel(pu, v0);
      uint8_t cLo = getPixel(pu, v1);
      rowBuf[x2 / 2] = (cHi << 4) | cLo;
    }
    dataBulk(rowBuf, sizeof(rowBuf));
  }

  cmd(0x04);  // PWR ON
  if (!waitIdle()) { SPI.endTransaction(); return false; }

  cmd(0x06);  // BTST2 二次设置（厂商时序要求）
  data(0x6F); data(0x1F); data(0x17); data(0x49);

  cmd(0x12);  // DRF 刷新
  data(0x00);
  bool ok = waitIdle();

  cmd(0x02);  // POF 下电
  data(0x00);
  waitIdle();

  SPI.endTransaction();
  return ok;
}

}  // namespace epd
