# 实现思路说明

## 整体架构

```
bin/裸hex/实时串口 ──► pcap (USER DLT) ──► Wireshark ──► Lua dissector
```

数据源统一转成 pcap，Wireshark 读入后映射为 internal encap，Lua dissector 挂在对应
encap 上自动解析。**抓包与解析解耦**：
- `bin2pcap.py` / `serial2pcap.py` 负责把监控器产物/串口流转成标准 pcap，并按帧结构
  投票识别国网/南网协议（信标/SOF 的"预计帧长==实际帧长"只在一种解释下成立）
- `packet-gw_2022.lua`（国网）/ `packet-nw_2021.lua`(南网) 负责纯解析，不关心数据从哪来

两套协议用不同 USER 通道区分（避免同一 encap 上两个 dissector 抢占）：

| 协议 | 载波 | 无线 | 字段前缀 |
|---|---|---|---|
| 国网 GW_2022 | USER0 (encap 45, linktype 147) | USER1 (encap 46, 148) | `gw_2022.` |
| 南网 NW_2021 | USER2 (encap 47, 149) | USER3 (encap 48, 150) | `nw_2021.` |

## 字节序（整个协议的根基）

国网 HPLC 协议多字节字段是 **little-endian（小端）**，依据 BPLC 监控器权威代码：
`BitDefine.getdata` 的 `byte_data << (8*(byte_num-start_byte))` 与
`main.py` 的 `b_nid = data[2] | data[3]<<8 | data[4]<<16`。

三类字段的取值方式：

| 字段类型 | 例子 | 读法 |
|---|---|---|
| 字节内 bit 域 | DT(3bit)、标志位 | mask |
| 跨字节 bit 域 | TEI(12bit)、符号数(9bit) | `read_bits(tvb, start_bit, nbits)` |
| 整字节多字节 | NID/BTS/MSDU序列号 | `tvb(off,len):le_uint()` |

`read_bits` 按「字节内 bit0=LSB，跨字节连续」的 little-endian bit 序取值，
12bit 的 TEI 跨 1.5 字节时必须用它，不能按整字节 mask。

## 帧结构（5 层）

```
MPDU = FCH(16B) + 载荷
FCH  = 定界符类型DT(3b) + 网络类型(5b) + NID(24b) + 可变区域(68b) + 版本(4b) + FCCS(24b)
```

按 DT 分 4 类帧，各自载荷不同：

| DT | 帧型 | 载荷 |
|---|---|---|
| 0 | 信标 | 帧载荷(PBSize 由 TMI 查表) + PB CRC24 |
| 1 | SOF | 物理块头(1B) + MAC帧 + ICV |
| 2 | SACK | 可变区域即全部 |
| 3 | 网间协调 | 可变区域即全部 |

**信标帧的坑**：物理块大小 PBSize 由 FCH 字节9 高4bit 的 TMI 查表决定
（0/1→520, 2-6→136, 7-10→520, 11/12→264, 13/14→72），不是固定值。
信标条目长度字段是「头(1B)+长度字段(1/2B)+内容」的总和，内容长要减 2 或 3。

**MAC 帧**（SOF 载荷内）：标准帧头 16B（带 MAC 地址时 28B）+ MSDU + ICV(4B)。
MSDU 类型=0 是管理消息，按 MMTYPE(2B) 分发到 21 种消息体解析。

## 关键坑（踩过才记住）

1. **小端**：所有多字节字段 `le_uint`，跨字节 bit 域 `read_bits`。大端读会让
   NID/BTS/条目长度全错。
2. **TMI→PBSize**：信标载荷边界必须查表，不能假设固定 20B 头。
3. **条目长度语义**：长度字段值含头+长度字段自身，内容长 = len_raw-2/-3。
4. **非中央信标信息每条 2 字节**（不是 4 字节），表51 是 TEI+类型+RF标志=16bit。
5. **兼容性**：Lua 不用 bit 库/原生 `&`（定制版 WiresharkRenesas 没有），
   纯算术 `band`。USER DLT internal encap 是 45-60（不是 pcap 的 linktype 147）。

## 时间轴

- 帧内 ts 域 = NTB（25MHz，40ns/tick，约171.8s 回绕）
- **首帧时间 = 文件头 8B BCD 标注时刻**，后续帧 = 上一帧 + (本帧NTB-上一帧NTB)×40ns
- 帧间 NTB 差 > 60s（kMaxNtbGapTicks）→ 断点，不沿用上一帧

## 南网 NW_2021 与国网的关键差异（packet-nw_2021.lua）

1. **FCH**：帧类型(3b)+ConInd(1b)+SNID(4b)，无 24b NID；信标可变区域含 BPC(4B)，
   TMI 在字节10 高4bit（国网在字节9）；SOF 的 PB 个数/TMI 都在字节7。
