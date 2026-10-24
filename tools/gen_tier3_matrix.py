#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
3 级帧型矩阵 v2 —— 覆盖四协议**全部** MMe 语义, 并给出层级/方向/四协议表号与标识符。
"""
import os
import re
import sys

sys.path.insert(0, r"D:/code/gitlab/HPLC_HRF_GW/monitor/BPLC_STA_QtMonitor/tools")
import gen_frame_catalog as G

R = G.R
PROTO = [("南网BPLC", f"{R}/nw/BPLC"), ("国网HDC1.0", f"{R}/gw/HDC1.0"),
         ("国网HDC2.0", f"{R}/gw/HDC2.0"), ("南网SPLC", f"{R}/nw/SPLC")]

# 语义 -> (层级, 方向, 用途, 匹配正则)
ROLE = [
    ("关联请求",         "3→2", "上行", "站点申请入网，携带候选代理 TEI 列表", r"^关联请求"),
    ("关联确认",         "2→3", "下行", "代理/CCO 应答，分配站点 TEI 与链路类型", r"^关联确认"),
    ("关联汇总指示",     "2→1", "上行", "代理向 CCO 汇总本级下属站点", r"^关联汇总指示"),
    ("代理变更请求",     "2→1", "上行", "代理拓扑变更上报", r"^代理变更请求"),
    ("代理变更确认",     "1→2", "下行", "CCO 对代理变更的应答", r"^代理变更确认(?!.*BitMap)"),
    ("代理变更位图确认", "1→2", "下行", "CCO 以位图形式确认代理变更", r"^代理变更确认.*BitMap"),
    ("离线指示",         "3→1", "上报", "站点离线通知", r"^离线指示"),
    ("心跳检测",         "*→1", "上报", "保活心跳", r"^心跳检测"),
    ("发现列表",         "3→2→1", "每级上报", "**每一级都向上一级上报**邻居发现信息(STA→代理、代理→CCO); 报文自带『层级』『角色』字段, 说明各级都发", r"^发现列表"),
    ("通信成功率上报",   "3→1", "上报", "站点上报通信成功率", r"^通信成功率上报"),
    ("网络冲突上报",     "2→1", "上报", "代理上报网络冲突", r"^网络冲突上报"),
    ("路由请求",         "3→1", "上行", "请求到目标站点的路由", r"^路由请求"),
    ("路由回复",         "1→3", "下行", "路由请求的应答", r"^路由回复"),
    ("路由应答",         "2→1", "上行", "路由建立完成应答", r"^路由应答"),
    ("链路确认请求",     "2→2", "同级", "相邻代理间链路确认", r"^链路确认请求"),
    ("链路确认回应",     "2→2", "同级", "相邻代理间链路确认回应", r"^链路确认回应"),
    ("无线信道冲突上报", "2→1", "上报", "上报无线信道冲突", r"^无线信道冲突上报"),
]


def norm(s):
    s = re.sub(r"[（(].*?[)）]", "", s)
    s = re.sub(r"(报文|信息|帧|类型|格式|字段|取值|定义|说明)+$", "", s)
    return re.sub(r"[\s　]+", "", s)


def collect(d):
    p, name = G.main_md(d)
    lines, tables = G.parse_doc(p)
    res = {}
    for num in sorted(tables):
        for t in tables[num]:
            if not G.is_enum(t):
                continue
            if not re.search(r"管理消息类型|MMe", t["title"]):
                continue
            for r in t["rows"][1:]:
                if len(r) < 2 or not r[0].strip():
                    continue
                raw = r[0].strip()
                k = norm(raw)
                if not k or len(k) > 14 or k == "保留":
                    continue
                res.setdefault(k, []).append((num, r[1].strip(), t["sec"][:24], raw))
    return res, name


def hop_msgtype(d):
    p, name = G.main_md(d)
    lines, tables = G.parse_doc(p)
    res = {}
    for num in sorted(tables):
        for t in tables[num]:
            if not G.is_enum(t):
                continue
            if not re.search(r"消息类型|帧类型", t["title"]):
                continue
            for r in t["rows"][1:]:
                if len(r) < 2 or not r[0].strip():
                    continue
                res.setdefault(r[0].strip(), []).append((num, r[1].strip() if len(r) > 1 else "", t["sec"][:24]))
    return res


data = {}
for tag, d in PROTO:
    e, src = collect(d)
    data[tag] = e
    print(f"{tag:<9s} MMe 语义 {len(e):>3d}  {src[:38]}")

union = sorted({k for e in data.values() for k in e})

out = ["# 3 级帧型矩阵（v2）", "",
       "> 用途：为 3 级网络测试数据集提供「**哪一帧型 / 哪一层级之间 / 哪一方向 / 四协议各是什么表号与标识符**」。",
       "> 层级：**第1级 = CCO（主站）**、**第2级 = 代理（Proxy）**、**第3级 = 站点（STA）**；",
       "> 方向以 **上行 = 3→2→1**、**下行 = 1→2→3**、**同级 = 2↔2** 为准。",
       "> 自动生成：`tools/gen_tier3_matrix.py`。**层级/方向为按语义推断**（标「推断」）；表号与标识符为源表实测。", ""]

out.append("## 一、3 级帧型矩阵（语义 × 层级 × 方向 × 四协议）\n")
out.append("| 帧型(语义) | 层级 | 方向 | 用途 | 南网BPLC | 国网HDC1.0 | 国网HDC2.0 | 南网SPLC |")
out.append("|---|---|---|---|---|---|---|---|")
for sem in union:
    role = next((r for r in ROLE if re.search(r[4], sem)), None)
    lvl, dirn, use = (role[1], role[2], role[3]) if role else ("—", "—", "—")
    cells = []
    for tag, _ in PROTO:
        h = data[tag].get(sem)
        if not h:
            cells.append("—")
        else:
            num, val, sec, raw = h[0]
            cells.append(f"表{num} / `{val}`")
    if all(c == "—" for c in cells):
        continue
    out.append(f"| **{sem}** | {lvl}{'（推断）' if role else ''} | {dirn} | {use} | " + " | ".join(cells) + " |")

out.append("")
out.append("> **HDC2.0 的「发现列表」在矩阵中为「—」，是【正确】而非漏配**：")
out.append("> HDC2.0 表43 的 `0x0008` = **保留**（2.0 改经单跳帧/信标承载），")
out.append("> 其发现列表走**单跳帧**：**msg_type 0（无线，表92 14B）/ msg_type 2（载波，表63 23B）**；")
out.append("> 代码佐证：`gw_2026` 的 `topo_event.kind` 无 DiscoverList 事件。")
out.append("> HDC1.0 仍有 MMe 0x0008（表59），故其单元格为表号。")
out.append("\n## 二、同名 MMe 表消歧（同语义多表必看）\n")
out.append("| 协议 | 语义 | 表号 | 标识符 | 归属章节 |")
out.append("|---|---|---|---|---|")
for tag, _ in PROTO:
    for sem, hits in sorted(data[tag].items()):
        if len(hits) > 1:
            for num, val, sec, raw in hits:
                out.append(f"| {tag} | {sem} | 表{num} | `{val}` | `{sec}` |")

out.append("\n## 三、单跳帧消息类型（发现列表类走单跳帧，不走 MMe 标识符）\n")
out.append("| 协议 | 消息类型值 | 含义 | 表号 | 章节 |")
out.append("|---|---|---|---|---|")
for tag, d in PROTO:
    for val, lst in sorted(hop_msgtype(d).items(), key=lambda x: x[0]):
        num, v2, sec = lst[0]
        out.append(f"| {tag} | `{val}` | {v2[:28]} | 表{num} | `{sec}` |")

out.append("\n## 四、🔴 跨协议 MMe 标识符号段分裂（按语义复制 = 静默失效）\n")
out.append("| 语义 | 南网BPLC | 南网SPLC | 国网HDC1.0 | 国网HDC2.0 |")
out.append("|---|---|---|---|---|")
split = 0
for sem in union:
    row = []
    for tag, _ in PROTO:
        h = data[tag].get(sem)
        row.append(f"`{h[0][1]}`" if h else "—")
    vals = {x for x in row if x != "—"}
    if len(vals) > 1:
        split += 1
        out.append(f"| **{sem}** | " + " | ".join(row) + " |")
out.append(f"\n> 共 **{split}** 个语义在两套协议间标识符不同。")
out.append("> **南网用 0x0030+ 号段，国网用 0x0000+ 号段。**")
out.append("> 把南网的 MMe 值搬进国网帧 → 国网解析器判为「未知 MMe」**静默跳过**，最难查。")
out.append("> 数据集里 MMe 标识符必须**按协议参数化**，不可按语义硬编码。")

out.append("\n## 五、边界说明\n")
out.append("- **表号与标识符**为源表实测（自动提取 + rowspan 展开）。")
out.append("- **层级与方向是按语义推断的**，非源表字段；已标「推断」。落地前请与实现侧对齐。")
out.append("- 仅覆盖**管理消息（MMe）**层；物理层帧（信标/ACK/COORD）与 MSDU/IE 层见 "
           "`docs/TEST_FRAME_CATALOG.md` 的「★ 帧类型总表」。")
out.append("- 四协议中缺失的 MMe 以「—」表示，**不代表该协议不支持**，"
           "只代表本协议的『管理消息类型』枚举表里没列。")

dest = r"D:/code/gitlab/HPLC_HRF_GW/monitor/BPLC_STA_QtMonitor/docs/TEST_TIER3_FRAME_MATRIX.md"
open(dest, "w", encoding="utf-8", newline="\n").write("\n".join(out))
print(f"\n已生成 {dest}  {os.path.getsize(dest):,} B  语义并集 {len(union)}")
