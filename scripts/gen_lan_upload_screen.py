#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成「局域网传图地址」墨屏静态画面（横屏 800×480）。

中文 + 固定文案用 Noto/Menlo 按配网页同款 1-bit 栅格化。
完整 URL 由固件用 ASCII 字库叠画（坐标见 LAN_URL_*）。

用法：
  cd jnr_ink_screen
  ~/.platformio/penv/bin/python scripts/gen_lan_upload_screen.py
"""

from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
PROJECT = ROOT.parent

NOTO = PROJECT / "docs" / "字体库" / "思源黑体-谷歌Noto简体中文无衬线" / "NotoSansSC-Regular.otf"
MENLO = "/System/Library/Fonts/Menlo.ttc"
OUT_H = ROOT / "lib" / "wifi_setup" / "ui_lan_upload_screen.h"
PREVIEW = ROOT / "docs" / "lan_upload_preview.png"

W, H = 800, 480
BLACK, WHITE, YELLOW = 0x00, 0x01, 0x02
RGB_BLACK = (0, 0, 0)
RGB_YELLOW = (255, 255, 0)
PALETTE = {BLACK: RGB_BLACK, WHITE: (255, 255, 255), YELLOW: RGB_YELLOW}
PALETTE_RGB = [RGB_BLACK, (255, 255, 255), RGB_YELLOW, (255, 0, 0), (0, 0, 255), (0, 255, 0)]
PALETTE_IDX = [BLACK, WHITE, YELLOW, 0x03, 0x05, 0x06]

URL_X, URL_Y = 48, 188
URL_SCALE = 2


def font_noto(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(NOTO), size)


def font_mono(size: int) -> ImageFont.FreeTypeFont:
    if os.path.isfile(MENLO):
        return ImageFont.truetype(MENLO, size, index=0)
    return font_noto(size)


def paste_text(
    img: Image.Image,
    x: int,
    y: int,
    text: str,
    size: int,
    fill: tuple,
    *,
    mono: bool = False,
) -> None:
    """灰度抗锯齿绘制 → 低阈值二值化，细笔不易丢。"""
    if not text:
        return
    f = font_mono(size) if mono else font_noto(size)
    bbox = f.getbbox(text)
    tw, th = max(1, bbox[2] - bbox[0]), max(1, bbox[3] - bbox[1])
    pad = 2
    gray = Image.new("L", (tw + pad * 2, th + pad * 2), 0)
    ImageDraw.Draw(gray).text(
        (pad - bbox[0], pad - bbox[1]), text, font=f, fill=255
    )
    # 阈值偏低：把抗锯齿灰边也收成墨，减少断笔
    bw = gray.point(lambda p: 255 if p >= 64 else 0, mode="1")
    img.paste(Image.new("RGB", bw.size, fill), (x - pad, y - pad), bw)


def render(include_sample_url: bool = False) -> Image.Image:
    img = Image.new("RGB", (W, H), RGB_YELLOW)
    paste_text(img, 48, 44, "局域网传图", 48, RGB_BLACK)
    paste_text(img, 48, 118, "请用浏览器打开", 30, RGB_BLACK)
    if include_sample_url:
        paste_text(
            img, URL_X, URL_Y, "http://192.168.0.116/upload", 28, RGB_BLACK, mono=True
        )
    paste_text(img, 48, 280, "与相框同一 WiFi", 28, RGB_BLACK)
    paste_text(img, 48, 348, "长按执行键退出", 28, RGB_BLACK)
    return img


def nearest_index(rgb):
    for idx, prgb in PALETTE.items():
        if rgb == prgb:
            return idx
    r, g, b = rgb[:3]
    best_i, best_d = BLACK, 1e18
    for i, (pr, pg, pb) in enumerate(PALETTE_RGB):
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < best_d:
            best_d, best_i = d, PALETTE_IDX[i]
    return best_i


def quantize_pack(img: Image.Image) -> bytes:
    px = img.load()
    stride = W // 2
    buf = bytearray(stride * H)
    for y in range(H):
        for x in range(0, W, 2):
            hi = nearest_index(px[x, y])
            lo = nearest_index(px[x + 1, y])
            buf[y * stride + x // 2] = (hi << 4) | lo
    return bytes(buf)


def rle(buf: bytes) -> bytes:
    out = bytearray()
    i, n = 0, len(buf)
    while i < n:
        v = buf[i]
        j = i + 1
        while j < n and buf[j] == v and (j - i) < 255:
            j += 1
        out.append(j - i)
        out.append(v)
        i = j
    return bytes(out)


def emit_header(rle_buf: bytes) -> None:
    lines = [
        "// 自动生成，请勿手改。源：scripts/gen_lan_upload_screen.py",
        "// 局域网传图地址页：横屏 800×480，中文预渲染 RLE；URL 运行时叠画。",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        f"#define LAN_UPLOAD_SCREEN_W {W}",
        f"#define LAN_UPLOAD_SCREEN_H {H}",
        f"#define LAN_URL_X {URL_X}",
        f"#define LAN_URL_Y {URL_Y}",
        f"#define LAN_URL_SCALE {URL_SCALE}",
        "#define LAN_URL_COLOR 0x00  // 黑",
        "",
        f"static const uint32_t kLanUploadScreenRleLen = {len(rle_buf)};",
        "static const uint8_t kLanUploadScreenRle[] PROGMEM = {",
    ]
    for i in range(0, len(rle_buf), 20):
        chunk = rle_buf[i : i + 20]
        lines.append("  " + ",".join(str(b) for b in chunk) + ",")
    lines += ["};", ""]
    OUT_H.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    if not NOTO.is_file():
        raise SystemExit(f"缺少字体：{NOTO}")

    # 固件用：无 URL（黄底留白）
    img = render(include_sample_url=False)
    packed = quantize_pack(img)
    rle_buf = rle(packed)
    emit_header(rle_buf)

    # 预览用：带示例 URL
    preview = render(include_sample_url=True)
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    preview.save(PREVIEW)

    print(f"预览 → {PREVIEW}")
    print(f"头文件 → {OUT_H}（RLE {len(rle_buf)} / raw {len(packed)}）")
    print(f"URL 叠画: ({URL_X},{URL_Y}) ×{URL_SCALE}")


if __name__ == "__main__":
    main()