2. **PB 块**：`PB头(4B) + 块体(PBSize-8) + 保留(1B) + CRC24(3B)`（国网是 1B 头）。
   多块时 MAC 帧跨块，用「块体逻辑视图」解析（见下节，与 Qt nw_2021_parser 的
   msdu_body 拼接语义一致）。
3. **MAC 帧**：标准帧头 32B长/12B短（MACHeadFlag bit0），单跳帧头 4B（版本 bits1-2=2）；
   MSDU 帧头 = 目的/源MAC(48b)+VLAN(32b)+类型(16b)，**VLAN==0x8100 → MMe**。
4. **MMe 头 6B**：MMVersion(8b)+MMType(16b)+RSV(24b)，17 种消息体。
5. **ACK 帧按扩展帧类型分流**（字节12 低4bit）：常规/网络搜索/同步/无线切频。
6. **BPCS 覆盖范围**：帧载荷区去掉 BPCS(4B)+保留字节(1B)，即 `crc32_le(tvb, off, pbsize-4)`
   覆盖到 BPCS 前；保留字节在 BPCS 之后、PB CRC24 之前。
7. **MSDU ICV**：覆盖 MSDU 载荷（MAC 帧头之后），与国网一致。
8. **信标条目长度**：只有时隙分配(0x02)用 2B 长度字段，其余 1B（国网是 0xC0+ 用 2B）。
9. **无线 PB 大小**：PBLen 0-5 → 16/40/72/136/264/520（表130）。

## 多物理块 MAC 帧的「块体逻辑视图」（两套解析器同机制）

1. **为什么不用 `ByteArray` 重组**：`ByteArray:tvb()` 会注册新数据源 → hex 窗口多出
   一个页签，点击 MAC 帧字段时切过去，原始帧里的 PB头/块尾 CRC"消失"。
2. **机制**：`make_block_view(tvb, base, pbsz, n)` 把"重组坐标"（各块块体拼接）映射回
   原始帧坐标。逐字节读取（1B Range）总是字节精确；`(off,len)` 跨块时高亮范围顺带
   覆盖块间字节。GW：body=pbsz-4、base=17；NW：body=pbsize-8、base=20。
   `view_slice(v, delta)` 做坐标平移的子视图（替代 `range:tvb()` 子集）。
3. `add_le` 与 MAC 路径的隐式 `le_uint` 读取全部改为 `read_bits` 逐字节 —— 跨块读取
   仍正确；ICV/PB CRC 校验按重组语义精确。
4. `add_span`：跨块数据字段（APP载荷/填充/单跳载荷/TEI位图/厂家信息/诊断）按块拆成
   多个同名树项，每项只高亮该块内的数据字节（排除块间 PB头/CRC）。
5. GW 单块直接用原帧（绝对偏移 17）；NW 单/多块统一走视图。

## 混合媒介 DLT（USER4/USER5，串口采集用）

pcap 的 linktype 全文件唯一 → 纯 MPDU 格式无法载波/无线混采。串口采集记录保留 4B
媒介头 `[phr_mcs][option][channel][isRF]`：GW=USER4(151/encap49)、NW=USER5(152/encap50)，
解析器**逐帧读 isRF 自选载波/无线分支**（南网信标/SOF 两套字段坐标），协议列逐帧
显示 HPLC/RF；pcap 头可立写（不再等首帧定媒介）。旧纯 MPDU DLT（147-150）行为不变。

## extcap 串口采集的设计坑（BPLC_Serial_Capture.bat + bplc_serial_extcap.py）

1. **stderr＝错误弹窗**：Wireshark 把 extcap 的任何 stderr 输出当错误弹出 → 运行日志
   必须落文件（`bplc_extcap_debug.log`），stderr 只留真错误（如串口打不开的指引）。
2. **bat 包装的孤儿进程**：停止时 Wireshark 只杀 bat（cmd.exe），python 孙进程会孤儿化
   继续占用串口 → 父进程看门狗（`OpenProcess`+`WaitForSingleObject`，随 0.5s 串口读
   超时周期检查，父进程已死即退出释放）。
3. **com0com 虚拟串口枚举**：pyserial `comports()` 看不到（挂在 CNCPorts 设备类）→
   读 `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` 注册表补充；虚拟口成对（如 COM90/COM91），
   监控器占一端发送，抓包选另一端。
4. bat 内注释必须纯 ASCII（GBK 代码页会把 UTF-8 中文 rem 行当命令执行而报错）。

## 列显示

- **Source/Destination 列**：`pinfo.cols.src/dst`，按 DT 提取 TEI（CCO=TEI1、
  广播=TEI4095、STA=TEIn）
