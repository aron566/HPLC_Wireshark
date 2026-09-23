"""构造南网 NW_2021 测试 pcap (USER2 DLT=149), 验证 packet-nw_2021.lua dissector.

字节序: little-endian (低字节在前), bit 序: 字节内 bit0=LSB 跨字节连续.
帧坐标依据 src/protocol/nw_protocol/nw_2021 (Qt 监控器) 与报批稿表号.

包含 4 帧 (全部带正确 FCCS/PB CRC24/BPCS/ICV 校验值):
  1. 信标帧 (中央信标, TMI=2→PB 136B, 含站点能力条目)
  2. SOF 数据帧 (长 MAC 头 32B + MMe 关联确认)
  3. SOF 数据帧 (短 MAC 头 12B + APP 命令帧)
  4. SOF 数据帧 (单跳 MAC 帧)
"""
import struct

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
    frame[13:16] = struct.pack('<I', crc24(frame[:16]))[:3]

def build_pcap(payloads, dlt=149):
    gheader = struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, dlt)
    out = gheader
    for p in payloads:
        out += struct.pack('<IIII', 0, 0, len(p), len(p)) + bytes(p)
    return out

SNID = 5
CCO_MAC = bytes.fromhex('001122334455')
STA_MAC = bytes.fromhex('001122334466')

# =========================================================================
# 帧1: 信标帧 (中央信标). TMI=2 → 信标物理块 136B, 整帧 = 16 + 136 = 152B
# =========================================================================
PBSIZE = 136
beacon = bytearray(16 + PBSIZE)
beacon[0] = 0x00 | 0x08 | (SNID << 4)     # 帧类型=0 信标, ConInd=1, SNID=5
beacon[1:5] = struct.pack('<I', 25000000)  # BTS = 25000000 tick (1.0 秒)
beacon[5:9] = struct.pack('<I', 2)         # BPC = 2
write_bits(beacon, 9*8, 12, 1)             # 源TEI = 1 (CCO)
write_bits(beacon, 10*8+4, 4, 2)           # TMI = 2 → 136B
write_bits(beacon, 11*8, 9, 100)           # 符号数 = 100
write_bits(beacon, 12*8+2, 2, 1)           # 信标相线 = A
fix_fccs(beacon)
# ---- 信标帧载荷区 gb = frame[16 : 16+133) ----
gb_off, gb_len = 16, PBSIZE - 3
beacon[gb_off] = 0x02 | 0x08 | 0x20 | 0x40  # 信标类型=2 中央 + 组网完成 + 多网络优选 + 开始关联
beacon[gb_off+1] = 7                        # 组网序列号
beacon[gb_off+2] = SNID                     # 短网络标识
# 信标管理信息从 gb[6]: 条目数 1 + 站点能力条目(头1+长度1+内容20)
beacon[gb_off+6] = 1                        # 条目数
beacon[gb_off+7] = 0x01                     # 条目头 = 站点能力条目
beacon[gb_off+8] = 22                       # 条目长度 = 1+1+20
cap = gb_off + 9
write_bits(beacon, cap*8, 6, 1)             # 层级数 = 1
write_bits(beacon, cap*8+6, 2, 1)           # 站点相线 = A
write_bits(beacon, (cap+1)*8, 12, 3)        # TEI = 3
write_bits(beacon, (cap+2)*8+4, 4, 2)       # 角色 = PCO
beacon[cap+3] = 1                           # 信标使用标志 = 使用
beacon[cap+4:cap+10] = STA_MAC              # 发送信标站点 MAC
write_bits(beacon, (cap+10)*8, 12, 1)       # 代理站点 TEI = 1
beacon[cap+12:cap+16] = struct.pack('<I', 99)  # 路径最低通信成功率 = 99
# BPCS CRC32 @ gb[gb_len-5], 覆盖 gb[0 : gb_len-5] (不含 BPCS 与保留字节); 保留字节 @ gb[gb_len-1]
bpcs = crc32(beacon[gb_off:gb_off+gb_len-1])
beacon[gb_off+gb_len-5:gb_off+gb_len-1] = struct.pack('<I', bpcs)
# PB CRC24 @ 块尾 3B, 覆盖块内前 PBSIZE-3 字节
beacon[16+PBSIZE-3:16+PBSIZE] = struct.pack('<I', crc24(beacon[16:16+PBSIZE]))[:3]

# =========================================================================
# 帧2: SOF 数据帧, 长 MAC 头(32B) + MMe 关联确认 (MMType=0x0031)
# =========================================================================
def build_sof(src_tei, dst_tei, mac_frame):
    """SOF 帧: FCH + 1 个物理块(PB头4B + 块体128B + 保留1B + CRC24 3B)."""
    assert len(mac_frame) <= PBSIZE - 8
    frame = bytearray(16 + PBSIZE)
    frame[0] = 0x01 | (SNID << 4)          # 帧类型=1 SOF, ConInd=0
    write_bits(frame, 1*8, 12, src_tei)     # 源TEI
    write_bits(frame, 2*8+4, 12, dst_tei)   # 目的TEI
    frame[4] = 7                            # LID
    frame[7] = 1 | (2 << 4)                 # PB个数=1, TMI=2 → 136B
    write_bits(frame, 8*8, 12, 500)         # 帧长
    fix_fccs(frame)
    frame[20:20+len(mac_frame)] = mac_frame  # 块体 = MAC 帧(不足补 0)
    frame[16+PBSIZE-3:16+PBSIZE] = struct.pack('<I', crc24(frame[16:16+PBSIZE]))[:3]
    return frame

