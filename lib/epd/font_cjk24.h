// 自动生成，请勿手改。源：scripts/gen_epd_font.py
// 中文请用 gen_lan_upload_screen.py 整页 RLE；此表仅占位。
#pragma once
#include <Arduino.h>

#define FONT_CJK_W 24
#define FONT_CJK_H 24
#define FONT_CJK_BYTES_PER_GLYPH 72
#define FONT_CJK_COUNT 0

struct FontCjkGlyph {
  uint16_t code;
  uint8_t bits[72];
} __attribute__((packed));

static const FontCjkGlyph kFontCjk[1] PROGMEM = {{0, {0}}};
