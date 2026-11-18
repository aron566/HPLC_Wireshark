# tools —— 辅助脚本索引

本目录放**开发/自查/转换类**小工具（不参与主程序构建）。运行环境：
- Windows 用 `py -3 <脚本>`；Linux/macOS 用 `python3 <脚本>`
- 除 `frame_wrap/` 需要 tkinter（Python 自带）外，其余仅用标准库

---

## 1. 报文转换（有 GUI）

### `frame_wrap/` —— 原始报文 → 0x3C/0x3E 封装 txt

把十六进制原始报文封装成 `0x3C … 0x3E` 帧文本，**时间值固定 0**，可选 `isRF`。
输出与程序内 `src/io/playbackwriter.h` 的 RawHex 导出**逐字节同构**，可被「回放(RawHex)」直接读取。

| 平台 | 启动 |
|---|---|
| Windows | 双击 `tools/frame_wrap/frame_wrap.bat` |
| Linux/macOS | `./tools/frame_wrap/frame_wrap.sh` |
| 任意（直接） | `python3 tools/frame_wrap/frame_wrap_gui.py` |
| 无图形环境自检 | `python3 tools/frame_wrap/frame_wrap_gui.py --selftest` |

界面：原始报文输入框（每行一帧）· isRF 单选（0 PLC / 1 RF）· 时间值固定 0 · 头字段
`phr_mcs/option/channel`（默认 0）· 输入模式（MPDU 自动加头 / 已含完整 data 只置时间 0）·
存储位置选择 · 开始转换。

> 细节、格式公式与已验证事实见 [`frame_wrap/README.md`](frame_wrap/README.md)。
> 注意：回放端要求反转义后 `data.size() >= 26`（即 MPDU ≥ 16 字节），过短的帧会被静默丢弃，工具会给出警告。

---

## 2. 代码规范 / i18n 自查（CI 也会跑）

| 脚本 | 用途 | 用法 |
|---|---|---|
| `check_i18n.py` | i18n 配对与「裸中文」自查：①裸中文未走 `trl::L` ②`trl::L` 未配对键 ③`register_en` 死键 | `py -3 tools/check_i18n.py [--strict] [--list]` |
| `check_title_case.py` | 英文文案 Title Case 合规（规则见 `docs/CODING_STYLE.md`） | `py -3 tools/check_title_case.py`（只报硬伤）<br>`py -3 tools/check_title_case.py --strict`（连带实词小写） |

两者都要求 **0 问题** 才能过检。日常提交前建议各跑一次；`--strict` 用于发版前。

---

## 3. 协议资料生成（读 docs/protocol_doc 下的规约，产出对照表）

| 脚本 | 产出 |
|---|---|
| `gen_frame_catalog.py` | `TEST_FRAME_CATALOG` —— 按 rowspan/colspan 展开表格网格，区分「字节号 / 比特位」，生成帧字段目录 |
| `gen_frame_crossref.py` | 跨协议帧类型对照表（南网 BPLC / HDC1.0 / HDC2.0 / 南网 SPLC 四协议横向） |
| `gen_tier3_matrix.py` | 3 级帧型矩阵（覆盖四协议全部 MMe 语义，含层级/方向/四协议表号与标识） |

> 这三个脚本从 `docs/protocol_doc/` 的 MinerU 转换件里解析规约表格。
> **注意**：MinerU 的 `.md` 可能截断/错位，涉及协议坐标的结论必须回原始 PDF 逐行核对。

---

## 4. 其它

| 路径 | 说明 |
|---|---|
| `nsis-3.09/`、`nsis.zip` | NSIS 3.x 便携版，供 `scripts/package.sh` 打 Windows 安装包（被 `.gitignore` 忽略，仅本地存在） |
| `__pycache__/` | Python 缓存，勿入库 |

---

## 相关入口（不在此目录）

- 打包/发版脚本：`scripts/`（如 `package.sh`、`update_json_release.py`）
- CI 探针驱动：`unit_test/ci/probe_suite.sh`
- 规约原始资料：`docs/protocol_doc/`