def mac_icv(mac_head, msdu):
    """MAC 帧尾 ICV CRC32: 校验 MSDU 载荷(mac 头之后)."""
    return struct.pack('<I', crc32(msdu + b'\x00\x00\x00\x00'))

# MAC 帧头(长 32B) + MSDU(长帧头 18B + MMe) + ICV
mme_body = bytearray(6 + 42 + 8)
mme_body[0] = 1                                    # MMVersion
mme_body[1:3] = struct.pack('<H', 0x0031)          # MMType = 关联确认
mme_body[6:12] = STA_MAC                           # 站点 MAC
mme_body[12] = 0x00                                # 结果 = 关联请求成功
mme_body[13] = 1                                   # 站点层级
write_bits(mme_body, 14*8, 12, 3)                  # 站点 TEI = 3
mme_body[16:18] = struct.pack('<H', 1)             # 代理 TEI
mme_body[18] = 1                                   # 总分包数
mme_body[19] = 1                                   # 分包序号
mme_body[20] = 1                                   # 最后一个分包
mme_body[21] = 0x02                                # 链路类型=载波, 载波频段=Band1
mme_body[22:26] = struct.pack('<I', 0xDEADBEEF)    # 关联随机数
mme_body[26:30] = struct.pack('<I', 60000)         # 重新关联时间
mme_body[30:34] = struct.pack('<I', 0x111)         # 端到端序列号
mme_body[34:38] = struct.pack('<I', 0x222)         # 路径序号
mme_body[38] = 7                                   # 组网序列号
mme_body[39] = 1                                   # MMe 版本
# 路由信息 @42: 直连站点数0 / 直连代理数0 / 路由表大小0
msdu = bytearray(18 + len(mme_body))
msdu[0:6] = STA_MAC                                # 原始目的 MAC
msdu[6:12] = CCO_MAC                               # 原始源 MAC
msdu[12:16] = struct.pack('<I', 0x8100)            # VLAN = 0x8100 管理消息
msdu[16:18] = struct.pack('<H', 0x88E1)            # MSDU 类型 = 管理消息报文
msdu[18:] = mme_body
mac_hdr = bytearray(32)
mac_hdr[0] = 0x02                                  # MACHeadFlag=0 长头, 版本 bits1-2 = 1 标准帧
mac_hdr[2:4] = struct.pack('<H', len(msdu))        # MSDU 长度
write_bits(mac_hdr, 4*8, 12, 3)                    # 目的 TEI = 3
write_bits(mac_hdr, 5*8+4, 12, 1)                  # 源 TEI = 1 (CCO)
mac_hdr[10:12] = struct.pack('<H', 0x1234)         # MSDU 序列号
mac_frame2 = bytes(mac_hdr) + bytes(msdu) + mac_icv(mac_hdr, msdu)
sof_mme = build_sof(1, 3, mac_frame2)

# =========================================================================
# 帧3: SOF 数据帧, 短 MAC 头(12B) + APP 命令帧 (BID=0x05 从节点信息查询)
# =========================================================================
app = bytearray(12 + 8)
app[0] = 0x11                                      # 报文端口号 = 业务报文
app[1:3] = struct.pack('<H', 0x0101)               # 报文标识符 = CCO-STA 应用层报文
app[4] = 0x02                                      # 控制域: 帧类型 = 命令帧
app[5] = 0xE0                                      # 控制域: 业务扩展域+需应答+启动站+上行
app[6] = 0x05                                      # 业务标识 BID = 从节点信息查询
app[7] = 1                                         # 应用版本号
app[8:10] = struct.pack('<H', 1)                   # 帧序号
app[10:12] = struct.pack('<H', 8)                  # 帧长
app[12:] = bytes(range(0x41, 0x49))                # 载荷 "ABCDEFGH"
msdu3 = bytes([0x00, 0x01]) + bytes(app)           # MSDU 短帧头: VLAN + 类型=应用层
mac_hdr3 = bytearray(12)
mac_hdr3[0] = 0x03                                 # MACHeadFlag=1 短头, 版本 = 1 标准帧
mac_hdr3[2:4] = struct.pack('<H', len(msdu3))
write_bits(mac_hdr3, 4*8, 12, 1)                   # 目的 TEI = 1 (CCO)
write_bits(mac_hdr3, 5*8+4, 12, 3)                 # 源 TEI = 3
mac_hdr3[10:12] = struct.pack('<H', 0x5678)
mac_frame3 = bytes(mac_hdr3) + msdu3 + mac_icv(mac_hdr3, msdu3)
sof_app = build_sof(3, 1, mac_frame3)

# =========================================================================
# 帧4: SOF 数据帧, 单跳 MAC 帧 (版本 bits1-2 = 2)
# =========================================================================
sh_payload = bytes(range(0x20, 0x28))              # 8B 载荷
sh = bytearray(4)
sh[0] = 0x04                                       # 版本 bits1-2 = 2 单跳帧
sh[1] = 0x05                                       # 消息类型
sh[2:4] = struct.pack('<H', len(sh_payload))
mac_frame4 = bytes(sh) + sh_payload + struct.pack('<I', crc32(sh_payload + b'\x00\x00\x00\x00'))
sof_sh = build_sof(3, 1, mac_frame4)

data = build_pcap([beacon, sof_mme, sof_app, sof_sh])
with open('test_nw_frames.pcap', 'wb') as fp:
    fp.write(data)
for name, fr in [('BEACON', beacon), ('SOF-MME', sof_mme), ('SOF-APP', sof_app), ('SOF-SH', sof_sh)]:
    print(f"{name:8s} ({len(fr)}B): {fr.hex()}")
print(f"pcap written: test_nw_frames.pcap ({len(data)} bytes, 4 frames, linktype=149 USER2)")
