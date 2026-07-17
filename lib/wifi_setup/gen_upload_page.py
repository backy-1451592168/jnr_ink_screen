#!/usr/bin/env python3
"""
把局域网传图页从「可读 HTML」转成固件可嵌入的 PROGMEM 头文件。

为什么需要：
  ESP32 固件通过 wifi_setup.cpp 里的 kUploadPage 提供 GET /upload 页面。
  直接在 .cpp 里改超长 HTML 很难维护，所以源文件是 upload_page.html，
  本脚本把它包进 ui_upload_page.h（R"UPHTML(...)" 原始字符串）。

用法（改完页面后执行一次，再编译烧录）：
  cd jnr_ink_screen/lib/wifi_setup
  python3 gen_upload_page.py

输入 / 输出（均在本脚本同目录）：
  读  upload_page.html   ← 请只改这个
  写  ui_upload_page.h   ← 自动生成，不要手改（下次运行会覆盖）

注意：
  - PlatformIO 构建不会自动跑本脚本，改 HTML 后必须手动执行。
  - HTML 正文里不能出现字面量 )UPHTML，否则会截断原始字符串。
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "upload_page.html"
OUT = HERE / "ui_upload_page.h"
DELIM = "UPHTML"

html = SRC.read_text(encoding="utf-8")
if f"){DELIM}" in html:
    raise SystemExit(
        f"错误：{SRC.name} 含有 ){DELIM}，会破坏 C++ 原始字符串。"
        f"请改掉该片段，或修改本脚本的 DELIM。"
    )

OUT.write_text(
    "// 由 gen_upload_page.py 从 upload_page.html 生成，勿手改。\n"
    "// 重新生成：python3 gen_upload_page.py\n"
    "#pragma once\n"
    "#include <Arduino.h>\n"
    f"static const char kUploadPage[] PROGMEM = R\"{DELIM}(\n"
    + html
    + f"\n){DELIM}\";\n",
    encoding="utf-8",
)
print(f"已生成 {OUT.name}（{OUT.stat().st_size} 字节）← {SRC.name}")
