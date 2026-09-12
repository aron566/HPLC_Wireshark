# BPLC STA Monitor 使用说明书

适用版本：`v1.0.15`  
适用平台：Windows

## 1. 工具简介

BPLC STA Monitor 是 BPLC/HRF（HPLC）协议 STA 报文监控上位机。它用于：

- 从调试串口实时捕获固件输出的 `0x3C ... 0x3E` 封装帧。
- 回放 `.bin` 原始帧文件或裸 hex 文本文件。
- 按 BEACON、SOF、ACK、COORD、SEARCH、SWITCH 等帧类型展示报文列表。
- 解析 FCH、MPDU、PB、MSDU、MMe、APP 等协议字段。
- 将协议字段与十六进制原始字节联动高亮，便于定位字段来源。
- 将捕获结果导出为可再次回放的 `.bin` 文件或裸 hex 文本。
- 统计帧类型、丢帧数量和已重组 MSDU 数量。

## 2. 安装与启动

### 2.1 使用安装包

安装包位于 `dist/`，文件名类似：

```text
BPLC_STA_Monitor_Setup_v1.0.15.exe
```

安装特点：

- 当前用户安装，默认目录为：

```text
%LOCALAPPDATA%\Programs\BPLC_STA_Monitor
```

- 不需要管理员权限，不触发 UAC。
- 自动创建桌面快捷方式和开始菜单项。
- 升级安装会先关闭正在运行的程序，并覆盖旧程序文件。
- 升级时保留已有 `config.ini`；卸载时会删除安装目录，请先备份需要保留的配置或导出数据。

### 2.2 直接运行便携版

如果 `release/` 或安装目录中已经包含 Qt 运行库 DLL，可直接运行：

```text
BPLC_STA_Monitor.exe
```

程序配置文件和导出文件默认位于程序目录或用户选择的位置。

## 3. 快速开始

### 3.1 实时串口采集

1. 连接 STA 调试串口。
2. 点击工具栏“开始”，或按 `Ctrl+E`。
3. 在“通讯口设置”中选择“实时串口”。
4. 选择 COM 口和波特率。当前采集端实际使用 `8` 数据位、`1` 停止位、无校验。
5. 点击“开始捕获”。
6. 新帧会按时间顺序进入帧列表。
7. 点击“停止”结束采集。

### 3.2 回放 `.bin` 文件

1. 点击“开始”。
2. 数据源类型选择“文件回放 (.bin)”。
3. 选择 `.bin` 文件。
4. 点击“开始捕获”。
5. 程序自动识别文件或分段前的 8 字节 BCD 时间标签，并尽可能恢复原始捕获时间。

### 3.3 回放裸 hex 文本

1. 点击“开始”。
2. 数据源类型选择“裸 hex 文本”。
3. 选择 `.txt` 或 `.hex` 文件。
4. 点击“开始捕获”。
5. 文件中的 `TIME: yyyy-MM-dd HH:mm:ss.zzz` 行用于建立该段的时间基准。

### 3.4 查看一帧

1. 在帧列表中选择一行。
2. 底部左侧显示协议字段树。
3. 点击协议树中的字段，例如 `Net ID`、`PB CRC24`、`MSDU CRC32`。
4. 底部右侧十六进制视图会高亮该字段覆盖的原始字节。
5. 右侧“原始报文”区域显示当前帧完整的 `0x3C ... 0x3E` 原始数据。

## 4. 主界面说明

### 4.1 工具栏

| 按钮 | 功能 |
|------|------|
| 开始 | 打开数据源对话框并开始串口采集、文件回放或裸 hex 导入 |
| 停止 | 停止当前采集 |
| 暂停 | 暂停向帧列表写入新帧；再次点击恢复 |
| 清空 | 清空帧列表、统计、协议树和字节视图 |
| 导出 | 将全部帧导出为 `.bin` 或裸 hex 文本 |
| 设置 | 打开通讯口/语言/主题设置对话框 |
| 显示过滤器 | 输入表达式并按“应用”或回车筛选帧列表 |

注意：在 `v1.0.15` 源码当前状态下，工具栏“设置”按钮没有连接到配置对话框。语言、主题和串口参数仍可在“开始”对话框中修改，也可直接编辑 `config.ini`。

### 4.2 帧列表

帧列表列为：

