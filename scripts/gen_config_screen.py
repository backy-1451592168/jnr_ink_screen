#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# 运行命令
# cd "/Users/sujunhao/project/私人项目/纪念日小程序全盏/jnr_ink_screen"
# ~/.platformio/penv/bin/python scripts/gen_config_screen.py

"""生成墨屏「设备配网」画面资源。

布局参考 docs/UI设计稿/单片机/ai_studio_code.html（400×800 等比放大到 480×800）。
字体：细体黑体优先（Noto Sans SC Light / 思源黑体 Light / 华文细黑），全页统一字重，用颜色区分层级。
渲染：480×800 原生像素直接绘制 → 六色最近邻量化（与 InkTime 一致，不做 2×+LANCZOS，避免灰阶边缘发糊）

用法：~/.platformio/penv/bin/python scripts/gen_config_screen.py
"""

import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCREEN_H = os.path.join(ROOT, "lib", "wifi_setup", "ui_config_screen.h")
FONT_H = os.path.join(ROOT, "lib", "epd", "font12x24.h")
PREVIEW = os.path.join(ROOT, "docs", "UI设计稿", "单片机", "预览效果图", "配网页面-墨屏预览.png")
QR_PATH = os.path.join(ROOT, "docs", "UI设计稿", "单片机", "配网二维码.png")
ICON_DIR = os.path.join(ROOT, "docs", "UI设计稿", "单片机", "素材")
ICON_WIFI = os.path.join(ICON_DIR, "wifi.png")
ICON_MAC = os.path.join(ICON_DIR, "路由器.png")

W, H = 480, 800
# 细体黑体：笔画粗细均匀（与 ai_studio_code.html 系统 UI 字重接近）
FONT_CANDIDATES = [
    # ("/Users/sujunhao/project/私人项目/纪念日小程序全盏/jnr_ink_screen/docs/字体库/点阵/ark-pixel-12px-monospaced-zh_cn.ttf", 0),
    # ("/Users/sujunhao/project/私人项目/纪念日小程序全盏/jnr_ink_screen/docs/字体库/点阵/ark-pixel-inherited-12px-monospaced.ttf", 0),
    # ("/Users/sujunhao/project/私人项目/纪念日小程序全盏/jnr_ink_screen/docs/字体库/仿宋_GB2312/仿宋_GB2312.ttf", 0),
    ("/Users/sujunhao/project/私人项目/纪念日小程序全盏/jnr_ink_screen/docs/字体库/windows XP 宋体/simsun.ttc", 0),
    ("/Users/sujunhao/project/私人项目/纪念日小程序全盏/jnr_ink_screen/docs/字体库/SanJiSongHeiTi-Xi/SanJiSongHeiTi-Xi-2.ttf", 0),
    ("/System/Library/Fonts/STHeiti Light.ttc", 0),
    ("/System/Library/Fonts/Hiragino Sans GB.ttc", 0),
]
MAC_FONT = "/System/Library/Fonts/Menlo.ttc"
MAC_FONT_INDEX = 0

BLACK, WHITE, YELLOW, RED, BLUE, GREEN = 0x00, 0x01, 0x02, 0x03, 0x05, 0x06
PALETTE = {
    BLACK: (0, 0, 0),
    WHITE: (255, 255, 255),
    YELLOW: (255, 255, 0),
    RED: (255, 0, 0),
    BLUE: (0, 0, 255),
    GREEN: (0, 255, 0),
}
RGB_BLACK = PALETTE[BLACK]
RGB_WHITE = PALETTE[WHITE]
RGB_YELLOW = PALETTE[YELLOW]
RGB_RED = PALETTE[RED]
RGB_BLUE = PALETTE[BLUE]
RGB_GREEN = PALETTE[GREEN]

C_TITLE = RGB_BLUE
C_ACCENT = RGB_BLUE
C_GREEN = RGB_GREEN
C_ORANGE = RGB_YELLOW
C_RED = RGB_RED
C_GRAY = RGB_BLACK
C_BODY = RGB_BLACK

PALETTE_RGB = [
    (0, 0, 0),
    (255, 255, 255),
    (255, 255, 0),
    (255, 0, 0),
    (0, 0, 255),
    (0, 255, 0),
]
PALETTE_IDX = [BLACK, WHITE, YELLOW, RED, BLUE, GREEN]

