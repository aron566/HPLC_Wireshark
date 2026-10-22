# 提交归属表（代码工作区）

用途：提交时防止"未复核半成品混入"与"漏提交导致干净 clone 编不过"。

> **⛔ 提交冻结**：用户 2026-10 指示"arch 架构先等我通知再改"——本表所涉两笔提交**一律暂停**，
> 等用户明确通知后再执行。届时按 §3/§4 的顺序走，第 3 步（提交后重新全量编译）不可省。
口径：`git diff -U0 -- src/` 逐 hunk 人工核对 + `git status` 实测未跟踪项。
建立：2026-10-22（sytem-arch-analysis 产出，vibe-coding 交叉核对 hunk 数一致）

---

## 0. 结论先行

| 层 | 数量 | 处置 |
|---|---|---|
| **索引已暂存** | **757** | 755 张协议文档图片重排（`gw/images/` → `gw/HDC1.0|HDC2.0/images/`、`nw/BPLC/…images/`）+ 2 份差异报告。**独立一笔直接 `git commit`** |
| 已跟踪已修改（`src/`） | **32 文件 / 190 hunk** | 逐 hunk 归属见 §1 |
| **未跟踪（`src/`）** | **12 项** | 见 §2。**漏 add 任一 → 干净 clone 编不过**（`protocol.pri:15` 引用了 `gw_2026.pri`） |
| 未跟踪（非 src） | 18 项 | 4 份 docs + 6 个 samples + 既有探针 |

## 1. 已跟踪已修改文件（190 hunk）

### 1.1 本会话 A1–A5（可完全归属，7 个文件）

| 文件 | 本会话/总 hunk | 内容 |
|---|---|---|
| `common/packetentry_serialize.h` | 9/9 | 删 3 死字段 + 补 `coord_rsv1/2/3` + block v10 |
| `plugin/ipc/plugin_serialization.cpp` | 9/9 | `DissectField` 算子 + `ParseResult.dissect_fields` + 插件位置位 |
| `plugin/ipc/plugin_serialization.h` | 1/1 | 同上声明 |
| `plugin/ipc/plugin_ipc.h` | 1/1 | `kWireFormatVersion` 6→7 |
| `protocol/iprotocolparser.h` | 2/2 | `dissect_fields` + `entry_protocol_id()` |
| `protocol/protocol_tree_builder.{h,cpp}` | 3/3 | 抽 `tree_apply_type_color` |
| `ui/packetlistmodel.cpp` | 1/1 | A5 掩位 `& 0x7F` |

### 1.2 共享文件（无法文件级切割）

| 文件 | 本会话 | 其余 hunk 归属 |
|---|---|---|
| `common/bplcframe.h` | **10/16** | 6：GW_2026 SOF 9 字段 + `MsduState.reasm_proto`（新协议） |
| `app/mainwindow.cpp` | **4/7** | 3：删 `protocolfactory.h`、2 处 `set_variant`（**上会话**） |
| `ui/protocoltree.cpp` | **3/4** | 1："构建器不预设"注释（**上会话**） |
| `protocol/nw_protocol/nw_2026/nw_2026_tree.cpp` | **1/17** | 16：表23-26 释义、表47/49 物理块、`AckExtType::Bitloading`（新协议） |

### 1.3 全属其它

| 归属 | 文件（hunk 数） |
|---|---|
| **vibe-coding SPLC 修复** | `nw_2021_beacon_parser.{cpp,h}`(7) · `nw_2021_msdu_parser.{cpp,h}`(11) · `nw_2026_beacon_parser.cpp`(14) · `nw_2026_msdu_parser.cpp`(28) · `gw_2022_msdu_parser.cpp`(9) · `nw_2021_tree.cpp`(6) · `nw_2021_parser.cpp`(2) |
| **上会话（IProtocolProvider 重构等）** | `protocolfactory.{cpp,h}`(24) · `commconfigdialog.cpp`(6) · `protocoltree.h`(4) · `appconfig.h`(1) · `framedispatcher.cpp`(1) · `plugin_manager.cpp`(2) · `protocolvariant.h`(1) · 3 个 `.pri`(4) |

## 2. 未跟踪 src 清单（12 项，**必须全部 add**）

| 归属 | 项 |
|---|---|
| **新协议：GW_2026 / HDC 2.0（整个目录从未入库，9 个文件）** | `protocol/gw_protocol/gw_2026/` |
| **新协议：NW_2026 / SPLC（8 个新文件）** | `nw_2026.pri` · `nw_2026_parser.{h,cpp}` · `nw_2026_parser.h` · `nw_2026_pb_table.h` · `nw_2026_provider.cpp` · `nw_2026_tree.h` · `nw_2026_beacon_parser.h` · `nw_2026_msdu_parser.h` |
| **上会话：IProtocolProvider 重构（3 项）** | `protocol/iprotocolprovider.h` · `gw_2022_provider.cpp` · `nw_2021_provider.cpp` |