| 列 | 说明 |
|----|------|
| `#` | 从 1 开始的帧序号；清空后重新计数 |
| `Time` | 帧时间。回放时优先使用文件/分段时间标签；实时采集使用本地接收时间 |
| `Delta` | 与上一帧的时间差，单位为秒 |
| `Orig Src` | MSDU 原始源 TEI，显示为 `CCO` 或 `STA-N` |
| `Source` | MPDU 源 TEI；无法解析时显示 `PLC`、`HRF` 或 `DROP` |
| `Destination` | MPDU 目的 TEI；广播显示为 `BROADCAST` |
| `Orig Dst` | MSDU 原始目的 TEI，显示为 `CCO`、`STA-N` 或 `BCAST` |
| `Dir` | 方向：`↑` 上行、`↓` 下行、`→` 广播、`*` 未知或其他 |
| `Protocol` | `HPLC` 或 `HRF`；坏帧显示 `ERR` |
| `Frame Type` | `BEACON`、`SOF`、`ACK`、`COORD`、`SEARCH`、`SWITCH` 等 |
| `MSDU Type` | 完整重组后的 MSDU/MMe/APP 类型摘要 |
| `MSDU Seq` | MSDU 序号 |
| `Length` | 当前记录保存的原始字节长度 |
| `Info` | NetID、TEI、TMI、PBNum、MSDU 长度或丢帧原因 |

帧颜色：

- BEACON：蓝色
- SOF：绿色
- ACK：黄色
- COORD：红色
- DROP/错误帧：灰色

列表默认自动跟随最新帧。用户向上滚动后会暂停自动跟随；滚动回底部后恢复。

### 4.3 协议树

协议树按层级展示：

- Physical：媒介、NTB 时间戳、信道/频段、PHR MCS、Option、FrameTime。
- MPDU Base：Frame Type、Net Type、Net ID、Version、FCH CRC24。
- BEACON：时间戳、源 TEI、TMI、Symbol Num、Line、信标载荷条目和 CRC。
- SOF：源/目的 TEI、Link ID、帧长、PB 数、广播/重发/加密标志、TMI、TMI_EXT。
- SOF PB：每块的 Header、Body、Padding 和 CRC24。
- MSDU：重组后的公用头、MMe/APP 字段树、MSDU CRC32。
- ACK：常规 ACK、Search、Sync、切频等扩展字段。
- COORD：TimeDuration、NextTimeSlotShift、NeighbourNID、NetRfChannel。

字段名后的 `[Nb]` 表示字段位宽。

### 4.4 十六进制与原始报文

十六进制区域格式为：

```text
偏移  byte0 ... byte7  byte8 ... byte15  ASCII
```

- 无对应原始字节的协议分组节点不会产生高亮。
- 同一 MSDU 跨多个 PB 时，只有能够映射到当前帧连续字节范围的字段才可高亮。
- 右键十六进制区域可复制当前高亮字节：
  - `复制 Hex(N 字节)`
  - `复制为 0x 前缀(N 字节)`
- 右键“原始报文”区域可复制整帧：
  - `复制(含 0x 前缀)`
  - `复制(纯 hex)`

### 4.5 状态栏

状态栏左侧显示当前操作状态、采集源、回放进度或错误信息。右侧显示：

- `Total`
- `BEACON`
- `SOF`
- `ACK`
- `COORD`
- `Drop`
- `MSDU`

其中 `MSDU` 表示已完成重组的 MSDU 计数，`Total` 是各类统计值之和，不等同于帧列表行数。

## 5. 帧操作

### 5.1 暂停

“暂停”只暂停把新解析结果加入帧列表。采集线程仍会继续运行，因此暂停期间到达的帧不会保存在列表中，也不会在恢复后补写。

### 5.2 清空

“清空”会清除：

- 当前帧列表
- 待写入列表
- 帧统计
- 协议树
- 十六进制视图
- 帧序号

清空不会停止串口采集。

### 5.3 停止

“停止”会关闭串口或结束当前数据源任务。回放文件打开后会尽快结束并回到待机状态。

## 6. 显示过滤器

### 6.1 基本语法

- `&` 表示 AND。
- `|` 表示 OR。
- `&` 优先于 `|`。
- 不支持括号。

示例：

```text
a & b | c
```

等价于：

```text
(a 且 b) 或 c
```

按“应用”或按回车执行过滤器。过滤器会应用到已有帧以及之后新增的帧，并保存到 `config.ini`，下次启动自动恢复。

