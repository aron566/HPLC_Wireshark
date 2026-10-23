# Wireshark 解析支持插件

> 本目录随 **BPLC_STA_QtMonitor** 工程一起维护,包含国网/南网两套双模协议的
> Wireshark 解析器及配套抓包转换工具 (国网版由 `monitor/HPLC_HRF_Wireshark` 拷贝入库)。

两套独立解析器,可同时加载、互不冲突:

- **packet-gw_2022.lua** — 国网《双模通信互联互通技术规范 第4-2部分:数据链路层通信协议》
- **packet-nw_2021.lua** — 南网双模 2021 报批版 (链路层与国网差异较大: FCH 为
  SNID 4b 而非 NID 24b、SOF 物理块含 4B PB 头、MAC 帧头 32B/12B + MSDU 帧头
  MAC48b+VLAN、管理消息为 MMe 6B 头)

两套解析器的过滤器字段名分别带 `gw_2022.` / `nw_2021.` 前缀,显示/过滤/着色互不影响;
抓包文件按 pcap linktype 区分协议(见 [抓包文件格式要求](#抓包文件格式要求))。

## 文件

| 文件 | 说明 |
|---|---|
| `packet-gw_2022.lua` | **国网 GW_2022 Lua dissector（即装即用，HDC 1.0；入口含版本分派）** |
| `packet-gw_2026.lua` | **国网 GW_2026 / HDC 2.0 Lua dissector（由 gw_2022 按版本号委托进入）** |
| `packet-nw_2021.lua` | **南网 NW_2021 Lua dissector（BPLC；入口含版本分派）** |
| `packet-nw_2026.lua` | **南网 NW_2026 / SPLC 2026 Lua dissector（由 nw_2021 按版本号委托进入；另：version≠2 但 PB 头聚合标志=1 的载波 SOF 聚合 BPLC 帧也委托进入，表20 布局 + 级联 MAC 帧拆分）** |
| `test_2026/` | 新协议测试帧生成器与 pcap（GW: gen_gw2026_test_pcap.py；NW: gen_2026_test_pcap.py） |
| `bin2pcap.py` | BIN 回放文件 → pcap（协议自动识别，`--gw`/`--nw` 可强制） |
| `serial2pcap.py` | **实时串口抓包 → pcap**（`--gw`/`--nw` 可强制协议） |
| `BPLC_Serial_Capture.bat` + `bplc_serial_extcap.py` | **Wireshark 原生串口采集（extcap）** |
| `gen_test_pcap.py` / `gen_assoc_req_test.py` | 国网测试帧生成 |
| `gen_nw_test_pcap.py` | 南网测试帧生成（含完整 FCCS/PB CRC24/BPCS/ICV 校验值） |
| `test_nw_bplc_agg.pcap` | 聚合 BPLC 帧回归（帧1=聚合 v1 → nw_2026 5 条级联 / 帧2=非聚合 → nw_2021） |
| `DESIGN.md` | 实现思路说明 |

## 新旧协议版本自适应委托

四个 Lua 文件构成两套"入口 + 委托"链（与上位机 C++ 解析器同架构）：

```
USER0/1/4 ──► packet-gw_2022.lua ──(FCH 标准版本号 byte12 高nibble)──┬─ 0(1.0) → 内联旧布局解析
                                                                    └─ 1/保留(2.0) → 整帧委托 gw_2026
USER2/3/5 ──► packet-nw_2021.lua ──(FCH 标准版本号 byte12 高nibble)──┬─ 0/1(BPLC) → 聚合例外:载波且
                    （聚合标志=表47 PB头 byte2 bit0）                │   PB头聚合标志=1 → 委托 nw_2026(表20+级联拆分)
                                                                    └─ 其余(非聚合) → 内联旧布局解析
                                                                    └─ 2(SPLC) → 整帧委托 nw_2026
```

- 委托在**媒介剥离前**发生（原始 tvb 连同 USER4/USER5 的 4B 媒介头一并传入），媒介剥离与
  载波/无线判定由新协议文件自行完成（`gw_2026.*` / `nw_2026.*` 媒介头字段），避免 is_rf 信息
  在委托中丢失
- 新协议文件不注册 wtap_encap，只能经委托进入；旧协议入口对"新文件未加载"有兜底提示
- 版本字段受 FCH FCCS CRC24 保护，链路噪声不会造成误派
- GW:版本 0=HDC 1.0 / 1=HDC 2.0 / 保留值按 2.0 尝试；NW:表16 0 保留 / 1=BPLC / 2=SPLC，
  保留值保守归旧解析（与上位机 GW_2026_Parser / NW_2026_Parser 策略一一对应）
- 测试：`test_2026/` 下的生成器可构造 2.0 帧（含单跳 RFDL/位图 IE/MMe 0x0011/0x00A0 等），
  既有 1.0 pcap 回归无变化

## 快速使用

### 离线回放（bin 文件）

```
python bin2pcap.py BPLC_xxx.bin        # 协议自动识别（按帧结构投票，平票回退国网）
python bin2pcap.py --nw BPLC_xxx.bin   # 强制南网 NW_2021
python bin2pcap.py --gw BPLC_xxx.bin   # 强制国网 GW_2022
```

生成同名 `.pcap`，用 Wireshark 打开。

### 实时串口抓包

```
python serial2pcap.py COM8 460800 capture.pcap    # 自动识别协议（可用 --gw/--nw 强制）
```

边抓边落 pcap，可同时用 Wireshark 打开实时查看。

### 在 Wireshark 里直接抓串口（extcap，推荐）

把 `BPLC_Serial_Capture.bat` 和 `bplc_serial_extcap.py` 复制到 `%APPDATA%\Wireshark\extcap\`
（需 python + pyserial 在 PATH），重启 Wireshark 后捕获列表出现两个接口：

- **NW_2021/NW_2026_Capture (南网串口, BPLC+SPLC)** → 自动用 `packet-nw_2021.lua` 解析（USER2/3；
  聚合 BPLC 帧经版本分派由 `packet-nw_2026.lua` 解析）
- **GW_2022/GW_2026_Capture (国网串口, HDC1.0+2.0)** → 自动用 `packet-gw_2022.lua` 解析（USER0/1）

选中接口后可配置：**串口号**（自动枚举 COM 口，含 com0com 虚拟串口）、**波特率**
（默认 460800，9600~921600），点"开始"即可实时采集解析。
采集记录带 4B 媒介头（USER4/USER5 格式），解析器**逐帧按 isRF 判断载波/无线**，
两种帧混合出现都能正确解析。时间轴与 serial2pcap 一致（首帧=本地时刻，后续=NTB 差×40ns）。

### 解析（重要：新旧协议各加载两个脚本）

Wireshark GUI：把 4 个 Lua 文件（`packet-gw_2022.lua` / `packet-gw_2026.lua` /
`packet-nw_2021.lua` / `packet-nw_2026.lua`）复制到 `%APPDATA%\Wireshark\plugins\`，
重启即自动加载。
命令行（**每套协议的新旧两个脚本必须同时加载**——2.0/SPLC 帧由入口按版本号委托给新
协议文件，只加载旧脚本会在 2.0 帧上显示兜底警告并按 1.0 布局尽力解析）：

```
tshark -r capture.pcap -X lua_script:packet-gw_2022.lua -X lua_script:packet-gw_2026.lua -V
tshark -r capture.pcap -X lua_script:packet-nw_2021.lua -X lua_script:packet-nw_2026.lua -V
```

版本分派规则（FCH 标准版本号 byte12 高 nibble）：国网 0=HDC 1.0 内联 / 1 及保留值委托
`gw_2026`；南网（用户决策 2026-10 分派策略）：**仅 SOF 帧按标准版本号分派**（1=BPLC 内联 /
2=SPLC 委托 `nw_2026`）；**聚合例外（用户需求）：载波 SOF version≠2 但 PB 头聚合标志=1
（表47 byte2 bit0）的聚合 BPLC 帧也委托 `nw_2026`**（nw_2021 无聚合拆分能力，由 nw_2026 按
BPLC 表20 布局 + 级联 MAC 帧拆分解析，实测 5 条级联 APP 报文逐条展示）；**信标/ACK/COORD 不判版本号默认委托 `nw_2026`**（SPLC 是 BPLC 的
保留位启用补充、兼容旧帧：信标 表119→表18 / ACK 表7→表30 / COORD 表15→表41 均为保留位
启用无冲突），nw_2026 解析异常时 pcall 回退内联旧布局解析。详见上文
"新旧协议版本自适应委托"。

### 多物理块帧与字节高亮行为

MSDU 跨多个物理块时（块尾 CRC24 与下块 PB 头夹在 MAC 帧中间）：

- hex 窗口**始终显示完整原始帧**（单页签，PB 头/块尾 PBCRC 都在，不切换、不删字节）
- 点击「物理块 N」→ 高亮该块全部字节（含块头与 CRC）
- MAC 帧/APP 载荷等字段 → 高亮原帧对应数据字节；**跨块数据字段按块拆成多个同名树项**，
  每项只高亮该块内的数据段（不覆盖块间 PB头/CRC）
- 字段值与 ICV/PB CRC 校验按"块体拼接"的重组语义逐字节精确计算

## 英文显示

字段语言**自动跟随 Wireshark 界面语言**（Edit → Preferences → Appearance → Language），无需额外设置：

- 界面语言设英文 → 字段名/value_string/树节点全部英文
- 界面语言设中文（或跟随系统中文）→ 全部中文

实现原理：Wireshark 4.x 把界面语言存在 `%APPDATA%\Wireshark\language` 文件（内容 `language: en`），
dissector 加载时读取该文件决定字段名。

> 环境变量可强制覆盖（优先于界面语言）：国网 `HPLC_RF_LANG=en` / `=zh`，南网 `NW_2021_LANG=en` / `=zh`。

## 抓包文件格式要求

- 封装类型 = **USER DLT**，帧内容 = raw_wire 原样字节流（0x3C 帧流，不含 BIN 回放文件头）
- 两套协议用不同 USER 通道区分（`bin2pcap.py` / `serial2pcap.py` 按协议自动选择）：

| 协议 | 载波（HPLC） | 无线（RF） | 解析器 |
|---|---|---|---|
| 国网 GW_2022 | USER0 = linktype 147 / encap 45 | USER1 = linktype 148 / encap 46 | `packet-gw_2022.lua` |
| 南网 NW_2021 | USER2 = linktype 149 / encap 47 | USER3 = linktype 150 / encap 48 | `packet-nw_2021.lua` |
| 国网 GW_2022 **混合**(串口采集) | USER4 = linktype 151 / encap 49,记录 = 媒介头4B `[phr_mcs][option][channel][isRF]` + MPDU | 同左 | `packet-gw_2022.lua` |
| 南网 NW_2021 **混合**(串口采集) | USER5 = linktype 152 / encap 50,同上 | 同左 | `packet-nw_2021.lua` |

混合格式下解析器**逐帧读媒介头 isRF 自选载波/无线分支**(协议列逐帧显示 HPLC/RF),
一个抓包里载波帧与无线帧混排均可正确解析; extcap 串口采集即用此格式。

## 当前覆盖

### 国网 GW_2022（packet-gw_2022.lua）

| 层 | 状态 |
|---|---|
| MPDU 帧控制 16B（表13） | ✅ 完整 |
| DT 分流（信标/SOF/SACK/网间协调） | ✅ 可变区域完整 |
| 信标帧载荷 + 信标管理信息条目（表38/44-57） | ✅ 完整 |
| 物理块头（表37） | ✅ 完整 |
| 标准/单跳 MAC 帧头（表4/11） | ✅ 完整 |
| 管理消息体（21 种 MMTYPE，表60-122） | ✅ 完整 |
| 无线发现列表（表123-137，TLV） | ✅ 完整 |
| 源/目的地址列（Source/Destination） | ✅ 完整 |
| 中英双语显示 | ✅ 完整 |
| ICV / FCCS / BPCS CRC 校验 | ✅ 完整(原始值 + 计算值 + 校验通过标志) |

### 南网 NW_2021（packet-nw_2021.lua）

| 层 | 状态 |
|---|---|
| MPDU 帧控制 16B（帧类型/ConInd/SNID + 各帧型可变区域） | ✅ 完整 |
| ACK 扩展帧类型（常规/网络搜索/同步/无线切频） | ✅ 完整（10-12 南网扩展暂缓，与 Qt 监控器一致） |
| 信标帧载荷（固定头 6B + 管理信息 6 种条目 + BPCS + 保留字节） | ✅ 完整 |
| SOF 物理块（4B PB 头 + 块体 + 保留 + CRC24，多块重组 MAC 帧） | ✅ 完整 |
| MAC 帧头（长 32B / 短 12B / 单跳 4B） + MSDU 帧头（MAC48b + VLAN + 类型） | ✅ 完整 |
| MMe 管理消息体（17 种 MMType） | ✅ 完整（0x0083/0x0084 暂缓，与 Qt 监控器一致；0x00A0-0x00A2 物联终端接入已实现） |
| APP 应用层报文（通道控制信息 + 业务报文头 + BID 释义） | ✅ 完整 |
| 源/目的地址列（含 TEI↔MAC 学习） | ✅ 完整 |
| 中英双语显示 | ✅ 完整 |
| FCCS / PB CRC24 / BPCS / MSDU ICV 校验 | ✅ 完整(原始值 + 计算值 + 校验通过标志) |

### 国网 GW_2026 / HDC 2.0（packet-gw_2026.lua，经 gw_2022 入口版本委托进入）

| 层 | 状态 |
|---|---|
| MPDU 帧控制 16B（表4：NID 16bit + 标准版本号） | ✅ 完整 |
| SOF 可变区域（表7：帧类型分派 数据/信道探测/OFDMA/TDA）+ PB 大小（表8） | ✅ 完整 |
| SACK（表14，68bit 位流） | ✅ 完整 |
| 信标（表5 + 新条目 0x06/0x07/0x08/0xC1/0xC2） | ✅ 完整 |
| 标准/单跳 MAC 帧头（表2 新3字段 / 表3 含聚合标识） | ✅ 完整 |
| 单跳消息类型 0/1/2/128/129（表3） | ✅ 完整 |
| 无线发现列表（表91-103：站点属性/路由/邻居信道非位图+位图/BRUSNR32） | ✅ 完整 |
| 载波发现列表（表62/63-68/76/77，与无线同构，组合 10/11 含 BRU） | ✅ 完整 |
| 信道评估参数更新（表106-108：BRU SNR/SAT 配置/子载波选择/比特加载） | ✅ 完整 |
| MMe 管理消息体（表43：0x0011-0x0014 Bitloading/分组等） | ✅ 完整 |
| SNR/RSSI 量化渲染（无线 表97/98/99/100/101，载波 表70/71/72/73/74） | ✅ 完整 |
| 中英双语 + FCCS/PB/MSDU CRC/ICV 校验 | ✅ 完整 |

### 南网 NW_2026 / SPLC 2026（packet-nw_2026.lua，经 nw_2021 入口版本委托进入）

| 层 | 状态 |
|---|---|
| MPDU 帧控制 16B（表16：ConInd/SNID 4b+高位按帧型合并 5bit/版本） | ✅ 完整 |
| SPLC SOF（表23-26：单站点/多站点 DL-OFDMA eFC 16B→PB 基址 32/训练帧） | ✅ 完整 |
| 无线 SOF（表131：帧长/载荷 PB 大小/MCS/重传） | ✅ 完整 |
| PB 三张查表（BPLC TMI / SPLC TMI编号 / 无线 PBLen；PB 块 4B 头+体+保留+CRC24） | ✅ 完整 |
| 信标（表51 增量：PLC 能力/网络版本号等） | ✅ 完整 |
| MAC 帧头（长 32B/短 12B/单跳 4B，表6 版本分派）+ 无线发现列表（表214-227） | ✅ 完整 |
| MMe 含物联终端接入 0x00A0-0x00A2（表39/40/42/243/244/246） | ✅ 完整 |
| 源/目的地址列（含 TEI↔MAC 学习）+ 中英双语 + FCCS/PB/ICV 全部 CRC 校验 | ✅ 完整 |

## 关键实现约定（字节序）

**整个协议多字节字段是 little-endian（小端）**。依据 BPLC 监控器权威代码：
`BitDefine.getdata` 的 `byte_data << (8*(byte_num-start_byte))` 与
`main.py` 的 `b_nid = data[2] | data[3]<<8 | data[4]<<16`。

- **bit 序**：字节内 bit0 = LSB，跨字节连续。`read_bits(tvb, start_bit, nbits)` 按绝对 bit 偏移取值。
- **12bit 字段**（TEI 等）跨字节，用 `read_bits` + 显式值添加。
- **整字节多字节字段**（NID/BTS/MSDU序列号）小端，`tvb(off,len):le_uint()`。
- **时间戳（BTS/NTB）**：NTB（25MHz，40ns/tick，约171.8s回绕）。换算秒 = NTB/25,000,000。
- **信标物理块大小**：由 FCH 字节9 高4bit TMI 查表（0/1→520, 2-6→136, 7-10→520, 11/12→264, 13/14→72）。
- **信标条目长度**：长度字段值含头+长度字段自身，内容长 = len_raw-2（普通）/ len_raw-3（0xC0）。

## 常见问题（FAQ）

**1. 升级后启动报 `"hplc_rf.xxx" is not a valid protocol field`**
`%APPDATA%\Wireshark\colorfilters` 里还是旧字段名。把 `hplc_rf.*` 改成 `gw_2022.*`（国网）
并按需补 `nw_2021.*` 规则（本仓库现机已配置好，可作参考）。

**2. 旧插件与新版抢占 DLT / 信标帧报 Lua 越界错误**
删除或改名 `%APPDATA%\Wireshark\plugins\packet-hplc_rf.lua`（旧名已退役，改为
`packet-gw_2022.lua`）。保留旧文件会抢占 encap 45-60 并在新 USER2/3 上抢先解析。
旧 extcap 采集脚本同理：`hplc_rf_extcap.py` / `hplc_rf_extcap.bat.bak` 已在 extcap 目录
改名 `.bak` 退役（其接口名与现接口混杂），如需恢复去掉后缀即可。

**3. extcap 报"无法打开串口"（PermissionError）**
端口被其他程序占用（监控器/串口助手/另一次抓包），或 **com0com 虚拟串口选了数据发送端** ——
虚拟口成对（如 COM90/COM91），监控器占一端发送，抓包选**配对的另一端**。

**4. 串口下拉看不到 COM90/COM91**
pyserial `comports()` 枚举不到 com0com 虚拟口（自定义设备类）；脚本已加
SERIALCOMM 注册表补充枚举，插上设备即可见。

**5. 停止抓包时弹 "Error from extcap pipe"**
旧版把运行信息写到 stderr（Wireshark 把 extcap 的任何 stderr 输出当错误弹窗）。
已改为写入 `%APPDATA%\Wireshark\extcap\bplc_extcap_debug.log`，stderr 只留真错误。

**6. 停止后再开始，串口打不开**
旧版停止时 Wireshark 只杀 bat（cmd.exe），python 孙进程孤儿化继续占串口。
已加父进程看门狗（0.5s 内检测并退出释放串口）。

**7. 多物理块帧 ICV 校验失败 / APP 载荷里混入怪字节**
旧版对 MAC 帧按单块线性解析（块间 CRC/头被当数据）。已改"块体逻辑视图"重组语义，
多块帧 ICV/载荷/填充全部正确。

## 兼容性

脚本不依赖 bit/bit32 库，不用原生 `&` 运算符（纯算术 `band`），兼容标准 Wireshark 和定制版（WiresharkRenesas 等）。

## 下一步(可选)

(暂无——ICV/FCCS/BPCS CRC 校验已实现)
