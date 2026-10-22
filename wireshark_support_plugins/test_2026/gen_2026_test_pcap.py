# -*- coding: utf-8 -*-
"""构造南网 SPLC 2026 (NW_2026) 测试 pcap, 验证 packet-nw_2026.lua dissector.

帧结构(全部 SPLC, FCH byte12 高 nibble 标准版本号=2, 由 packet-nw_2021.lua 版本分派
整帧委托 packet-nw_2026.lua):
  FCH 16B: 帧类型(0,0,3)/ConInd(0,3,1)/SNID(0,4,4) 4b+按帧型高位合并 5bit /
           标准版本号(12,4,4)=2 / FCCS CRC24(前 13B, 存 13-15)
  载波 SOF(表23, 单站点): 多站点帧标识(4,0,1)=0 / 训练帧标识(4,1,1)=0 /
           PB个数(4,2-5) / Bitloading(6,0) / TMI编号(6,1-5) / 帧长(8,0,12) / SNID高位(12,0,1)
  PB 块: PB头4B + 块体(pb_size-8) + 保留1B + CRC24 3B(小端存块尾, 覆盖块前 pb_size-3 字节)
  MAC 帧: 短头 12B(版本 bits1-2=1) / 长头 32B / 单跳 4B(版本 bits1-2=2); 尾 ICV CRC32
  无线 SOF(表131): LID(4,0,8)/帧长(5,0,12)/载荷PB大小(6,4-7)/MCS(7,0-4)/重传(7,5,1), 单块
  信标载荷固定头(表51): PLC能力指示/无线信道/通道数/网络版本号/信标机制状态/发现信标使能

生成两个 pcap (linktype 为文件级属性, 载波/无线分开):
  test_2026_splc.pcap  (USER2, linktype 149): 信标 + SOF 短头APP + SOF 长头MMe(IoT 0x00A0)
  test_2026_rf.pcap    (USER3, linktype 150): SOF 无线 + 单跳帧(无线发现列表 表214)
"""
import struct
import os


def write_bits(buf, start_bit, nbits, value):
    """little-endian bit 序: 字节内 bit0=LSB, 跨字节连续."""
    for i in range(nbits):
        if (value >> i) & 1:
            abs_bit = start_bit + i
            buf[abs_bit // 8] |= (1 << (abs_bit % 8))


def crc24(buf):
    """poly=0xC60001, init=0, LSB-first; buf 含末尾 3B 校验位, 校验前 len-3 字节."""
    crc = 0
    for byte in buf[:-3]:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xC60001 if crc & 1 else crc >> 1
    return crc & 0xFFFFFF


def crc32(buf):
    """poly=0xEDB88320, init=0xFFFFFFFF, LSB-first, 末取反; buf 含末尾 4B 校验位."""
    crc = 0xFFFFFFFF
    for byte in buf[:-4]:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def fix_fccs(frame):
    """FCCS CRC24: 校验 FCH 前 13 字节(0-12), 存储值字节 13-15."""
    frame[13:16] = struct.pack('<I', crc24(frame[:16]))[:3]


def build_pcap(payloads, dlt):
    gheader = struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, dlt)
    out = gheader
    for p in payloads:
        out += struct.pack('<IIII', 0, 0, len(p), len(p)) + bytes(p)
    return out


SNID = 5
CCO_MAC = bytes.fromhex('001122334455')
STA_MAC = bytes.fromhex('001122334466')
VER2 = 2 << 4   # FCH byte12 高 nibble: 标准版本号 = 2 (SPLC)


def add_pb_crc24(frame, bstart, pbsize):
    """PB CRC24 @ 块尾 3B(小端), 覆盖块内前 pbsize-3 字节."""
    frame[bstart + pbsize - 3:bstart + pbsize] = struct.pack('<I', crc24(frame[bstart:bstart + pbsize]))[:3]


def mac_icv(msdu):
    """MAC 帧尾 ICV CRC32: 校验 MSDU 载荷(MAC 帧头之后)."""
    return struct.pack('<I', crc32(msdu + b'\x00\x00\x00\x00'))


