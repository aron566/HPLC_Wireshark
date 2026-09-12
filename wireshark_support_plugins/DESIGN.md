# 实现思路说明

## 整体架构

```
bin/裸hex/实时串口 ──► pcap (USER DLT) ──► Wireshark ──► Lua dissector
```

数据源统一转成 pcap（linktype=147 DLT_USER0），Wireshark 读入后映射为 internal
encap 45，Lua dissector 挂在该 encap 上自动解析。**抓包与解析解耦**：
- `bin2pcap.py` 负责把监控器产物转成标准 pcap
- `packet-hplc_rf.lua` 负责纯解析，不关心数据从哪来

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

## 列显示

- **Source/Destination 列**：`pinfo.cols.src/dst`，按 DT 提取 TEI（CCO=TEI1、
  广播=TEI4095、STA=TEIn）