# 480 = 400 × 1.2；参照 ai_studio_code.html
LY = {
    "M": 26,
    "hdr_main_y": 10,
    "qc_top": 44,
    "qc_bot": 248,
    "qr_y": 50,
    "qsz": 168,
    "cap_y": 226,
    "hdr_info_y": 264,
    "info_top": 294,
    "info_bot": 416,
    "info_div_y": 354,
    "info_pad": 19,
    "info_icon_x": 60,
    "info_wifi_cy": 324,
    "info_mac_cy": 384,
    "lbl_x": 89,
    "lbl1_y": 308,
    "val1_y": 328,
    "lbl2_y": 368,
    "mac_y": 388,
    "hdr_steps_y": 430,
    "steps_top": 460,
    "steps_bot": 768,
    "steps_ty": 476,
    "steps_item_h": 74,
    "steps_desc_dy": 32,
    "steps_title_sz": 18,
    "steps_desc_sz": 16,
    "footer_y": 778,
    "icon_box": 30,
    "icon_gfx": 20,
    "icon_r": 8,
    "step_r": 14,
}


def rrect(d, box, r, fill=None, outline=None, width=1):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def icon_tile(d, cx, cy, fg):
    """圆角底框（对齐 ai_studio_code.html .icon，480 下略缩小）。"""
    half = LY["icon_box"] // 2
    rrect(d, (cx - half, cy - half, cx + half, cy + half), LY["icon_r"],
          fill=RGB_WHITE, outline=fg, width=1)


def paste_icon(img, cx, cy, path):
    """RGBA 素材居中叠到圆角底（先裁切透明边，避免图形偏左/偏上）。"""
    half = LY["icon_box"] // 2
    x0, y0 = cx - half, cy - half
    box = LY["icon_box"]
    gfx = LY["icon_gfx"]
    icon = Image.open(path).convert("RGBA")
    bbox = icon.split()[-1].getbbox()
    if bbox:
        icon = icon.crop(bbox)
    icon = icon.resize((gfx, gfx), Image.NEAREST)
    layer = Image.new("RGBA", (box, box), (0, 0, 0, 0))
    off = (box - gfx) // 2
    layer.paste(icon, (off, off), icon)
    base = img.crop((x0, y0, x0 + box, y0 + box)).convert("RGBA")
    img.paste(Image.alpha_composite(base, layer).convert("RGB"), (x0, y0))


def ic_wifi(img, d, cx, cy, fg=C_ACCENT):
    icon_tile(d, cx, cy, fg)
    paste_icon(img, cx, cy, ICON_WIFI)


def ic_chip(img, d, cx, cy, fg=C_GREEN):
    icon_tile(d, cx, cy, fg)
    paste_icon(img, cx, cy, ICON_MAC)


def draw_step_connectors(d, n, item_h, ty, step_cx, step_r):
    """步骤间竖线：只画圆点之间的空隙（html ::after，e-ink 用 1px 黑线）。"""
    for i in range(n - 1):
        y1 = ty + i * item_h + item_h // 2 + step_r + 1
        y2 = ty + (i + 1) * item_h + item_h // 2 - step_r - 1
        if y2 > y1:
            d.line((step_cx, y1, step_cx, y2), fill=RGB_BLACK, width=1)


def resolve_font():
    for path, idx in FONT_CANDIDATES:
        if not os.path.isfile(path):
            continue
        try:
            ImageFont.truetype(path, 24, index=idx)
            return path, idx
        except OSError:
            continue
    raise FileNotFoundError(
        "未找到细体黑体，请放置 NotoSansSC-Light.otf 到 resource/fonts/")


FONT_PATH, FONT_INDEX = resolve_font()


def tfont(size, font_path=None, font_index=None):
    return ImageFont.truetype(
        font_path or FONT_PATH, size,
        index=FONT_INDEX if font_index is None else font_index)


def _text_width(text, size):
    return ImageDraw.Draw(Image.new("L", (1, 1))).textlength(
        text, font=tfont(size))


