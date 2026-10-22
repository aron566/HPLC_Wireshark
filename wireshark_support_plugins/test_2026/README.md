# GW_2026 (HDC 2.0) Wireshark 解析器测试

## 运行

```bash
# 1. 生成测试 pcap (需 python3)
python gen_gw2026_test_pcap.py        # 输出 test_gw2026.pcap (3 帧)

# 2. 回归: HDC 1.0 混合帧仍由 gw_2022 解析 (版本=0 就地解析)
tshark -r ../test_gw_mixed.pcap \
    -X lua_script:../packet-gw_2022.lua -X lua_script:../packet-gw_2026.lua \
    -T fields -e frame.number -e frame.protocols -e gw_2022.fc.dt

# 3. HDC 2.0 帧解析验证 (版本=1 委托给 gw_2026)
tshark -r test_gw2026.pcap \
    -X lua_script:../packet-gw_2022.lua -X lua_script:../packet-gw_2026.lua -V

# 字段级断言 (3 帧均应为 1/True/True/True)
tshark -r test_gw2026.pcap \
    -X lua_script:../packet-gw_2022.lua -X lua_script:../packet-gw_2026.lua \
    -T fields -e frame.number -e frame.protocols \
    -e gw_2026.fc.std_ver -e gw_2026.fc.fccs_ok -e gw_2026.pb.crc_ok \
    -e gw_2026.sh.msdu_crc_ok
```

(Windows 下 tshark 位于 `/c/Program Files/Wireshark/tshark.exe`;
两个 lua 必须同时加载 —— 版本分派入口在 packet-gw_2022.lua)

## 测试帧字节布局

每帧 = USER4 串口混合封装 + MPDU:

```
[phr_mcs 1B=0x07][option 1B=0x01][channel 1B=0x05][isRF 1B=0x00载波]   4B 媒介头(入口已剥离)
[FCH 16B  表4: DT/NID16bit(LE)/保留/可变区域9B/标准版本号@byte12高nibble=1/FCCS CRC24@13-15(LE)]
[物理块 136B: 头1B(END|START|seq) + 体(补零) + PBCS CRC24 3B(LE, 校验前 pbsz-3=头+体)]
[MAC 帧]
```

- **帧1** 单跳帧(表3 头4B: version=1/msg_type=0/MSDULen11bit/聚合位) +
  无线发现列表(表91: STA MAC 6B + 序号 1B + IE type0 站点属性14B(表92) +
  IE type3 邻居信道位图版(表102/103: StartTEI=8 12b + 位图1B=0x25 → TEI 8/10/13,
  combo0 = 8b 接收率×3) ) + MSDU CRC32 4B (校验 MSDU, 不含 4B 单跳头)
- **帧2** 标准 MAC 帧(表2 16B, 含 2.0 新增: 聚合标识(4,5)/发送帧序号(11,4,12)/链路标识符 byte15)
  + MMe 0x0011 Bitloading 训练请求(表104: 源TEI12b+目的TEI12b+分组大小1B) +
  ICV CRC32 (校验 MSDU 含 MMe, 不含 16B MAC 头)
- **帧3** 标准 MAC + MMe 0x0013 分组请求(表109: 保留8B + 邻居个数 +
  邻居表110 每条 3B: TEI12b+类型1b+RSV3b+PLC质量4b+RF质量4b) + ICV

## 已验证结果 (tshark 实测)

- 帧1-3 `frame.protocols = gw_2022:gw_2026`, `gw_2026.fc.std_ver = 1`
- 3 帧 FCCS OK / PB CRC24 OK; 帧1 单跳 MSDU CRC32 OK; 帧2/3 ICV OK; 0 Malformed
- 帧1 位图 IE 解析出 Neighbor[0..2] 接收率 90/75/60%
- 帧3 GroupReq 邻居 TEI 291(0x123)/564(0x234), 类型=组成员(0)
- 回归: test_gw_mixed.pcap 两帧仍为 `gw_2022` 且 `gw_2022.fc.dt=1`, 无 Lua 错误

---

# NW_2026 (南网 SPLC) Wireshark 解析器测试

## 运行