### 6.2 可匹配内容

| 内容 | 写法 | 示例 |
|------|------|------|
| 帧类型，精确匹配 | `beacon`、`sof`、`ack`、`coord`、`search`、`switch` | `sof` |
| NetID | 十六进制，可带 `0x` | `cda1d5`、`0xcda1d5` |
| 源或目的 | `cco`、`sta-N`、`broadcast` | `sta-2` |
| 媒介 | `hplc`、`hrf` | `hplc` |
| MSDU/MMe/APP 摘要 | 报文摘要文本 | `assoc`、`event` |
| 帧序号 | 十进制数字，按子串匹配 | `42` |
| 丢帧 | `drop`、`err` 或丢帧原因文本 | `drop` |

示例：

```text
beacon & cda1d5
```

只显示 NetID 为 `cda1d5` 的 BEACON。

```text
sof & sta-2 | ack
```

显示“SOF 且源/目的包含 STA-2”或所有 ACK。

```text
coord & 0xcda1d5
```

显示指定网络的 COORD。

```text
hplc & beacon
```

只显示 HPLC 媒介的 BEACON。

## 7. 导出

点击“导出”，然后选择：

- 回放文件 `*.bin`
- 裸 hex 文本 `*.txt`

导出范围是当前会话中的全部帧，不受当前显示过滤器影响。文件默认名称格式为：

```text
BPLC_yyyyMMdd_HHmmss.bin
```

### 7.1 回放 `.bin`

导出文件保留每帧的原始 `0x3C ... 0x3E` 数据。在首个帧和每个新时间分段前写入 8 字节 BCD 时间标签，用于回放时恢复时间轴。

### 7.2 裸 hex 文本

每行是一条完整的原始帧：

```text
0x3C 0x.. ... 0x3E
```

帧前可包含时间行：

```text
TIME: 2026-09-07 18:43:00.123
```

该文件可直接重新导入“裸 hex 文本”模式。

## 8. 配置文件

配置文件位置：

```text
<程序目录>\config.ini
```

首次启动时自动创建。删除后下次启动会重新生成默认配置。

主要配置项：

```ini
[general]
lang=auto
update_url=https://raw.githubusercontent.com/aron566/HPLC_Wireshark/main/update.json
filter=
theme=auto

[reader]
mode=0
com=COM3
baud=460800
file_path=
time_tag=false
```

| 配置项 | 取值 |
|--------|------|
| `lang` | `auto`、`zh`、`en` |
| `theme` | `auto`、`dark`、`light` |
| `filter` | 帧列表显示过滤器 |
| `update_url` | `update.json` 更新清单地址 |
| `mode` | `0` 串口、`1` `.bin` 回放、`2` 裸 hex |
| `com` | 串口名称 |
| `baud` | 波特率 |
| `file_path` | 回放或导入文件路径 |
| `time_tag` | 兼容旧配置字段；当前新格式会自动识别 BCD 时间标签 |

语言和主题在“开始”对话框修改后通常会立即保存；部分窗口框架文本在重启程序后完全生效。

## 9. 检查更新

操作路径：

```text
帮助 -> 检查更新
```

程序从 `config.ini` 的 `general/update_url` 获取更新清单，比较版本号。发现新版本后会提示下载并运行安装包。

更新清单格式：

```json
{
  "updates": {
    "windows": {
      "latest-version": "1.0.15",
      "download-url": "https://example.com/BPLC_STA_Monitor_Setup_v1.0.15.exe",
      "changelog": "Release notes",
      "mandatory-update": false
    }
  }
}
```

更新说明字符串支持 `<br/>`。

## 10. 输入文件格式

### 10.1 帧边界

串口和 `.bin` 文件使用：

```text
0x3C <data> 0x3E
```

数据中的 `0x3C`、`0x3E`、`0x3D` 必须转义：

| 原始字节 | 转义后 |
|----------|--------|
| `0x3C` | `0x3D 0xC3` |
| `0x3E` | `0x3D 0xC1` |
| `0x3D` | `0x3D 0xC2` |

反转义规则为 `0x3D` 后面的字节取反：`原始字节 = 0xFF - 转义字节`。

### 10.2 帧内数据

反转义后的数据布局：

```text
[dlen 2B LE][ts 4B LE][phr_mcs 1B][option 1B][channel 1B][isRF 1B][MPDU...]
```