def render_frame(include_mac=False):
    """480×800 原生像素绘制（与 InkTime render_daily_photo 同策略）。"""
    img = Image.new("RGB", (W, H), RGB_WHITE)
    d = ImageDraw.Draw(img)

    M = LY["M"]
    lw = 1

    rrect(d, (M, LY["qc_top"], W - M, LY["qc_bot"]), 16,
          fill=RGB_WHITE, outline=RGB_BLACK, width=lw)
    qr_x = (W - LY["qsz"]) // 2
    img.paste(prepare_qr(LY["qsz"]), (qr_x, LY["qr_y"]))

    rrect(d, (M, LY["info_top"], W - M, LY["info_bot"]), 14,
          fill=RGB_WHITE, outline=RGB_BLACK, width=lw)
    ic_wifi(img, d, LY["info_icon_x"], LY["info_wifi_cy"])
    d.line((M + 14, LY["info_div_y"], W - M - 14, LY["info_div_y"]),
           fill=RGB_BLACK, width=lw)
    ic_chip(img, d, LY["info_icon_x"], LY["info_mac_cy"])

    rrect(d, (M, LY["steps_top"], W - M, LY["steps_bot"]), 14,
          fill=RGB_WHITE, outline=RGB_BLACK, width=lw)
    step_cx = LY["M"] + 34
    item_h = LY["steps_item_h"]
    draw_step_connectors(d, 4, item_h, LY["steps_ty"], step_cx, LY["step_r"])

    def font(size, fp=None, fi=None):
        return tfont(size, fp, fi)

    def put(x, y, text, size, fill, fp=None, fi=None):
        d.text((x, y), text, font=font(size, fp, fi), fill=fill)

    def put_c(cx, y, text, size, fill, fp=None, fi=None):
        f = font(size, fp, fi)
        tw = d.textlength(text, font=f)
        d.text((cx - tw / 2, y), text, font=f, fill=fill)

    def sec_hdr(y, title, size):
        cy = y + size // 2
        f = font(size)
        tw = d.textlength(title, font=f)
        text_l = W // 2 - tw / 2
        text_r = W // 2 + tw / 2
        pad = 10
        d.line((M, cy, text_l - pad, cy), fill=RGB_BLACK, width=lw)
        d.line((text_r + pad, cy, W - M, cy), fill=RGB_BLACK, width=lw)
        put_c(W // 2, y, title, size, C_TITLE)

    cx, X = W // 2, LY["lbl_x"]
    sec_hdr(LY["hdr_main_y"], "设备配网", 18)  # SimSun 仅 12/14/16/18 有点阵，20px 走矢量抗锯齿会缺笔划
    put_c(cx, LY["cap_y"], "扫描二维码进行设备配网", 16, C_GRAY)

    sec_hdr(LY["hdr_info_y"], "设备信息", 18)
    put(X, LY["lbl1_y"], "Wi-Fi 名称 (SSID)", 16, C_GRAY)
    put(X, LY["val1_y"], "DayIJoy-心选日", 18, C_ACCENT)
    put(X, LY["lbl2_y"], "设备 MAC 地址", 16, C_GRAY)
    mac_x, mac_y = X, LY["mac_y"]
    if include_mac:
        put(X, mac_y, "E0:72:A1:F6:15:SU", 18, C_GREEN, MAC_FONT, MAC_FONT_INDEX)

    sec_hdr(LY["hdr_steps_y"], "使用事项", 18)
    steps = [
        ("连接设备 Wi-Fi", "在列表中选择 DayIJoy-心选日"),
        ("打开配置页面", "自动打开或手动访问 192.168.8.1"),
        ("配置网络", "选择路由器名称并输入密码完成配网"),
        ("注意事项", "保持通电靠近；成功后设备将自动重启"),
    ]
    text_x = LY["M"] + 56
    badge_cols = [C_ACCENT, C_GREEN, C_ORANGE, C_RED]
    for i, (title, desc) in enumerate(steps):
        y = LY["steps_ty"] + i * LY["steps_item_h"]
        bcy = y + LY["steps_item_h"] // 2
        br = LY["step_r"]
        d.ellipse((step_cx - br, bcy - br, step_cx + br, bcy + br),
                  fill=badge_cols[i])
        put_c(step_cx, bcy - 8, str(i + 1), 14, RGB_WHITE)
        put(text_x, y, title, LY["steps_title_sz"], RGB_BLACK)
        put(text_x, y + LY["steps_desc_dy"], desc, LY["steps_desc_sz"], C_BODY)

    put_c(cx, LY["footer_y"], "DayIJoy · 心选日", 16, C_ACCENT)
    return img


def finalize(img):
    return quantize_nearest(img)


def render():
    return finalize(render_frame(include_mac=False)), (LY["lbl_x"], LY["mac_y"])


def prepare_qr(target_px):
    raw = Image.open(QR_PATH).convert("L")
    bw = raw.point(lambda p: 0 if p < 160 else 255, mode="1")
    pad = max(4, min(bw.width, bw.height) // 24)
    bordered = Image.new("1", (bw.width + 2 * pad, bw.height + 2 * pad), 1)
    bordered.paste(bw, (pad, pad))
    margin = 2
    ratio = min((target_px - margin) / bordered.width,
                (target_px - margin) / bordered.height)
    nw = max(1, int(bordered.width * ratio))
    nh = max(1, int(bordered.height * ratio))
    scaled = bordered.resize((nw, nh), Image.NEAREST)
    out = Image.new("RGB", (target_px, target_px), RGB_WHITE)
    ox = (target_px - nw) // 2
    oy = (target_px - nh) // 2
    spx, opx = scaled.load(), out.load()
    for y in range(nh):
        for x in range(nw):
            if spx[x, y] == 0:
                opx[ox + x, oy + y] = (0, 0, 0)
    return out


def _nearest_palette_index(r, g, b):
    best_i, best_d = 0, float("inf")
    for i, (pr, pg, pb) in enumerate(PALETTE_RGB):
        dist = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if dist < best_d:
            best_d, best_i = dist, i
    return PALETTE_IDX[best_i]


def quantize_nearest(img):
    """UI 纯色画面用最近邻，不 Floyd-Steinberg（抖动会让文字边缘发糊）。"""
    flat = []
    for rgb in PALETTE_RGB:
        flat.extend(rgb)
    flat.extend([0] * (768 - len(flat)))
    pal = Image.new("P", (1, 1))
    pal.putpalette(flat)
    return img.convert("RGB").quantize(
        palette=pal, dither=Image.Dither.NONE).convert("RGB")


def nearest_index(rgb):
    for idx, prgb in PALETTE.items():
        if rgb == prgb:
            return idx
    return _nearest_palette_index(*rgb[:3])


def quantize_pack(img):
    px = img.load()
    stride = W // 2
    buf = bytearray(stride * H)
    for y in range(H):
        for x in range(0, W, 2):
            hi = nearest_index(px[x, y])
            lo = nearest_index(px[x + 1, y])
            buf[y * stride + x // 2] = (hi << 4) | lo
    return bytes(buf)


def rle(buf):
    out = bytearray()
    i, n = 0, len(buf)
    while i < n:
        v = buf[i]
        run = 1
        while i + run < n and buf[i + run] == v and run < 255:
            run += 1
        out.append(run)
        out.append(v)
        i += run
    return bytes(out)


def emit_screen_header(rle_buf, mac_xy):
    mac_x, mac_y = mac_xy
    lines = [
        "// 自动生成，请勿手改。源：scripts/gen_config_screen.py",
        "// 墨屏「设备配网」静态画面：竖屏 480x800，4bit/像素，六色，RLE 压缩。",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "#define CFG_SCREEN_W 480",
        "#define CFG_SCREEN_H 800",
        f"#define CFG_MAC_X {mac_x}",
        f"#define CFG_MAC_Y {mac_y}",
        "#define CFG_MAC_SCALE 1",
        "#define CFG_MAC_COLOR 0x06  // 绿",
        "",
        f"static const uint32_t kCfgScreenRleLen = {len(rle_buf)};",
        "static const uint8_t kCfgScreenRle[] PROGMEM = {",
    ]
    for i in range(0, len(rle_buf), 20):
        lines.append("  " + ",".join(str(b) for b in rle_buf[i:i + 20]) + ",")
    lines += ["};", ""]
    with open(SCREEN_H, "w") as f:
        f.write("\n".join(lines))


def glyph_rows(ch, cell_w=12, cell_h=24):
    gs = 4
    f = tfont(18 * gs, MAC_FONT, MAC_FONT_INDEX)
    hi = Image.new("L", (cell_w * gs, cell_h * gs), 0)
    ImageDraw.Draw(hi).text((0, gs // 2), ch, font=f, fill=255)
    lo = hi.resize((cell_w, cell_h), Image.Resampling.LANCZOS)
    px = lo.load()
    rows = []
    for y in range(cell_h):
        b0 = b1 = 0
        for x in range(cell_w):
            if px[x, y] >= 100:
                if x < 8:
                    b0 |= (1 << (7 - x))
                else:
                    b1 |= (1 << (7 - (x - 8)))
        rows.extend([b0, b1])
    return rows


def emit_font_header():
    glyphs = [glyph_rows(chr(code)) for code in range(0x20, 0x7F)]
    lines = [
        "// 自动生成，请勿手改。源：scripts/gen_config_screen.py",
        "// 12x24 ASCII 点阵字体（0x20-0x7E），每字符 24 行 x 2 字节/行。",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "#define FONT12X24_FIRST 0x20",
        "#define FONT12X24_LAST 0x7E",
        "#define FONT12X24_BYTES_PER_GLYPH 48",
        "static const uint8_t kFont12x24[] PROGMEM = {",
    ]
    for code, rows in zip(range(0x20, 0x7F), glyphs):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in rows) +
                     f", // 0x{code:02X} '{chr(code)}'")
    lines += ["};", ""]
    with open(FONT_H, "w") as f:
        f.write("\n".join(lines))


def main():
    print(f"字体: {FONT_PATH} (index={FONT_INDEX})", flush=True)
    print("渲染画面...", flush=True)
    img, mac_xy = render()
    preview = finalize(render_frame(include_mac=True))
    print("量化打包...", flush=True)
    packed = quantize_pack(img)
    preview.save(PREVIEW)
    print("导出头文件...", flush=True)
    rle_buf = rle(packed)
    emit_screen_header(rle_buf, mac_xy)
    emit_font_header()
    print(f"预览图: {PREVIEW}")
    print(f"点阵原始 {len(packed)} 字节 -> RLE {len(rle_buf)} 字节 "
          f"({len(rle_buf) * 100 // len(packed)}%)")
    print(f"MAC 绘制坐标(竖屏): {mac_xy}")


if __name__ == "__main__":
    main()
