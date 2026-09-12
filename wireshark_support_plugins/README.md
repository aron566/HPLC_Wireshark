# Wireshark 解析支持插件

> 本目录随 **BPLC_STA_QtMonitor** 工程一起维护,是国网《双模通信互联互通技术规范
> 第4-2部分:数据链路层通信协议》的 Wireshark 解析器及配套抓包转换工具
> (由 `monitor/HPLC_HRF_Wireshark` 拷贝入库)。

国网《双模通信互联互通技术规范 第4-2部分：数据链路层通信协议》的 Wireshark 解析器。
南网 CSG 协议链路层与国网一致（差异在物理层），本解析器直接兼容。

## 文件

| 文件 | 说明 |
|---|---|
| `packet-hplc_rf.lua` | **Lua dissector（推荐，即装即用）** |
| `packet-hplc_rf.c` | C 插件源（已落后于 Lua 版，勿用） |
| `bin2pcap.py` | BIN 回放文件 → pcap |
| `serial2pcap.py` | **实时串口抓包 → pcap** |
| `gen_test_pcap.py` / `gen_assoc_req_test.py` | 测试帧生成 |
| `DESIGN.md` | 实现思路说明 |

## 快速使用

### 离线回放（bin 文件）

```
python bin2pcap.py BPLC_xxx.bin
```

生成同名 `.pcap`，用 Wireshark 打开。

### 实时串口抓包

```
python serial2pcap.py COM8 460800 capture.pcap
```

边抓边落 pcap，可同时用 Wireshark 打开实时查看。

### 解析

Wireshark GUI：把 `packet-hplc_rf.lua` 复制到 `%APPDATA%\Wireshark\plugins\`，重启即自动加载。
命令行：`tshark -r capture.pcap -X lua_script:packet-hplc_rf.lua -V`

## 英文显示

字段语言**自动跟随 Wireshark 界面语言**（Edit → Preferences → Appearance → Language），无需额外设置：

- 界面语言设英文 → 字段名/value_string/树节点全部英文
- 界面语言设中文（或跟随系统中文）→ 全部中文

实现原理：Wireshark 4.x 把界面语言存在 `%APPDATA%\Wireshark\language` 文件（内容 `language: en`），
dissector 加载时读取该文件决定字段名。

> 环境变量 `HPLC_RF_LANG=en` / `=zh` 可强制覆盖（优先于界面语言）。

## 抓包文件格式要求

- 封装类型 = **USER DLT**（`USER 0` ~ `USER 15`），internal encap = 45~60
- 帧内容 = raw_wire 原样字节流（0x3C 帧流，不含 BIN 回放文件头）

## 当前覆盖

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
| ICV / FCCS / BPCS CRC 校验 | ❌ 仅显示原始值，未做校验判断 |

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

## 兼容性

脚本不依赖 bit/bit32 库，不用原生 `&` 运算符（纯算术 `band`），兼容标准 Wireshark 和定制版（WiresharkRenesas 等）。

## 下一步（可选）

- 补 ICV CRC32 / FCCS CRC24 / BPCS CRC32 校验判断（算法在 BPLC_STA 监控器现成，可移植）
