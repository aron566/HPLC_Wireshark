# SNR / RSSI 量化映射表 —— 400dpi 源像素核实记录

> 对应报告：`HDC1.0vsHDC2.0_v2.1.md` §6 事项 15（🔴 真协议差异）、附录A C2
> 核实方式：PyMuPDF `page.get_pixmap(dpi=400)` 直接渲染**原 PDF 页面**，逐行视觉转录。
> 页码由 MinerU `content_list.json` 的 `table_caption` 字段定位（**注意：题注不在 `text` 字段，
> 用 text 检索会全部落空 —— 这是本轮定位失败两次的原因**）。

## 源文件

| 侧 | PDF | 版面 | page_idx | 印刷页 |
|---|---|---|---|---|
| 旧 HDC1.0 | `gw/HDC1.0/双模通信互联互通技术规范 第4-2部分：数据链路层通信协议.pdf` | **扫描件（无文本层）** | 75 / 76 / 77 | 76 / 77 / 78 |
| 新 HDC2.0 | `gw/HDC2.0/双模通信互联互通技术规范_第4_2部分数据链路层通信协议.pdf` | **矢量（有文本层）** | 62 / 63 | 63 / 64 |

渲染产物：`%LOCALAPPDATA%/Temp/mineru_gw/snr_<tag>_p<page_idx>.png`

---

## 证据 1：旧版负号在**印刷时就缺失**（非 OCR 造成）

### 旧 HDC1.0 page_idx=75（印刷页 76）—— 表 131 正文 + 表 132

```
表 131  平均信噪比长度定义
  SNR 结果 | 定义
  6        | 平均信噪比信息长度为6比特, 映射关系为 SNR≤ 6 dB, 量化值=0,
           | SNR> 6 dB, 量化值=SNR 结果+6
表 132  比特时平均信噪比映射值
  SNR 结果                  | 量化值
  SNR≤6 dB                  | 0000
  6 dB<SNR≤4 dB             | 0001
  4 dB<SNR≤2 dB             | 0010
  2 dB<SNR≤0 dB             | 0011
  0 dB<SNR≤2 dB             | 0100
  2 dB<SNR≤4 dB             | 0101
```

### 旧 HDC1.0 page_idx=76（印刷页 77）—— 表 132（续）+ 表 134

```
表 132（续）
  4 dB<SNR≤6 dB   | 0110
  6 dB<SNR≤8 dB   | 0111
  8 dB<SNR≤10 dB  | 1000
  10 dB<SNR≤12 dB | 1001
  12 dB<SNR≤14 dB | 1010
  14 dB<SNR≤16 dB | 1011
  16 dB<SNR≤19 dB | 1100
  19 dB<SNR≤22 dB | 1101
  22 dB<SNR≤25 dB | 1110
  SNR>25 dB       | 1111
表 134  4比特信号强度长度映射
  RSSI≤105 dBm        | 0000
  105 dBm<RSSI≤95 dBm | 0001
  95 dBm<RSSI≤86 dBm  | 0010
  86 dBm<RSSI≤80 dBm  | 0011
  80 dBm<RSSI≤74 dBm  | 0100
  74 dBm<RSSI≤68 dBm  | 0101
  68 dBm<RSSI≤62 dBm  | 0110
  62 dBm<RSSI≤56 dBm  | 0111
  56 dBm<RSSI≤48 dBm  | 1000
  48 dBm<RSSI≤42 dBm  | 1001
  42 dBm<RSSI≤36 dBm  | 1010
  36 dBm<RSSI≤30 dBm  | 1011
  30 dBm<RSSI≤20 dBm  | 1100
  20 dBm<RSSI≤10 dBm  | 1101
  10 dBm<RSSI≤0 dBm   | 1110
```

### 旧 HDC1.0 page_idx=77（印刷页 78）—— 表 134（续）+ 表 135

```
表 134（续）
  RSSI> 0 dBm | 1111
表 135  6比特信号强度长度映射关系
  RSSI≤110 dBm                     | 000000 | 48 dBm<RSSI≤46 dBm  | 100000
  110 dBm<RSSI≤108 dBm            | 000001 | 46 dBm<RSSI≤44 dBm  | 100001
  108 dBm<RSSI≤106 dBm            | 000010 | 44 dBm<RSSI≤42 dBm  | 100010
  106 dBm<RSSI≤104 dBm            | 000011 | 42 dBm<RSSI≤40 dBm  | 100011
  104 dBm<RSSI≤102 dBm            | 000100 | 40 dBm<RSSI≤38 dBm  | 100100
  …
  50 dBm<RSSI≤48 dBm              | 011111 | RSSI>14 dBm         | 111111
```

