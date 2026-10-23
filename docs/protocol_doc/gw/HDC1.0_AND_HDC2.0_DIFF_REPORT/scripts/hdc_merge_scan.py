#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""② 粘连全量扫描(含 M4 标签格串值形态) + ⑥ D1 多口径可复现计数。"""
import json
import os
import re

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # <repo>/docs/protocol_doc/gw(由脚本位置推导,勿硬编码本机路径)
OUT = os.path.dirname(os.path.abspath(__file__))
DOCS = {
    "旧1": os.path.join(R, "HDC1.0", "双模通信互联互通技术规范_第4_2部分数据链路层通信协议_261023_152656.md"),
    "新2": os.path.join(R, "HDC2.0", "双模通信互联互通技术规范_第4_2部分数据链路层通信协议_261023_152707.md"),
}

M1 = re.compile(r"\d{1,3}\s+\d{3,}(?!\d)")
M2 = re.compile(r"^\d{1,3}\s+\d{1,3}\s+\d{1,3}$")
# M4: 标签格内串入下一格数值 —— "数值+单位+三位以上量化值" 或 "单位+量化值"
M4 = re.compile(r"dB\w*\d{3,}|\d+dB\w*\d{3,}")
# M5: 三段带横线拼接 (如 "4-70-7")
M5 = re.compile(r"^\d{1,3}-\d{1,3}-\d{1,3}$")
# M6: 数量+单位+数字 拼接 (如 "4 比特1")
M6 = re.compile(r"^\d{1,3}\s*\S{1,4}\d{1,3}$")
FIELD_WORDS = ["MAC 地址", "相线", "保留", "比特位", "字节号", "字段大小", "定义", "含义",
               "TEI", "代理 TEI", "层级", "角色", "告知总数量", "SNR", "RSSI"]


def cells_of(md):
    out = []
    for i, l in enumerate(md.split("\n")):
        for m in re.finditer(r"<td([^>]*)>(.*?)</td>", l, re.S):
            out.append((i + 1, re.sub(r"<[^>]+>", "", m.group(2)).strip()))
    return out


def d1(md, cells):
    trs = re.findall(r"<tr[^>]*>(.*?)</tr>", md, re.S)
    plain = re.sub(r"<[^>]+>", " ", md)
    return {
        "口径A_严格td_1to3位": {
            "连字符式": sum(1 for _, t in cells if re.fullmatch(r"\d{1,3}-\d{1,3}", t)),
            "空格式": sum(1 for _, t in cells if re.fullmatch(r"\d{1,3}\s+\d{1,3}", t)),
            "定义": "`<td>` 单元格全文匹配 ^\\d{1,3}-\\d{1,3}$ / ^\\d{1,3}\\s+\\d{1,3}$",
        },
        "口径B_宽松td_任意位数": {
            "连字符式": sum(1 for _, t in cells if re.fullmatch(r"\d+-\d+", t)),
            "空格式": sum(1 for _, t in cells if re.fullmatch(r"\d+\s+\d+", t)),
            "定义": "`<td>` 单元格全文匹配 ^\\d+-\\d+$ / ^\\d+\\s+\\d+$",
        },
        "口径C_tr行内任意位置": {
            "连字符式": sum(len(re.findall(r"(?<!\d)\d{1,3}-\d{1,3}(?!\d)", re.sub(r"<[^>]+>", " ", tr))) for tr in trs),
            "定义": "每个 `<tr>` 去标签后统计 (?<!\\d)\\d{1,3}-\\d{1,3}(?!\\d)",
        },
        "口径D_全文任意位置": {
            "连字符式": len(re.findall(r"(?<!\d)\d{1,3}-\d{1,3}(?!\d)", plain)),
            "定义": "全文去标签后统计 (?<!\\d)\\d{1,3}-\\d{1,3}(?!\\d)",
        },
        "td单元格总数": len(cells),
    }


res = {}
for tag, path in DOCS.items():
    md = open(path, encoding="utf-8").read()
    cells = cells_of(md)
    m1 = [(ln, t) for ln, t in cells if M1.search(t)]
    m2 = [(ln, t) for ln, t in cells if M2.match(t)]
    m4 = [(ln, t) for ln, t in cells if M4.search(t)]
    m5 = [(ln, t) for ln, t in cells if M5.match(t)]
    m6 = [(ln, t) for ln, t in cells if M6.match(t)]
    m3 = [(ln, t) for ln, t in cells if sum(1 for w in FIELD_WORDS if w in t) >= 2 and len(t) < 40]
    d = d1(md, cells)
    print("=" * 78)
    print(f"[{tag}]")
    print("=" * 78)
    print(f"  M1 数字串(数字 空格 +>=3位): {len(m1)}   例: {[t for _, t in m1[:5]]}")
    print(f"  M2 三段纯数字拼接       : {len(m2)}   例: {[t for _, t in m2[:5]]}")
    print(f"  M4 标签格串入下一格数值   : {len(m4)}   例: {[t for _, t in m4[:5]]}")
    print(f"  M5 三段带横线拼接        : {len(m5)}   例: {[t for _, t in m5[:5]]}")
    print(f"  M6 数量+单位+数字拼接    : {len(m6)}   例: {[t for _, t in m6[:5]]}")
    print(f"  M3 字段名粘连候选        : {len(m3)}")
    print("  D1 多口径:")
    for k, v in d.items():
        if k == "td单元格总数":
            print(f"      {k}: {v}")
        else:
            print(f"      {k}: 连字符式={v['连字符式']}" + (f" 空格式={v['空格式']}" if "空格式" in v else "") + f"   [{v['定义']}]")
    print()
    res[tag] = {"m1": m1, "m2": m2, "m4": m4, "m5": m5, "m6": m6, "m3": m3, "d1": d}

json.dump(res, open(os.path.join(OUT, "merge_scan.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("-> merge_scan.json")
