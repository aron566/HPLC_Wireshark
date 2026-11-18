# frame_wrap —— 原始报文 → 0x3C/0x3E 封装 txt 工具

把十六进制原始报文封装成 `0x3C … 0x3E` 帧文本，**时间值固定为 0**，并可选 `isRF`。
输出与程序内 `src/io/playbackwriter.h` 的 RawHex 导出**逐字节同构**，可直接被「回放(RawHex)」读取。

## 启动

| 平台 | 命令 |
|---|---|
| Windows | 双击 `frame_wrap.bat`（或命令行 `frame_wrap.bat`） |
| Linux/macOS | `./frame_wrap.sh` |
| 任意平台（直接） | `python3 frame_wrap_gui.py` |
| 无图形环境自检 | `python3 frame_wrap_gui.py --selftest` |

依赖：**仅 Python 3 标准库 tkinter**，无第三方包。
Linux 若提示缺 tkinter：`sudo apt-get install -y python3-tk`（Fedora 用 `python3-tkinter`）。

## 界面与操作

```
① 原始报文      每行一帧，十六进制；容忍 0x 前缀 / 空格 / 逗号 / 分号 / 换行 / 连写(3c9c0024)；# 开头为注释
② isRF          单选 0(PLC) / 1(RF)
   时间值        固定 0（不可改）
③ 头字段        phr_mcs / option / channel（十六进制，默认 0）
④ 输入模式      报文为 MPDU（自动加 6B 头，dlen = MPDU + 4）
                报文已含完整 data（只把时间 4B 置 0 并重算 dlen）
⑤ 存储位置      路径输入框 + 「浏览…」选择
⑥ 开始转换      转换并在下方预览；有存储路径时直接写入文件
```

## 封装格式（与 C++ 实现同构，勿自行改动）

```
data  = [dlen 2B LE][ts 4B LE][phr_mcs][option][channel][isRF][MPDU]
dlen  = len(MPDU) + 4              ts = 0（本工具固定）
frame = 0x3C + escape(data) + 0x3E
escape: 0x3C/0x3D/0x3E → 0x3D + (0xFF - b)   即 0xC3 / 0xC2 / 0xC1
输出行 = 每行一帧，字节写作 0xNN（小写），单空格分隔
```

示例（MPDU = `3C 9C 00 24`，isRF=1，头字段全 0）——以下为工具实算输出，`dlen = 4+4 = 8`：

```
0x3c 0x08 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x01 0x3d 0xc3 0x9c 0x00 0x24 0x3e
     └dlen=8┘ └── ts=0 (4B) ──┘ phr opt ch  isRF └─ 3C 被转义为 3D C3 ─┘
```

## 已验证事实

1. **自检**（`--selftest`，22 条断言）：转义/反转义互逆、dlen 语义、ts 全 0、头字段顺序、isRF 生效、帧首尾哨兵、行格式、解析容错、多行转换、`already_full` 模式 → `RESULT: ALL PASS`
2. **真实抓帧闭环**（fixture `unit_test/frames/frame-probe/nw_log_tail_wire_escaped.bin`，766 B）：
   - 该帧尾部 ASCII 日志有 **8 处未转义的 `=`(0x3D)**，属设备侧畸形帧（正是程序「宽松反转义」存在的原因）
   - 工具严格转义后比原 wire 长 **8 字节**（763 → 771），差值恰好等于那 8 处 → 行为正确
   - `unescape(escape(data)) == data` 自洽；`txt 行 ↔ 字节` 往返一致
3. **GUI 冒烟**：拦截 `mainloop` 构建整棵控件树无异常（不弹窗）。

## 维护须知（改这个工具前必读）

- **`frame_wrap.bat` 必须保持「纯 ASCII + CRLF」**，两条都不能破：
  - 用 **LF** 换行 ⇒ cmd.exe 按行解析错乱，会报 `'hon' 不是内部或外部命令` / `'rlevel'` / `'of'` 之类的怪错（因为 `%errorlevel%` 被拆开解析）；
  - 用 **UTF-8 中文注释** ⇒ 中文控制台（GBK/936）按 OEM 码页读，注释变乱码甚至被当命令执行（如把「安装并加入 PATH」当命令）。
  - 已在仓库根 `.gitattributes` 用 `*.bat text eol=crlf` 强制换行，**但仍不要在里面写中文**。
- `frame_wrap.sh` 必须保持 **LF**（已由 `*.sh text eol=lf` 强制）。
- 逻辑改动后请跑 `--selftest`（24 条断言）与 GUI 冒烟，两者都要过。

## 注意

- **回放端最短要求**：`serialreader.cpp::process_raw_hex_line` 要求反转义后的 `data.size() >= 26`（10B 采集头 + 16B FCH），即 **MPDU 至少 16 字节**；不足的帧会被程序**静默丢弃**。本工具会弹出警告并标出是哪一行，但不会阻止生成。
- 本工具**只负责封装**，不校验报文字段合法性（`dlen`/`ts` 按上式生成，不做语义检查）。
- 真实设备帧的 `dlen` 有时**不等于**剩余长度（如尾部附调试日志的帧）；本工具与 C++ 导出保持同一口径（`dlen = MPDU + 4`），不要照抄设备原值。
- 输出为 ASCII 文本，换行恒为 `\n`（与 C++ 导出一致）。
- 所有帧 `ts = 0`，因此回放时帧间 NTB 差恒为 0（时间轴等价于一帧的时刻），这就是"时间值为 0"的语义。
