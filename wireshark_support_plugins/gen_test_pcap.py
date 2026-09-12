"""构造 HPLC_RF 测试 pcap (USER0 DLT=147), 验证 Lua dissector.

字节序: little-endian (低字节在前). 依据 BPLC 监控器权威代码:
  BitDefine.getdata: byte_data << (8*(byte_num-start_byte))  → 低字节在前
  main.py: b_nid = data[2] | data[3]<<8 | data[4]<<16        → NID 低字节在前
"""
import struct

def write_bits(buf, start_bit, nbits, value):
    """little-endian bit 序: 字节内 bit0=LSB, 跨字节连续."""
    for i in range(nbits):
        if (value >> i) & 1:
            abs_bit = start_bit + i
            buf[abs_bit // 8] |= (1 << (abs_bit % 8))

def build_pcap(payloads, dlt=147):
    gheader = struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, dlt)
    out = gheader
    for p in payloads:
        out += struct.pack('<IIII', 0, 0, len(p), len(p)) + bytes(p)
    return out

# =========================================================================
# 帧1: SOF 数据帧 (关联请求), 验证 MAC头 + 管理消息 + 小端序
# =========================================================================
sof = bytearray(37)
sof[0] = 0x01                        # DT=1 SOF, 网络类型=0
sof[1:4] = b'\x34\x12\x00'          # NID=0x001234 小端
# SOF 可变区域 (表19, 从 bit32 起)
write_bits(sof, 32, 12, 0x123)       # 源TEI
write_bits(sof, 44, 12, 0x456)       # 目的TEI
write_bits(sof, 56, 8,  0x0A)        # LID
write_bits(sof, 64, 12, 0x789)       # 帧长
write_bits(sof, 76, 4,  1)           # 物理块个数
write_bits(sof, 80, 9,  0x1AB)       # 符号数
write_bits(sof, 92, 4,  0x5)         # 分集拷贝基本模式
write_bits(sof, 96, 4,  0x3)         # 分集拷贝扩展模式
sof[13:16] = b'\x00\x00\x00'        # FCCS
# 物理块头 (表37)
sof[16] = 0xC0                       # 序列号0, 帧起始1, 帧结束1
# 标准 MAC 帧头 (表4, 从字节17起)
mb = 17 * 8
write_bits(sof, mb+0,  4,  0)        # 版本=0
write_bits(sof, mb+4,  12, 0x123)    # 原始源TEI
write_bits(sof, mb+16, 12, 0x456)    # 原始目的TEI
write_bits(sof, mb+28, 4,  0)        # 发送类型=0 单播
write_bits(sof, mb+32, 5,  0)        # 发送次数限值=0
sof[22:24] = b'\x45\x23'            # MSDU序列号=0x2345 小端
sof[24] = 0x00                       # MSDU类型=0 网络管理消息
write_bits(sof, mb+64, 11, 4)        # MSDU长度=4
sof[30] = 0x01                       # 组网序列号=1
# 管理消息头 (表58): MMTYPE=0x0000 关联请求 小端
sof[33:35] = b'\x00\x00'
sof[35:37] = b'\x00\x00'

# =========================================================================
# 帧2: 信标帧 (中央信标), 验证 BTS 时间戳 + 信标载荷 + 小端序
# =========================================================================
# FCH 16B + 信标载荷固定头20B + 信标管理信息(1+1+1+13=16B) + BPCS 4B + PBCS 3B = 59B
beacon = bytearray(59)
beacon[0] = 0x00                        # DT=0 信标
beacon[1:4] = b'\x34\x12\x00'          # NID=0x001234 小端
# 信标可变区域 (表17, 从字节4起)
# BTS 信标时间戳 32bit: NTB值, 比如 25000000 tick = 1秒
write_bits(beacon, 32, 32, 25000000)    # BTS = 25000000 (1.0 秒)
write_bits(beacon, 64, 12, 1)           # 源TEI=1 (CCO)
write_bits(beacon, 76, 4,  0)           # 分集拷贝基本模式
write_bits(beacon, 80, 9,  100)         # 符号数
write_bits(beacon, 90, 2,  0)           # 相线=未知
beacon[13:16] = b'\x00\x00\x00'        # FCCS
# 信标帧载荷 (表38, 从字节16起)
pb = 16
# 首字节: 信标类型2(中央)+组网完成1 → bit0-2=010, bit3=1
beacon[pb] = 0x02 | 0x08                # 信标类型=中央信标, 组网完成
beacon[pb+1] = 0x05                     # 组网序列号=5
beacon[pb+2:pb+8] = b'\x00\x11\x22\x33\x44\x55'  # CCO MAC
beacon[pb+8:pb+12] = b'\x01\x00\x00\x00'  # BPC=1 小端
beacon[pb+12] = 0x0A                    # 无线信道编号=10
# 信标管理信息从 pb+20 起: 条目数1 + 站点能力条目(13B)
mgmt = pb + 20
beacon[mgmt] = 0x01                     # 1个信标条目
beacon[mgmt+1] = 0x00                   # 条目头=站点能力条目
beacon[mgmt+2] = 13                     # 条目长度=13
# 站点能力条目 (表47)
cap = mgmt + 3
write_bits(beacon, cap*8, 12, 1)        # TEI=1 (CCO)
write_bits(beacon, cap*8+12, 12, 0)     # 代理TEI=0
beacon[cap+3] = 100                      # 路径最低通信成功率
beacon[cap+4:cap+10] = b'\x00\x11\x22\x33\x44\x55'  # 发送信标站点MAC
beacon[cap+10] = 0x04                    # 角色=CCO
beacon[cap+10] |= 0x00                   # 层级数=0
beacon[cap+11] = 0xFF                    # 代理站点信道质量
beacon[cap+12] = 0x00                    # 相线=全相线, RF跳数=0
# BPCS 32bit 在管理信息后
bpcs_off = mgmt + 3 + 13
beacon[bpcs_off:bpcs_off+4] = b'\x00\x00\x00\x00'
# PBCS 3bit 在最后
beacon[-3:] = b'\x00\x00\x00'

data = build_pcap([sof, beacon])
with open('test_frames.pcap', 'wb') as fp:
    fp.write(data)
print("SOF   hex:", sof.hex())
print("BEACON hex:", beacon.hex())
print("pcap written: test_frames.pcap (%d bytes, 2 frames)" % len(data))