> ⚠️ `nw_2026_parser.cpp` 里含本会话 **A2（SPLC 无线 SOF SNID 高位合并）** 的修复 —— 它是新文件，漏 add 则 A2 完全不在提交内。
> ⚠️ `nw_2026/` 目录状态混杂：`_parser.cpp` 未跟踪，`_tree.cpp` / `_msdu_parser.cpp` / `_beacon_parser.cpp` 已跟踪已改。**整目录 add 会把未审内容一并带入**，需按上表逐个确认。

### 2.1 "全量提交"并不等于"全部已复核"（勿误解）

全量提交里含三类**不在本轮复核闭环内**的内容，解冻提交前需补一次自检：

| 类别 | 内容 | 复核状态 |
|---|---|---|
| 本会话 A1–A5 + B1 | 见 §1.1、§1.2 的"本会话"hunk、`nw_2026_parser.cpp` 内的 A2 | ✅ 两轮复核通过 |
| vibe-coding SPLC 6 项 | §1.3 前 7 项文件 + `nw_2026_tree.h` | ✅ code-review-bot 三轮（含终验） |
| **上会话重构** | `protocolfactory.{cpp,h}`、`commconfigdialog.cpp`、`protocoltree.h`、`appconfig.h`、`framedispatcher.cpp`、`plugin_manager.cpp`、`protocolvariant.h`、3 个 `.pri` | ⚠️ 当时经构建+回归验证，**不在本轮复核范围** |
| **`gw_2026/` 整个目录（从未入库）** | GW_2026 / HDC 2.0 全部解析器 + 树 + provider（**作者：vibe-coding 前几个会话，完整实现非半成品**） | ⚠️ **分功能多轮专项复核通过（code-review-bot：单跳/RFDL/越界守卫/载波表归属/SAT 头/SNR 双簇/MMe 等），缺一次全目录终览** |

> `gw_2026/` 实测（2026-10-22，arch-bot 自查）：**17 文件 / 4238 行**；调试残留扫描（`TODO|FIXME|XXX|printf(|qDebug(`）**仅 2 处命中，且都是协议术语 `XXX.41 TMI 编号` 出现在注释里，无真实残留**。
> 结论：可接受"多轮专项复核 + 全绿回归 + 残留扫描"替代逐行人工终览；若坚持终览，排到解冻后第 2 笔提交前执行。

> 提交前最低要求（不可省）：`qmake` + 全量编译 + `tree_dispatch_check` 26/26 + 回放 2457 帧，
> 并**复跑一次调试残留扫描**（`TODO|FIXME|printf(|qDebug(`，排除协议术语 `XXX.41` 这类）——
> 该扫描已由 arch-bot 在冻结期跑过一次，结果见上表注（`gw_2026/` 零真实残留）。

> **`.pri` 引用一致性已验（2026-10-22，arch-bot 只读检查，冻结期完成）**：
> - `gw_2026.pri`：列出 16 项（8 HEADERS + 8 SOURCES），**磁盘 16/16 全部存在，0 缺失**；
> - `nw_2026.pri`：引用 10 项，**磁盘 10/10 全部存在，0 缺失**，无磁盘多余项；
> - 唯一提示：`gw_2026_mcs.h` 未列入 HEADERS，但被 `gw_2026_tree.cpp:5` include —— 不影响编译
>   （qmake 不需要头文件登记），随目录 `git add` 一并入库即可。
>
> ⇒ **"干净 clone 编不过"的风险已排除**，前提是 12 项未跟踪（含 `gw_2026/` 整目录）全部入库。

## 3. 建议提交拆两笔

### 第 1 笔：文档重排（索引已就绪，不带 -a）
```bash
git commit -m "docs(protocol_doc): 协议文档目录重组 —— HDC1.0/HDC2.0/BPLC 图片归位 + 差异报告"
```
提交现有 757 条暂存（755 R + 2 A），不含任何代码。

### 第 2 笔：代码
需先决定范围：

| 方案 | 范围 | 说明 |
|---|---|---|
| **全量** | 32 个已改 + 12 项未跟踪 | 含 6 项 SPLC 修复（已由 code-review-bot 三轮复核闭环）与上会话重构。**推荐**：否则干净 clone 编不过 |
| **仅本会话** | 1.1 的 7 个 + 1.2 的 hunk 级挑选 + `nw_2026_parser.cpp` | 需 `git add -p`，且提交后工作区留半截状态，风险高 |

> 结论：**只能走全量**。因为 `bplcframe.h` 里新协议的 9 个字段、`nw_2026_tree.cpp` 的 16 个 hunk 与 A2 的修复物理相邻，
> 且 `protocol.pri` 已引用未跟踪的 `gw_2026.pri` —— 部分提交必然产生不可编译的提交点。

## 4. 提交前最后的检查清单

- [ ] `git commit`（第 1 笔）后确认 `git show --stat` 只有 docs 图片
- [ ] `git add` 12 项未跟踪 + 32 个已改（第 2 笔）
- [ ] 提交后跑 `qmake && mingw32-make -f Makefile.Release -j4` 确认 exit 0
- [ ] 跑 `samples/tree_dispatch_build` 的 26/26 与 `--replay` 回放
- [ ] `git status` 确认工作区干净（除 samples/docs 新件）
