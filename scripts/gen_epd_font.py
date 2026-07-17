#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成墨屏 ASCII 12×24 点阵（URL / MAC 等叠画用）。

与配网页相同：FreeType → 1-bit（无灰边），基线对齐，格内水平居中。
中文请用 scripts/gen_lan_upload_screen.py 整页预渲染，不要用小点阵放大。

用法：
  cd jnr_ink_screen
  ~/.platformio/penv/bin/python scripts/gen_epd_font.py
"""

from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PROJECT = ROOT.parent
PREVIEW = ROOT / "docs" / "font_preview.png"

NOTO = PROJECT / "docs" / "字体库" / "思源黑体-谷歌Noto简体中文无衬线" / "NotoSansSC-Regular.otf"
ASCII_FONT_CANDIDATES = [
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    str(NOTO),
]

OUT_ASCII = ROOT / "lib" / "epd" / "font12x24.h"
# 保留空中文表，避免旧固件引用断裂；地址页中文已改 RLE
OUT_CJK = ROOT / "lib" / "epd" / "font_cjk24.h"

ASCII_W, ASCII_H = 12, 24
ASCII_PX = 15
ASCII_BASELINE = 19


def load_font(path: str, size: int, index: int = 0) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(path, size, index=index)


def resolve_ascii_font() -> tuple[str, int]:
    for p in ASCII_FONT_CANDIDATES:
        if not os.path.isfile(p):
            continue
        try:
            load_font(p, ASCII_PX, 0)
            return p, 0
        except OSError:
            continue
    raise FileNotFoundError("未找到可用的 ASCII 字体")


def pack_rows(mono: Image.Image, cell_w: int, cell_h: int) -> list[int]:
    px = mono.load()
    bytes_per_row = (cell_w + 7) // 8
    out: list[int] = []
    for y in range(cell_h):
        for bi in range(bytes_per_row):
            b = 0
            for bit in range(8):
                x = bi * 8 + bit
                if x < cell_w and px[x, y]:
                    b |= 0x80 >> bit
            out.append(b)
    return out


def render_glyph(ch: str, font_path: str, font_index: int) -> Image.Image:
    """灰度抗锯齿 → 低阈值，基线对齐 + 水平居中。"""
    cell = Image.new("1", (ASCII_W, ASCII_H), 0)
    if ch == " ":
        return cell
    ss = 3
    big = Image.new("L", (ASCII_W * ss, ASCII_H * ss), 0)
    font_big = load_font(font_path, ASCII_PX * ss, font_index)
    bbox = font_big.getbbox(ch)
    gw = max(1, bbox[2] - bbox[0])
    x = (ASCII_W * ss - gw) // 2 - bbox[0]
    ImageDraw.Draw(big).text(
        (x, ASCII_BASELINE * ss), ch, font=font_big, fill=255, anchor="ls"
    )
    bw = big.point(lambda p: 255 if p >= 64 else 0, mode="1")
    sp, dp = bw.load(), cell.load()
    for cy in range(ASCII_H):
        for cx in range(ASCII_W):
            ink = False
            for yy in range(cy * ss, cy * ss + ss):
                for xx in range(cx * ss, cx * ss + ss):
                    if sp[xx, yy]:
                        ink = True
                        break
                if ink:
                    break
            if ink:
                dp[cx, cy] = 1
    return cell


def emit_ascii(font_path: str, font_index: int) -> dict[str, Image.Image]:
    glyphs: dict[str, Image.Image] = {}
    lines = [
        "// 自动生成，请勿手改。源：scripts/gen_epd_font.py",
        f"// 12x24 ASCII，字号 {ASCII_PX}，超采样+低阈值，基线对齐。",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "#define FONT12X24_FIRST 0x20",
        "#define FONT12X24_LAST 0x7E",
        "#define FONT12X24_BYTES_PER_GLYPH 48",
        "static const uint8_t kFont12x24[] PROGMEM = {",
    ]
    for code in range(0x20, 0x7F):
        ch = chr(code)
        img = render_glyph(ch, font_path, font_index)
        glyphs[ch] = img
        rows = pack_rows(img, ASCII_W, ASCII_H)
        label = ch if ch != "'" else "\\'"
        lines.append(
            "  " + ",".join(f"0x{b:02X}" for b in rows) + f", // 0x{code:02X} '{label}'"
        )
    lines += ["};", ""]
    OUT_ASCII.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return glyphs


def emit_empty_cjk() -> None:
    lines = [
        "// 自动生成，请勿手改。源：scripts/gen_epd_font.py",
        "// 中文请用 gen_lan_upload_screen.py 整页 RLE；此表仅占位。",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "#define FONT_CJK_W 24",
        "#define FONT_CJK_H 24",
        "#define FONT_CJK_BYTES_PER_GLYPH 72",
        "#define FONT_CJK_COUNT 0",
        "",
        "struct FontCjkGlyph {",
        "  uint16_t code;",
        "  uint8_t bits[72];",
        "} __attribute__((packed));",
        "",
        "static const FontCjkGlyph kFontCjk[1] PROGMEM = {{0, {0}}};",
        "",
    ]
    OUT_CJK.write_text("\n".join(lines), encoding="utf-8")


def save_preview(glyphs: dict[str, Image.Image]) -> None:
    text = "http://192.168.0.116/upload"
    scale = 2
    w = 40 + len(text) * ASCII_W * scale + 40
    h = 80
    canvas = Image.new("RGB", (w, h), (255, 196, 0))
    x = 20
    for ch in text:
        g = glyphs.get(ch)
        if not g:
            x += ASCII_W * scale
            continue
        ink = g.resize((ASCII_W * scale, ASCII_H * scale), Image.Resampling.NEAREST)
        canvas.paste(Image.new("RGB", ink.size, (0, 0, 0)), (x, 20), ink)
        x += ASCII_W * scale
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(PREVIEW)


def main() -> None:
    path, index = resolve_ascii_font()
    glyphs = emit_ascii(path, index)
    emit_empty_cjk()
    save_preview(glyphs)
    dot = glyphs["."]
    ink_rows = [
        i for i in range(ASCII_H) if any(dot.getpixel((x, i)) for x in range(ASCII_W))
    ]
    print(f"ASCII → {OUT_ASCII}（{path}, px={ASCII_PX}）")
    print(f"CJK   → {OUT_CJK}（空表，中文走 RLE）")
    print(f"'.' 有墨行: {ink_rows}")
    print(f"预览 → {PREVIEW}")


if __name__ == "__main__":
    main()