| 字段 | 说明 |
|------|------|
| `dlen` | `MPDU 长度 + 4`，读取端不校验 |
| `ts` | NTB tick，小端 32 位，40 ns/tick，约 171.8 秒回绕 |
| `phr_mcs` | HRF 物理层 MCS/调制信息 |
| `option` | 物理层选项 |
| `channel` | HRF 信道号或 PLC 频段 |
| `isRF` | `0` 表示 PLC，非 `0` 表示 HRF |
| `MPDU` | FCH 和后续 PB/载荷 |

### 10.3 `.bin` 时间标签

`.bin` 文件头部或分段前可插入一个独立的 8 字节 BCD 时间标签：

| 字节 | 内容 |
|------|------|
| 0 | 年减 2000 |
| 1 | 月 |
| 2 | 日 |
| 3 | 时 |
| 4 | 分 |
| 5 | 秒 |
| 6 | 毫秒百位 |
| 7 | 毫秒低两位 |

### 10.4 裸 hex 文本

- 独立时间行必须能解析为 `yyyy-MM-dd HH:mm:ss[.zzz]`，推荐使用 `TIME:` 前缀。
- 帧行必须是一条完整、带 `0x3C` 和 `0x3E` 的原始帧。
- 支持 `0x01 0xd5` 和 `01 d5` 两种字节写法。
- 旧版无 `0x3C/0x3E` 哨兵的裸 MPDU 行不再兼容，会被忽略。

## 11. 常见问题

### 11.1 串口打不开

- 确认 COM 口未被其他程序占用。
- 确认 STA 调试串口已连接。
- 默认波特率为 `460800`。
- 当前版本实际串口格式为 `8-N-1`。

### 11.2 文件回放没有记录

- `.bin` 必须使用 `0x3C ... 0x3E` 帧格式。
- 裸 hex 文本每行必须包含完整哨兵。
- 检查文件是否为空，或数据是否被旧格式转换工具改写。

### 11.3 帧显示为 DROP

常见原因：

- FCH CRC24 错误
- PB 配置非法
- PB 块超出帧长
- 帧长过短
- 链路、NetID、TEI 或帧类型被解析过滤器拒绝

查看 `Destination` 或 `Info` 列中的拒绝原因。

### 11.4 Delta 异常

- 实时串口在长时间无报文或跨 171.8 秒 NTB 回绕后会标记新分段。
- 文件回放依赖 BCD/TIME 时间标签恢复绝对时间。
- 没有时间标签的旧文件会回退到本地时间。

### 11.5 协议字段无法高亮

- 该协议节点是分组节点，本身不对应字节。
- MSDU 跨多个 PB，字段跨 PB 边界，无法映射到当前帧的连续字节。
- 帧被丢弃或载荷不完整。

## 12. 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+E` | 开始 |
| `Ctrl+.` | 停止 |
| `Ctrl+P` | 切换暂停菜单项 |
| `Ctrl+L` | 清空 |
| `Ctrl+F` | 应用显示过滤器 |
| `Enter` | 在过滤器输入框中应用过滤器 |

当前版本的菜单暂停项与工具栏暂停按钮状态没有完全同步。需要可靠暂停时，优先使用工具栏“暂停/继续”按钮。

## 13. 当前版本限制

- 详细协议树重点支持 BEACON、SOF、ACK、COORD；SEARCH、SWITCH 可出现在列表中，但没有同等完整的专用载荷解析。
- 过滤器不支持括号。
- 导出始终导出全部帧，不导出“仅当前过滤结果”。
- 暂停期间新到达的帧不会缓存。
- 工具栏“设置”按钮在当前源码版本中未接线。
- 串口数据位、停止位和校验位界面选项当前未真正写入采集配置，实际按 8-N-1 工作。

## 14. 构建与打包

环境：

| 组件 | 版本 |
|------|------|
| Qt | 6.10.1 mingw_64 |
| MinGW | 13.1.0 |
| 构建 | qmake + mingw32-make |

构建：

```bash
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.10.1/mingw_64/bin:$PATH"
qmake BPLC_STA_Monitor.pro
mingw32-make -j4
```

产物：

```text
release/BPLC_STA_Monitor.exe
```

打包：

```bash
bash scripts/package.sh 1.0.15
```

产物：

```text
dist/BPLC_STA_Monitor_Setup_v1.0.15.exe
```