# =========================================================================
# 帧1: 信标帧 (中央信标, 载波). TMI=2 → 信标物理块 136B, 整帧 = 16 + 136 = 152B
# =========================================================================
PBSIZE = 136
beacon = bytearray(16 + PBSIZE)
beacon[0] = 0x00 | 0x08 | (SNID << 4)      # 帧类型=0 信标, ConInd=1, SNID=5
beacon[1:5] = struct.pack('<I', 25000000)  # BTS = 1.0 秒
beacon[5:9] = struct.pack('<I', 3)         # BPC = 3
write_bits(beacon, 9 * 8, 12, 1)           # 源TEI = 1 (CCO)
write_bits(beacon, 10 * 8 + 4, 4, 2)       # 载波映射表索引 TMI = 2 → 136B
write_bits(beacon, 11 * 8, 9, 100)         # 符号数 = 100
write_bits(beacon, 12 * 8 + 1, 1, 0)       # SNID 高位(信标载波 12,1,1) = 0
write_bits(beacon, 12 * 8 + 2, 2, 1)       # 信标相线 = A
beacon[12] |= VER2                         # 标准版本号(12,4-7) = 2 (SPLC)
fix_fccs(beacon)
# ---- 信标帧载荷区 gb = frame[16 : 16+133) ----
gb_off, gb_len = 16, PBSIZE - 3
beacon[gb_off] = 0x02 | 0x08 | 0x40        # 信标类型=2 中央 + 组网完成 + 开始关联
beacon[gb_off + 1] = 7                     # 组网序列号
beacon[gb_off + 2] = SNID | (0x01 << 4) | (1 << 6)  # SNID低4b=5 | 无线option=1 | PLC能力=1(BPLC+SPLC,表51)
beacon[gb_off + 3] = 12                    # 本网络无线信道编号 = 12
beacon[gb_off + 4] = (1 << 1) | (1 << 5)   # 网络版本号=1(SPLC) | 发现信标使能=1
# 信标管理信息从 gb[6]: 条目数 1 + 站点能力条目(头1+长度1+内容20)
beacon[gb_off + 6] = 1
beacon[gb_off + 7] = 0x01                  # 条目头 = 站点能力条目
beacon[gb_off + 8] = 22                    # 条目长度 = 1+1+20
cap = gb_off + 9
write_bits(beacon, cap * 8, 6, 1)          # 层级数 = 1
write_bits(beacon, cap * 8 + 6, 2, 1)      # 站点相线 = A
write_bits(beacon, (cap + 1) * 8, 12, 3)   # TEI = 3
write_bits(beacon, (cap + 2) * 8 + 4, 4, 2)  # 角色 = PCO
beacon[cap + 3] = 1                        # 信标使用标志
beacon[cap + 4:cap + 10] = STA_MAC         # 发送信标站点 MAC
write_bits(beacon, (cap + 10) * 8, 12, 1)  # 代理站点 TEI = 1
beacon[cap + 12:cap + 16] = struct.pack('<I', 99)  # 路径最低通信成功率
# BPCS CRC32 @ gb[gb_len-5], 覆盖 gb[0 : gb_len-1](即 pbsize-8 字节); 保留字节 @ gb[gb_len-1]
bpcs = crc32(beacon[gb_off:gb_off + gb_len - 1])
beacon[gb_off + gb_len - 5:gb_off + gb_len - 1] = struct.pack('<I', bpcs)
add_pb_crc24(beacon, 16, PBSIZE)

# =========================================================================
# 帧2: 载波 SOF 单站点(表23) + 短 MAC 头(12B) + APP 命令帧
#       TMI编号=0(Bitloading=0) → PB 520B, 整帧 = 16 + 520 = 536B
# =========================================================================
def build_sof_plc(src_tei, dst_tei, pb_num, tmi_idx, mac_frame):
    """载波 SPLC SOF: FCH + pb_num 个物理块(PB头4B+块体+保留1B+CRC24 3B), 表23 布局."""
    pbsize = (520 if tmi_idx <= 8 else 264 if tmi_idx <= 15 else
              136 if tmi_idx <= 20 else 72 if tmi_idx <= 25 else 16)
    frame = bytearray(16 + pb_num * pbsize)
    frame[0] = 0x01 | (SNID << 4)            # 帧类型=1 SOF, ConInd=0, SNID=5
    write_bits(frame, 1 * 8, 12, src_tei)    # 源TEI
    write_bits(frame, 2 * 8 + 4, 12, dst_tei)  # 目的TEI
    write_bits(frame, 4 * 8, 1, 0)           # 多站点帧标识(4,0,1) = 0 单站点
    write_bits(frame, 4 * 8 + 1, 1, 0)       # 训练帧标识(4,1,1) = 0
    write_bits(frame, 4 * 8 + 2, 4, pb_num)  # 物理块个数(4,2-5)
    write_bits(frame, 6 * 8, 1, 0)           # Bitloading(6,0) = 0
    write_bits(frame, 6 * 8 + 1, 5, tmi_idx)  # TMI编号(6,1-5)
    write_bits(frame, 8 * 8, 12, 500)        # 帧长(8,0-11) ×10µs
    frame[12] = VER2 | 0x01                  # SNID高位(12,0,1)=1, 标准版本号=2
    fix_fccs(frame)
    for i in range(pb_num):
        bstart = 16 + i * pbsize
        body_len = pbsize - 8
        seg = mac_frame[i * body_len:(i + 1) * body_len]
        frame[bstart + 4:bstart + 4 + len(seg)] = seg
        add_pb_crc24(frame, bstart, pbsize)
    return frame


