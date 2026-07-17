#include "epd.h"

#include <SPI.h>

#include "font12x24.h"
#include "font_cjk24.h"

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

// 面板安装方向校正
#ifndef EPD_MIRROR_H
#define EPD_MIRROR_H 1  // 实测需水平镜像，否则文字左右颠倒
#endif
#ifndef EPD_FLIP_V
#define EPD_FLIP_V 0    // 若上下颠倒，改为 1
#endif

namespace epd {

namespace {

// 逻辑分辨率（默认竖屏）；行跨度 = width/2（4bit 两像素一字节）
int g_w = kPortraitW;
int g_h = kPortraitH;
int g_stride = kPortraitW / 2;

// 两种方向像素数相同，帧缓冲大小固定
const uint32_t FB_BYTES = (uint32_t)(kPortraitW * kPortraitH) / 2;  // 192000
uint8_t* g_fb = nullptr;

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

BusyPollHook g_busyPollHook = nullptr;

bool waitIdle(uint32_t timeoutMs = 30000) {
  uint32_t start = millis();
  while (digitalRead(PIN_EPD_BUSY) == LOW) {
    if (millis() - start > timeoutMs) return false;
    if (g_busyPollHook) g_busyPollHook();
    delay(1);
  }
  return true;
}

inline uint8_t getPixel(int x, int y) {
  uint8_t b = g_fb[(uint32_t)y * g_stride + (x >> 1)];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}

// 横屏：逻辑坐标 ≈ 面板坐标；不套 EPD_MIRROR_H（竖屏旋转才需要，横屏再镜像会左右颠倒）
void flushLandscape(uint8_t* rowBuf) {
  for (int y2 = 0; y2 < PANEL_H; ++y2) {
    if (g_busyPollHook && (y2 & 15) == 0) g_busyPollHook();
    int ly = EPD_FLIP_V ? (g_h - 1 - y2) : y2;
    for (int x2 = 0; x2 < PANEL_W; x2 += 2) {
      rowBuf[x2 / 2] = (getPixel(x2, ly) << 4) | getPixel(x2 + 1, ly);
    }
    dataBulk(rowBuf, PANEL_W / 2);
  }
}

// 竖屏：逻辑 (x,y)=(480,800) 旋转到面板 (800,480)
void flushPortrait(uint8_t* rowBuf) {
  for (int y2 = 0; y2 < PANEL_H; ++y2) {
    if (g_busyPollHook && (y2 & 15) == 0) g_busyPollHook();
    int pu = EPD_FLIP_V ? (g_w - 1 - y2) : y2;
    for (int x2 = 0; x2 < PANEL_W; x2 += 2) {
      int v0 = EPD_MIRROR_H ? (g_h - 1 - x2) : x2;
      int v1 = EPD_MIRROR_H ? (g_h - 1 - (x2 + 1)) : (x2 + 1);
      rowBuf[x2 / 2] = (getPixel(pu, v0) << 4) | getPixel(pu, v1);
    }
    dataBulk(rowBuf, PANEL_W / 2);
  }
}

}  // namespace

bool begin() {
  pinMode(PIN_EPD_CS, OUTPUT);
  pinMode(PIN_EPD_DC, OUTPUT);
  pinMode(PIN_EPD_RST, OUTPUT);
  pinMode(PIN_EPD_BUSY, INPUT);
  digitalWrite(PIN_EPD_CS, HIGH);

  SPI.begin(PIN_EPD_SCLK, -1, PIN_EPD_DIN, -1);

  if (!g_fb) g_fb = (uint8_t*)ps_malloc(FB_BYTES);
  if (!g_fb) g_fb = (uint8_t*)malloc(FB_BYTES);
  if (!g_fb) {
    Serial.println("[epd] 帧缓冲分配失败");
    return false;
  }
  clear(WHITE);
  return true;
}

int width() { return g_w; }
int height() { return g_h; }
bool isLandscape() { return g_w > g_h; }

bool setLogicalSize(int w, int h) {
  const bool ok =
      (w == kPortraitW && h == kPortraitH) ||
      (w == kLandscapeW && h == kLandscapeH);
  if (!ok) return false;
  if (w == g_w && h == g_h) return true;
  g_w = w;
  g_h = h;
  g_stride = w / 2;
  Serial.printf("[epd] 逻辑分辨率 %dx%d (%s)\n", w, h, isLandscape() ? "横屏" : "竖屏");
  return true;
}

void clear(uint8_t color) {
  if (!g_fb) return;
  memset(g_fb, (color << 4) | color, FB_BYTES);
}

void drawPixel(int x, int y, uint8_t color) {
  if (!g_fb || x < 0 || y < 0 || x >= g_w || y >= g_h) return;
  uint32_t i = (uint32_t)y * g_stride + (x >> 1);
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

const FontCjkGlyph* findCjkGlyph(uint16_t code) {
  if (FONT_CJK_COUNT <= 0) return nullptr;
  int lo = 0, hi = FONT_CJK_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint16_t c = pgm_read_word(&kFontCjk[mid].code);
    if (c == code) return &kFontCjk[mid];
    if (c < code) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

// 解码一个 UTF-8 码点；返回消耗字节数，失败返回 0
int decodeUtf8(const char* s, uint32_t* outCode) {
  const uint8_t c0 = (uint8_t)s[0];
  if (c0 < 0x80) {
    *outCode = c0;
    return 1;
  }
  if ((c0 & 0xE0) == 0xC0 && s[1]) {
    *outCode = ((c0 & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
    return 2;
  }
  if ((c0 & 0xF0) == 0xE0 && s[1] && s[2]) {
    *outCode = ((c0 & 0x0F) << 12) | (((uint8_t)s[1] & 0x3F) << 6) | ((uint8_t)s[2] & 0x3F);
    return 3;
  }
  if ((c0 & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
    *outCode = ((c0 & 0x07) << 18) | (((uint8_t)s[1] & 0x3F) << 12) |
               (((uint8_t)s[2] & 0x3F) << 6) | ((uint8_t)s[3] & 0x3F);
    return 4;
  }
  return 0;
}

void blitGlyphBits(int x, int y, const uint8_t* bits, int gw, int gh, int rowBytes,
                   uint8_t color, uint8_t scale) {
  for (int row = 0; row < gh; ++row) {
    for (int col = 0; col < gw; ++col) {
      uint8_t b = pgm_read_byte(&bits[row * rowBytes + (col >> 3)]);
      if (!(b & (0x80 >> (col & 7)))) continue;
      for (int sy = 0; sy < scale; ++sy)
        for (int sx = 0; sx < scale; ++sx)
          drawPixel(x + col * scale + sx, y + row * scale + sy, color);
    }
  }
}

void drawText(int x, int y, const char* s, uint8_t color, uint8_t scale) {
  if (!s) return;
  if (scale < 1) scale = 1;
  int cx = x;
  const char* p = s;
  while (*p) {
    uint32_t code = 0;
    int n = decodeUtf8(p, &code);
    if (n <= 0) {
      ++p;
      continue;
    }
    p += n;

    if (code >= FONT12X24_FIRST && code <= FONT12X24_LAST) {
      const uint8_t* glyph =
          &kFont12x24[(code - FONT12X24_FIRST) * FONT12X24_BYTES_PER_GLYPH];
      blitGlyphBits(cx, y, glyph, 12, 24, 2, color, scale);
      cx += 12 * scale;
      continue;
    }

    if (code <= 0xFFFF) {
      const FontCjkGlyph* g = findCjkGlyph((uint16_t)code);
      if (g) {
        blitGlyphBits(cx, y, g->bits, FONT_CJK_W, FONT_CJK_H, (FONT_CJK_W + 7) / 8, color,
                      scale);
        cx += FONT_CJK_W * scale;
        continue;
      }
    }
    // 缺字：留空位
    cx += FONT_CJK_W * scale;
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
  if (isLandscape()) {
    flushLandscape(rowBuf);
  } else {
    flushPortrait(rowBuf);
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

void setBusyPollHook(BusyPollHook fn) {
  g_busyPollHook = fn;
}

}  // namespace epd
