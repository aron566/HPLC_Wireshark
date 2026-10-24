#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""跨协议帧类型对照表: 四协议 (南网BPLC/HDC1.0/HDC2.0/南网SPLC) 横向对照。"""
import os
import re
import sys

sys.path.insert(0, r"D:/code/gitlab/HPLC_HRF_GW/monitor/BPLC_STA_QtMonitor/tools")
import gen_frame_catalog as G

R = G.R
PROTO = [
    ("南网BPLC", f"{R}/nw/BPLC"),
    ("国网HDC1.0", f"{R}/gw/HDC1.0"),
    ("国网HDC2.0", f"{R}/gw/HDC2.0"),
    ("南网SPLC", f"{R}/nw/SPLC"),
]
# 只取这些类别的枚举表做对照
CAT = [
    ("管理消息类型", re.compile(r"管理消息类型")),
    ("帧类型", re.compile(r"^帧类型$|帧类型域|^帧头类型$")),
    ("MSDU类型", re.compile(r"MSDU\s*类型")),
    ("信息单元类型", re.compile(r"信息单元.*类型")),
    ("信标类型", re.compile(r"信标类型")),
    ("链路类型值", re.compile(r"链路类型值")),
    ("代理类型", re.compile(r"^代理类型$")),
    ("设备类型", re.compile(r"设备类型字段")),
    ("MAC地址类型", re.compile(r"MAC\s*地址类型字段")),
    ("模块类型", re.compile(r"模块类型值")),
    ("信道信息组合类型", re.compile(r"信道信息组合类型取值")),
    ("位图版信道信息组合类型", re.compile(r"位图版信道信息组合类型")),
]


def norm(s):
    s = re.sub(r"[（(].*?[)）]", "", s)
    s = re.sub(r"(报文|信息|帧|类型|格式|字段|取值|定义|说明|组合)+$", "", s)
    return re.sub(r"[\s　]+", "", s).strip()


data = {}
for tag, d in PROTO:
    p, name = G.main_md(d)
    lines, tables = G.parse_doc(p)
    buckets = {}
    for num, ts in tables.items():
        for t in ts:
            if not G.is_enum(t):
                continue
            for cat, rx in CAT:
                if rx.search(t["title"]):
                    buckets.setdefault(cat, []).append((num, t))
                    break
    data[tag] = buckets
    print(f"{tag}: " + ", ".join(f"{k}×{len(v)}" for k, v in buckets.items()))

# 收集语义键
KEYS = {}
for tag, buckets in data.items():
    for cat, items in buckets.items():
        for num, t in items:
            for r in t["rows"][1:]:
                if not r or not r[0].strip():
                    continue
                k = norm(r[0])
                if not k or len(k) > 14:
                    continue
                val = r[1].strip() if len(r) > 1 else ""
                KEYS.setdefault((cat, k), {}).setdefault(tag, []).append(
                    (f"表{num}", val, t["sec"][:22]))

out = ["# 跨协议帧类型对照表", "",
       "> 自动生成：`tools/gen_frame_crossref.py`（依赖 `tools/gen_frame_catalog.py` 的解析器）。",
       "> 语义名已归一（去括号/去「报文·信息·格式·类型」后缀）。**同名语义在不同协议下的表号/取值不一定相同** ——",
       "> 按**语义**查，不要按表号查。`章节` 列给出归属，用于消歧。", ""]
for cat, _ in CAT:
    rows = {k: v for (c, k), v in KEYS.items() if c == cat}
    if not rows:
        continue
    out.append(f"\n---\n\n## {cat}\n")
    out.append("| 语义 | " + " | ".join(t for t, _ in PROTO) + " |")
    out.append("|---" * (len(PROTO) + 1) + "|")
    for k in sorted(rows):
        cells = []
        for t, _ in PROTO:
            hit = rows[k].get(t)
            if not hit:
                cells.append("—")
            else:
                tab, val, sec = hit[0]
                cells.append(f"{tab}<br>{val[:24]}<br>`{sec}`")
        out.append(f"| **{k}** | " + " | ".join(cells) + " |")
    out.append("")

dest = r"D:/code/gitlab/HPLC_HRF_GW/monitor/BPLC_STA_QtMonitor/docs/TEST_FRAME_CROSSREF.md"
open(dest, "w", encoding="utf-8", newline="\n").write("\n".join(out))
print(f"\n已生成 {dest}  {os.path.getsize(dest):,} B")


if __name__ == "__main__":
    main()