app = bytearray(12 + 8)
app[0] = 0x11                              # 报文端口号 = 业务报文
app[1:3] = struct.pack('<H', 0x0101)       # 报文标识符 = CCO-STA 应用层报文
app[4] = 0x02                              # 控制域: 帧类型 = 命令帧
app[5] = 0xE0                              # 控制域: 业务扩展域+需应答+启动站+上行
app[6] = 0x05                              # 业务标识 BID = 从节点信息查询
app[7] = 1                                 # 应用版本号
app[8:10] = struct.pack('<H', 1)           # 帧序号
app[10:12] = struct.pack('<H', 8)          # 帧长
app[12:] = bytes(range(0x41, 0x49))        # 载荷 "ABCDEFGH"
msdu2 = bytes([0x01, 0x01]) + bytes(app)   # MSDU 短帧头: VLAN(8b) + 类型=应用层
mac_hdr2 = bytearray(12)                   # NW 标准短头 12B
mac_hdr2[0] = 0x03                         # MACHeadFlag=1 短头, 版本 bits1-2 = 1 标准帧
mac_hdr2[2:4] = struct.pack('<H', len(msdu2))
write_bits(mac_hdr2, 4 * 8, 12, 1)         # 目的 TEI = 1 (CCO)
write_bits(mac_hdr2, 5 * 8 + 4, 12, 3)     # 源 TEI = 3
mac_hdr2[10:12] = struct.pack('<H', 0x5678)
mac_frame2 = bytes(mac_hdr2) + msdu2 + mac_icv(msdu2)
sof_app = build_sof_plc(3, 1, 1, 0, mac_frame2)

# =========================================================================
# 帧3: 载波 SOF 单站点 + 长 MAC 头(32B) + MMe 物联终端接入请求 (MMType=0x00A0)
# =========================================================================
mme_iot_body = bytearray(10)
mme_iot_body[0:6] = bytes.fromhex('AABBCCDDEE01')      # 物联终端 MAC
write_bits(mme_iot_body, 6 * 8, 12, 1)                 # 网关 TEI = 1
mme_iot_body[8] = 0x03                                 # 终端设备类型
mme_iot_body[9] = 0x14                                 # 发送功率
mme = bytearray(6 + len(mme_iot_body))
mme[0] = 1                                             # MMVersion
mme[1:3] = struct.pack('<H', 0x00A0)                   # MMType = 物联终端接入请求(表39)
mme[6:] = mme_iot_body
msdu3 = bytearray(18 + len(mme))                       # MSDU 长帧头 18B
msdu3[0:6] = bytes.fromhex('AABBCCDDEE01')             # 原始目的 MAC
msdu3[6:12] = CCO_MAC                                  # 原始源 MAC
msdu3[12:16] = struct.pack('<I', 0x8100)               # VLAN = 0x8100 管理消息
msdu3[16:18] = struct.pack('<H', 0x88E1)               # MSDU 类型 = 管理消息报文
msdu3[18:] = mme
mac_hdr3 = bytearray(32)                               # NW MAC 长头 32B
mac_hdr3[0] = 0x02                                     # MACHeadFlag=0 长头, 版本 = 1
mac_hdr3[2:4] = struct.pack('<H', len(msdu3))
write_bits(mac_hdr3, 4 * 8, 12, 1)                     # 目的 TEI = 1 (CCO)
write_bits(mac_hdr3, 5 * 8 + 4, 12, 2)                 # 源 TEI = 2
mac_hdr3[10:12] = struct.pack('<H', 0x1234)
mac_frame3 = bytes(mac_hdr3) + bytes(msdu3) + mac_icv(msdu3)
sof_iot = build_sof_plc(2, 1, 1, 0, mac_frame3)