```bash
# 1. 生成测试 pcap (需 python3)
py -3 gen_2026_test_pcap.py       # 输出 test_2026_splc.pcap(4帧,149) + test_2026_rf.pcap(1帧,150)

# 2. 回归: BPLC 帧仍由 nw_2021 解析 (标准版本号=1 不委托), 无 Lua 错误
tshark -r ../test_nw_frames.pcap \
    -X lua_script:../packet-nw_2021.lua -X lua_script:../packet-nw_2026.lua \
    -T fields -e frame.number -e nw_2021.beacon.bpc -e nw_2021.mme.type -e nw_2026.sof.pb_num

# 3. SPLC 帧解析验证 (标准版本号=2 整帧委托给 nw_2026)
tshark -r test_2026_splc.pcap -X lua_script:../packet-nw_2021.lua -X lua_script:../packet-nw_2026.lua -V

# 4. 字段级断言
# splc: 帧1 beacon.plc_cap=1; 帧2-4 sof.pb_num=1/1/2(多块重组); 帧3 mme.type=0x00a0; ICV 全 True
tshark -r test_2026_splc.pcap \
    -X lua_script:../packet-nw_2021.lua -X lua_script:../packet-nw_2026.lua \
    -T fields -e frame.number -e nw_2026.beacon.plc_cap -e nw_2026.sof.pb_num \
    -e nw_2026.mme.type -e nw_2026.mac.icv_ok
# rf:   单跳帧 mac.version=2 / msg_type=2(表214) / IE len=14(表217) / ICV True
tshark -r test_2026_rf.pcap \
    -X lua_script:../packet-nw_2021.lua -X lua_script:../packet-nw_2026.lua \
    -T fields -e nw_2026.mac.version -e nw_2026.sh.msg_type -e nw_2026.rf.ie.len -e nw_2026.mac.icv_ok
```

(两个 lua 必须同时加载 —— 版本分派入口在 packet-nw_2021.lua 的 nw.dissector,
FCH byte12 高 nibble==2 时整帧委托; packet-nw_2026.lua 不注册 wtap_encap)

## 测试帧字节布局

载波帧 = 裸 MPDU (USER2/linktype 149), 无线帧 = 裸 MPDU (USER3/linktype 150):

```
[FCH 16B  表16: 帧类型(0,0,3)/ConInd(0,3,1)/SNID(0,4,4)+按帧型高位合并5bit/
 标准版本号(12,4,4)=2 / FCCS CRC24@13-15(LE, 校验前13B)]
载波 SOF(表23): 多站点(4,0,1)=0/训练(4,1,1)=0/PB个数(4,2-5)/Bitloading(6,0)/
 TMI编号(6,1-5)/帧长(8,0,12)/SNID高位(12,0,1); 无线 SOF(表131): LID(4,0,8)/
 帧长(5,0,12)/载荷PB大小(6,4,4)/MCS(7,0,4)/重传(7,5,1)
[PB 块: PB头4B + 块体(pbsize-8) + 保留1B + CRC24 3B(LE存块尾, 覆盖块前 pbsize-3 字节)]
[MAC 帧: 短头12B/长头32B(版本 bits1-2=1) 或 单跳4B(版本 bits1-2=2) + ICV CRC32 4B]
信标帧载荷(表51 固定头6B: PLC能力指示(2,6,2)/无线信道byte3/通道数(4,0)/
 网络版本号(4,1,3)/信标机制(4,4)/发现信标使能(4,5)) + 管理条目 + BPCS CRC32 + 保留 + PB CRC24
```

- **帧1** 信标(TMI=2→136B, 表51 PLC能力=1/网络版本=1/发现信标使能=1, 站点能力条目)
- **帧2** SOF 单站点 1×520B(TMI编号=0), 短 MAC 头 12B + APP 命令帧(BID=0x05)
- **帧3** SOF 单站点 1×520B, 长 MAC 头 32B + MMe 0x00A0 物联终端接入请求(表39)
- **帧4** SOF 单站点 2×520B 多块重组, 短 MAC 头 + APP
- **rf帧1** 无线 SOF(PBLen=3→136B, MCS=5), 单跳 MAC 帧(表113) +
  无线发现列表(表214: STA MAC + 序号 + IE type0 站点属性 14B(表217)) + ICV

## 已验证结果 (tshark 实测)

- splc 4 帧 / rf 1 帧全部 FCCS OK / PB CRC24 OK / BPCS OK / ICV OK, 0 Malformed, 0 Lua 错误
- 帧4 1056B(16+2×520) 块体重组出完整 APP 帧; rf 帧单跳发现列表 IE 解析出
  代理 TEI=1/层级=1/RF跳数=2
- 回归: test_nw_frames 等既有 pcap 中 BPLC 帧仍为 nw_2021.* 字段, nw_2026.* 计数 0,
  无 Lua 错误
