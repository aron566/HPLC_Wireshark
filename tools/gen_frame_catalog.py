#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TEST_FRAME_CATALOG 生成器 v2 —— 修 4 条:
 1) 按 rowspan/colspan 展开表格网格 (关键)
 2) 修跨字节解析: 区分「字节号」与「比特位」, 不再把比特位当字节号
 3) 过滤非帧表 (STF/LTF 频域序列等)
 4) rowspan 拆行未合并处加 ⚠ 标记
"""
import os
import re

R = r"D:/code/gitlab/HPLC_HRF_GW/monitor/BPLC_STA_QtMonitor/docs/protocol_doc"
CAP = re.compile(r"^\s*(?:#{1,5}\s*)?(表|图)\s*(\d+)\s*(.*)$")
ROW = re.compile(r"<tr[^>]*>(.*?)</tr>", re.S)
CELLF = re.compile(r"<(t[dh])([^>]*)>(.*?)</\1>", re.S)
TBL = re.compile(r"<table[^>]*>(.*?)</table>", re.S)

DOCS = [
    ("南网BPLC(第2部分 无线通信接入协议)", f"{R}/nw/BPLC"),
    ("国网HDC2.0(第4-2部分 数据链路层)", f"{R}/gw/HDC2.0"),
    ("国网HDC1.0(第4-2部分 数据链路层)", f"{R}/gw/HDC1.0"),
    ("南网SPLC(第4部分 数据链路层)", f"{R}/nw/SPLC"),
]
SKIP_TITLE = re.compile(r"(续表|示例|说明|定义|符号|缩写|术语|缩略语|引用|参考文献|索引)")
NONFRAME = re.compile(r"(频域序列|STF|LTF|频域|时频|导频|OFDM符号|星座图|调制)")


def parse_table(html):
    """展开 rowspan/colspan -> grid; 标记来自 rowspan 继承的格"""
    grid, pending = [], {}
    for rm in ROW.finditer(html):
        r, col = [], 0
        for cm in CELLF.finditer(rm.group(1)):
            attrs, inner = cm.group(2), cm.group(3)
            while col in pending:
                t, n = pending[col]
                r.append((t, True))
                if n > 1:
                    pending[col] = (t, n - 1)
                else:
                    del pending[col]
                col += 1
            text = re.sub(r"<[^>]+>", "", inner).strip()
            mr = re.search(r'rowspan="(\d+)"', attrs)
            mc = re.search(r'colspan="(\d+)"', attrs)
            rs = int(mr.group(1)) if mr else 1
            cs = int(mc.group(1)) if mc else 1
            for k in range(cs):
                r.append((text, False))
                if rs > 1:
                    pending[col + k] = (text, rs - 1)
            col += cs
        while col in pending:
            t, n = pending[col]
            r.append((t, True))
            if n > 1:
                pending[col] = (t, n - 1)
            else:
                del pending[col]
            col += 1
        grid.append(r)
    return grid


def main_md(d):
    mds = [f for f in os.listdir(d) if f.endswith(".md")]
    if not mds:
        return None, None
    best = max(mds, key=lambda x: os.path.getsize(os.path.join(d, x)))
    return os.path.join(d, best), best


def parse_doc(path):
    raw = open(path, encoding="utf-8").read()
    lines = raw.split("\n")
    heads = [(i, l.strip()) for i, l in enumerate(lines, 1) if re.match(r"^#{1,5}\s", l)]
    tables = {}
    for i, l in enumerate(lines, 1):
        m = CAP.match(l)
        if not m:
            continue
        num, title = int(m.group(2)), re.sub(r"\s+", " ", m.group(3)).strip()
        if not title or SKIP_TITLE.search(title) or NONFRAME.search(title):
            continue
        body = None
        for j in range(i, min(i + 6, len(lines))):
            tm = TBL.search(lines[j]) or TBL.search("\n".join(lines[j:j + 3]))
            if tm:
                body = parse_table(tm.group(1))
                break
        if not body or len(body) < 2:
            continue
        sec = ""
        for hi, hl in heads:
            if hi < i:
                sec = re.sub(r"^#+\s*", "", hl)
            else:
                break
        tables.setdefault(num, []).append({
            "line": i, "title": title, "sec": sec,
            "rows": [[c[0] for c in r] for r in body],
            "fill": [[c[1] for c in r] for r in body],
        })
    return lines, tables


def is_enum(t):
    return t["rows"] and len(t["rows"][0]) <= 3 and not any(
        r and r[0].endswith("字节号") for r in t["rows"][:1])


def cross_byte(t):
    """修: 只有「字节号」列真含 '-' 且大小>4比特 才算跨字节; 比特位列不再被当字节号"""
    out = []
    for r, fl in zip(t["rows"][1:], t["fill"][1:]):
        if len(r) < 3:
            continue
        name, byte, bit = r[0], r[1], r[2]
        size = r[3] if len(r) > 3 else ""
        m = re.search(r"(\d+)\s*比特", size)
        if m and int(m.group(1)) > 4:
            out.append((name, byte, bit, size, "跨字节位域" + ("(续行)" if any(fl) else "")))
        elif "-" in byte and re.search(r"\d", byte) and bit.strip():
            out.append((name, byte, bit, size, "字节区间"))
    return out


CAT_FRAME = [
    ("物理层帧", re.compile(r"(SOF|前导|信标|物理层帧|突发|STF|LTF|协调帧|COORD|ACK帧|确认帧)")),
    ("MAC/链路帧", re.compile(r"(MAC\s*帧|帧头|帧格式|帧结构|单跳|多跳|BPLC\s*MAC|长帧头|短帧头)")),
    ("管理消息(MMe)", re.compile(r"(管理消息|关联|代理变更|代理汇总|离线|心跳|发现列表报文|报文格式)")),
    ("MSDU", re.compile(r"(MSDU|应用层|IPv4|RIP)")),
    ("信息单元(IE)", re.compile(r"(信息单元|站点属性|站点路由|邻居节点|站点信息|代理信息)")),
]


def fixed_len(t):
    """固定头长度 = 字节号跨度; 字节号含 '+' 或末字段含「可变」→ 可变长。"""
    hi = -1
    var = False
    for r in t["rows"][1:]:
        if len(r) < 2:
            continue
        name, byte = r[0], r[1]
        mean = " ".join(r[4:]) if len(r) > 4 else ""
        if re.search(r"可变", name + mean):
            var = True
        if "+" in byte:
            var = True
        m = re.search(r"(\d+)\s*(?:-\s*(\d+))?\s*$", byte.strip())
        if m:
            end = int(m.group(2) or m.group(1))
            hi = max(hi, end)
    return (hi + 1 if hi >= 0 else 0), var, 0


def frame_summary(tables):
    """每协议一��「帧类型总表」。"""
    rows = []
    for num in sorted(tables):
        for t in tables[num]:
            cat = None
            for c, rx in CAT_FRAME:
                if rx.search(t["title"]):
                    cat = c
                    break
            if cat is None:
                continue
            if t["title"] and re.search(r"(续表|取值范围|间隔|说明|定义|示例)", t["title"]):
                continue
            tot, var, bits = fixed_len(t)
            rows.append((cat, t["title"], num, tot, var, bits, t["sec"], t["line"]))
    return rows


def main():
    out = ["# TEST_FRAME_CATALOG — 四协议全帧类型清单", "",
           "> 自动生成：`tools/gen_frame_catalog.py`（v2：按 rowspan/colspan 展开网格）。",
           "> **同名表一律标注归属章节**；**跨字节位域**在字段表上方以 `⚠跨字节位域` 列出。",
           "> `⚠rowspan` 标记表示该行含源件 rowspan 拆行未合并的单元格 —— **回源表核对**。",
           ""]
    for tag, d in DOCS:
        p, name = main_md(d)
        out.append(f"\n---\n\n# {tag}")
        if not p:
            out.append(f"\n> 目录 `{d}` 下无 md。\n")
            continue
        out.append(f"\n> 源件：`{name}`（{os.path.getsize(p):,} B）\n")
        lines, tables = parse_doc(p)
        heads = [(i, l.strip()) for i, l in enumerate(lines, 1)
                 if re.match(r"^#{2,4}\s", l) and re.search(
                     r"(帧格式|帧结构|MAC ?帧|管理消息|单跳|信标|物理层帧|控制帧|数据帧|MPDU)", l)]
        if heads:
            out.append("## 帧相关章节\n")
            out.append("| 行号 | 章节 |")
            out.append("|---|---|")
            for i, h in heads[:34]:
                out.append(f"| L{i} | {re.sub(r'^#+\\s*','',h)[:80]} |")
            out.append("")
        fs = frame_summary(tables)
        if fs:
            out.append("## ★ 帧类型总表（本协议）\n")
            out.append("> 固定头长度 = 该表内各字段「字段大小」列累加（字节 + 比特折算字节）；")
            out.append("> 「可变」= 末字段含可变长部分，实际帧长需另加可变内容。")
            out.append("> **表号即源表号，同名表请看「归属章节」消歧**。\n")
            out.append("| 类别 | 帧/报文名 | 表号 | 固定头(B) | 含比特域 | 可变长 | 归属章节 | 行号 |")
            out.append("|---|---|---|---|---|---|---|---|")
            last = None
            for cat, title, num, tot, var, bits, sec, line in fs:
                if cat != last:
                    out.append(f"| **{cat}** | | | | | | | |")
                    last = cat
                out.append(f"| | {title[:44]} | **表{num}** | {tot if tot else '—'} | "
                           f"{bits if bits else '—'} | {'**是**' if var else '否'} | "
                           f"`{sec[:30]}` | L{line} |")
            out.append("")
        dups = {n: ts for n, ts in tables.items() if len(ts) > 1}
        if dups:
            out.append("## ⚠ 同名表索引（必须按章节区分，勿按表号直接配对）\n")
            out.append("| 表号 | 出现次数 | 各处归属章节 | 行号 |")
            out.append("|---|---|---|---|")
            for n, ts in sorted(dups.items()):
                out.append(f"| 表{n} | {len(ts)} | " +
                           " / ".join(f"`{t['sec'][:34]}`" for t in ts) + " | " +
                           ", ".join(f"L{t['line']}" for t in ts) + " |")
            out.append("")
        enums = [(n, t) for n, ts in tables.items() for t in ts if is_enum(t)]
        if enums:
            out.append("## 类型标识符清单（枚举表）\n")
            for n, t in sorted(enums, key=lambda x: x[0]):
                out.append(f"\n### 表{n} {t['title']}　`{t['sec'][:60]}`　(L{t['line']})\n")
                w = max(len(t["rows"][0]), 2)
                out.append("| " + " | ".join((t["rows"][0] + [""] * w)[:w]) + " |")
                out.append("|---|" + "---|" * (w - 1))
                for r in t["rows"][1:]:
                    out.append("| " + " | ".join(x.replace("|", "\\|")[:60] for x in (r + [""] * w)[:w]) + " |")
                out.append("")
        fields = [(n, t) for n, ts in tables.items() for t in ts if not is_enum(t)]
        if fields:
            out.append("## 帧/报文字段表\n")
            for n, t in sorted(fields, key=lambda x: x[0]):
                cb = cross_byte(t)
                out.append(f"\n### 表{n} {t['title']}　`{t['sec'][:60]}`　(L{t['line']})\n")
                if cb:
                    out.append("**⚠跨字节位域**：" + "；".join(
                        f"{c[0]}(byte{c[1]} bit{c[2]}, {c[3]})" for c in cb[:8]) + "\n")
                w = max(len(t["rows"][0]), 5)
                out.append("| " + " | ".join((t["rows"][0] + [""] * w)[:w]) + " |")
                out.append("|---|" + "---|" * (w - 1))
                for r, fl in zip(t["rows"][1:], t["fill"][1:]):
                    cells = (r + [""] * w)[:w]
                    mark = " ⚠rowspan" if any(fl) else ""
                    cells = [c + mark for c in cells]
                    out.append("| " + " | ".join(c.replace("|", "\\|")[:64] for c in cells) + " |")
                out.append("")
    dest = r"D:/code/gitlab/HPLC_HRF_GW/monitor/BPLC_STA_QtMonitor/docs/TEST_FRAME_CATALOG.md"
    open(dest, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print(f"已生成 {dest}  {os.path.getsize(dest):,} B  {len(chr(10).join(out).split(chr(10)))} 行")


if __name__ == "__main__":
    main()