# =========================================================================
# 帧3b: 载波 SOF 单站点 + 2 个物理块(PB个数=2, 多块重组) + 短 MAC 头 APP
# =========================================================================
sof_2pb = build_sof_plc(3, 1, 2, 0, mac_frame2)

# =========================================================================
# 帧4: 无线 SOF(表131, USER3/150) + 单跳 MAC 帧(版本=2, 表113) +
#       无线发现列表 (msg_type=2, 表214, IE 站点属性 表217)
# =========================================================================
PBRF = 136
sof_rf = bytearray(16 + PBRF)
sof_rf[0] = 0x01 | (SNID << 4)
write_bits(sof_rf, 1 * 8, 12, 3)           # 源TEI = 3
write_bits(sof_rf, 2 * 8 + 4, 12, 0xFFF)   # 目的TEI = 广播
sof_rf[4] = 7                              # LID(4,0-7)
write_bits(sof_rf, 5 * 8, 12, 1000)        # 帧长(5,0-11) ×100µs
write_bits(sof_rf, 6 * 8 + 4, 4, 3)        # 载荷PB大小(6,4-7) = 3 → 136B
write_bits(sof_rf, 7 * 8, 4, 5)            # MCS = 5
write_bits(sof_rf, 7 * 8 + 5, 1, 0)        # 重传 = 0
sof_rf[12] = VER2                          # 标准版本号 = 2 (无线 SOF byte12 其余保留)
fix_fccs(sof_rf)
# 单跳 MAC 帧: 4B 头 + 无线发现列表 + ICV
ie_body = bytearray(14)                    # 站点属性信息(表217)
ie_body[0:6] = CCO_MAC                     # CCO MAC
write_bits(ie_body, 6 * 8, 12, 1)          # 代理 TEI = 1
ie_body[7] = 0x10                          # 角色 = 站点(bit4-7)
ie_body[8] = 0x21                          # 层级=1 / 跳数=2
ie_body[9] = 30                            # 上行速率
ie_body[10] = 25                           # 下行速率
ie_body[11] = 3                            # 最小通信成功率
ie_body[12] = 10                           # 上报周期
ie_body[13] = 60                           # 老化时间
ie = bytes([0x00, 14]) + bytes(ie_body)    # IE: 7b类型=0(长度类型1B) + 长度14 + 内容
disc_payload = STA_MAC + bytes([0x77]) + ie  # 站点MAC + 统计序号 + IE
sh = bytearray(4)
sh[0] = 0x04                               # 版本 bits1-2 = 2 单跳帧
sh[1] = 0x02                               # 消息类型 = 无线发现列表(表214)
sh[2:4] = struct.pack('<H', len(disc_payload))
mac_frame4 = bytes(sh) + disc_payload + mac_icv(disc_payload)
sof_rf[20:20 + len(mac_frame4)] = mac_frame4               # 块体(无线 PB 136B → 块体 128B)
add_pb_crc24(sof_rf, 16, PBRF)

# =========================================================================
# 输出
# =========================================================================
out_dir = os.path.dirname(os.path.abspath(__file__))
pcap1 = build_pcap([beacon, sof_app, sof_iot, sof_2pb], 149)   # USER2 载波
pcap2 = build_pcap([sof_rf], 150)                     # USER3 无线
with open(os.path.join(out_dir, 'test_2026_splc.pcap'), 'wb') as fp:
    fp.write(pcap1)
with open(os.path.join(out_dir, 'test_2026_rf.pcap'), 'wb') as fp:
    fp.write(pcap2)
for name, fr in [('BEACON-SPLC', beacon), ('SOF-APP', sof_app),
                 ('SOF-IOT', sof_iot), ('SOF-RF-SH', sof_rf)]:
    print(f"{name:12s} ({len(fr)}B): {fr.hex()}")
print(f"test_2026_splc.pcap: {len(pcap1)} bytes, 3 frames, linktype=149 (USER2)")
print(f"test_2026_rf.pcap:   {len(pcap2)} bytes, 1 frame,  linktype=150 (USER3)")