### 新 HDC2.0 page_idx=62（印刷页 63）—— 表 71（续）+ 表 73（负号完整）

```
表 71（续）  4比特平均信噪比映射值
  SNR≤-10 dB            | 0000
  -10 dB<SNR≤-7 dB      | 0001
  -7 dB<SNR≤-4 dB       | 0010
  -4 dB<SNR≤-1 dB       | 0011
  -1 dB<SNR≤2 dB        | 0100
  …
  29 dB<SNR≤32 dB       | 1110
  SNR>35 dB             | 1111
表 73  4比特信号强度长度映射
  RSSI≤-95 dBm           | 0000
  -95 dBm<RSSI≤-86 dBm   | 0001
  -86 dBm<RSSI≤-80 dBm   | 0010
  -80 dBm<RSSI≤-74 dBm   | 0011
  -74 dBm<RSSI≤-68 dBm   | 0100
  -68 dBm<RSSI≤-62 dBm   | 0101
  -62 dBm<RSSI≤-56 dBm   | 0110
  -56 dBm<RSSI≤-48 dBm   | 0111
  -48 dBm<RSSI≤-42 dBm   | 1000
  -42 dBm<RSSI≤-36 dBm   | 1001
  -36 dBm<RSSI≤-30 dBm   | 1010
  …
```

---

## 定性结论

### ① 负号缺失 = **源扫描件缺陷**，不是转换缺陷

- 400dpi 原图中**确实没有负号笔画** —— 表 131 正文、表 132、134、135 **三处一致**；
- HDC1.0 PDF **无文本层**（纯扫描），MinerU 的 OCR 未丢失任何信息，**转换是忠实的**；
- 对比 HDC2.0 为**矢量 PDF**（有文本层），负号在**版式源**中即存在 → 完整保留。

### ② SNR 侧：**旧表132 ≡ 新表98，无协议变更**

| 档位 | 旧表132 印刷值 | 还原负号 | 新表98 |
|---|---|---|---|
| 0000 | `SNR≤6 dB` | `SNR≤-6 dB` | `SNR≤-6 dB` |
| 0001 | `6 dB<SNR≤4 dB` | `-6 dB<SNR≤-4 dB` | `-6 dB<SNR≤-4 dB` |
| … | … | … | … |
| 1111 | `SNR>25 dB` | `SNR>25 dB` | `SNR>25 dB` |

**每一行印刷值都恰好等于真值去掉负号**；还原后与新表98 **逐档一致**（起点 -6 dB、2 dB 步进）。
→ 旧侧负号缺失**不影响判读**，SNR 映射**两版相同**。

### ③ RSSI 侧：**真实量化错位**（🔴 影响互操作）

**4 比特（旧表134 ↔ 新表73）**

| RSSI 区间 | 旧表134（还原负号） | 新表73 | 量化值差 |
|---|---|---|---|
| ≤ -105 | 0000 | — | — |
| (-105, -95] | 0001 | 并入 0000 | **1** |
| (-95, -86] | 0010 | 0001 | **1** |
| (-86, -80] | 0011 | 0010 | **1** |
| (-80, -74] | 0100 | 0011 | **1** |

新版把旧版最低一档 `(-105,-95]` **并入 `0000`**，导致**整个量程的量化值比旧版小 1**。

**6 比特（旧表135 ↔ 新表74）**：旧起点 `-110 dBm`、新起点 `-100 dBm`，步进同为 2 dB → **量程整体错位 5 档（10 dB）**。

> ⚠️ 新表71（`≤-10 dB` 起、**3 dB** 步进）属 §5.1.3.10.3 章节的**另一张表**，与旧表132 无对应关系。

### ④ 待编制方确认

新版 RSSI 量化网格属**有意调整**，抑或系编制时的排版偏移？该差异直接影响
**上报值与门限判定的跨版本一致性**，建议向 HDC2.0 编制方求证。

---

## 复现方法

```python
import glob, json, os, fitz
cl = [f for f in glob.glob(os.path.join(STAGE, "*_content_list.json")) if "v2" not in f][0]
d = json.load(open(cl, encoding="utf-8"))
doc = fitz.open(PDF)
for it in d:                                    # ⚠️ 题注在 table_caption，不在 text
    caps = it.get("table_caption") or []
    if caps and caps[0].replace(" ", "").startswith("表132"):
        doc[it["page_idx"]].get_pixmap(dpi=400).save(f"p{it['page_idx']}.png")
```