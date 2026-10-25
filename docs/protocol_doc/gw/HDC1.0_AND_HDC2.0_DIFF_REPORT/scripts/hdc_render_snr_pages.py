#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按 table_caption 定位 SNR/RSSI 映射表页码, 400dpi 渲染供像素核实。"""
import glob
import json
import os
import re
import tempfile

import fitz

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))  # <repo>/docs/protocol_doc(由脚本位置推导,勿硬编码本机路径)
OUT = os.path.join(tempfile.gettempdir(), "mineru_gw")  # 渲染产物输出(对应核实记录里的 %LOCALAPPDATA%/Temp/mineru_gw)

JOBS = [
    ("HDC1.0", os.path.join(R, "gw/HDC1.0", "双模通信互联互通技术规范 第4-2部分：数据链路层通信协议.pdf"),
     ["表 132", "表 134", "表 135"]),
    ("HDC2.0", os.path.join(R, "gw/HDC2.0", "双模通信互联互通技术规范_第4_2部分数据链路层通信协议.pdf"),
     ["表71", "表73", "表74", "表98"]),
]

for tag, pdf, want in JOBS:
    dirs = [x for x in glob.glob(os.path.join(OUT, f"{tag}_*")) if os.path.isdir(x)]
    cl = [f for f in glob.glob(os.path.join(dirs[0], "*_content_list.json")) if "v2" not in f][0]
    d = json.load(open(cl, encoding="utf-8"))
    doc = fitz.open(pdf)
    print("=" * 74)
    print(f"[{tag}]")
    print("=" * 74)
    for it in d:
        caps = it.get("table_caption") or []
        if not caps:
            continue
        cap = caps[0]
        for w in want:
            if cap.replace(" ", "").startswith(w.replace(" ", "")):
                pg = it.get("page_idx")
                fn = os.path.join(OUT, f"snr_{tag}_p{pg}.png")
                if not os.path.exists(fn):
                    doc[pg].get_pixmap(dpi=400).save(fn)
                # 打印该表首几行
                rows = re.findall(r"<tr[^>]*>(.*?)</tr>", it.get("table_body") or "", re.S)
                print(f"  【{cap}】page_idx={pg} -> {os.path.basename(fn)}")
                for k, r in enumerate(rows[:4]):
                    cells = [re.sub(r"<[^>]+>", "", c).strip() for c in re.findall(r"<td[^>]*>(.*?)</td>", r, re.S)]
                    print(f"      r{k}: {cells}")
    print()
    doc.close()
