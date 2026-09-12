"""构造完整关联请求帧, 验证管理消息体解析 (88字节 AssocReq)."""
import struct

def write_bits(buf, start_bit, nbits, value):
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

# 关联请求消息体 88 字节 (表60)
body = bytearray(88)
body[0:6]   = b'\xAA\xBB\xCC\xDD\xEE\xFF'  # 站点MAC
# 候选代理TEI x5 (每个2字节, TEI 12bit + 链路类型1bit)
for i in range(5):
    p = 6 + i*2
    write_bits(body, p*8, 12, 0x100 + i)   # 候选代理TEI
    write_bits(body, p*8+12, 1, i % 2)     # 链路类型
write_bits(body, 16*8, 2, 1)               # 相线=A
body[17] = 3                               # 设备类型=电表通信单元
body[18] = 0                               # MAC地址类型=电能表地址
write_bits(body, 19*8, 2, 1)               # 模块类型=双模
body[20:24] = b'\x01\x02\x03\x04'          # 站点关联随机数
body[24:42] = b'\x00' * 18                 # 厂家自定义信息
# 站点版本信息 (表66)
body[42] = 0                               # 系统启动原因=正常启动
body[43] = 1                               # BOOT版本号
body[44:46] = b'\x01\x20'                  # 软件版本号(BCD)
body[46:48] = b'\x15\x09'                  # 版本时间(BIN)
body[48:50] = b'\x41\x42'                  # 厂商代码(ASCII)
body[50:52] = b'\x43\x44'                  # 芯片代码(ASCII)
body[52:54] = b'\x05\x00'                  # 硬复位累积次数
body[54:56] = b'\x02\x00'                  # 软复位累积次数
body[56] = 0                               # 代理类型
body[60:64] = b'\x01\x00\x00\x00'          # 端到端序列号
body[64:88] = b'\x99' * 24                 # 管理ID信息(24B)

# SOF 帧: FCH 16B + 物理块头1B + MAC头16B + 管理消息头4B + 消息体88B
total = 16 + 1 + 16 + 4 + 88
frame = bytearray(total)
frame[0] = 0x01                            # DT=1 SOF
frame[1:4] = b'\x34\x12\x00'              # NID 小端
write_bits(frame, 32, 12, 0x123)           # 源TEI
write_bits(frame, 44, 12, 0x456)           # 目的TEI
write_bits(frame, 56, 8,  0x0A)            # LID
write_bits(frame, 64, 12, 0x789)           # 帧长
write_bits(frame, 76, 4,  1)               # 物理块个数
write_bits(frame, 80, 9,  0x1AB)           # 符号数
frame[13:16] = b'\x00\x00\x00'            # FCCS
frame[16] = 0xC0                           # 物理块头
mb = 17 * 8
write_bits(frame, mb+0,  4,  0)            # 版本
write_bits(frame, mb+4,  12, 0x123)        # 原始源TEI
write_bits(frame, mb+16, 12, 0x456)        # 原始目的TEI
write_bits(frame, mb+28, 4,  0)            # 发送类型
write_bits(frame, mb+32, 5,  0)            # 发送次数限值
frame[22:24] = b'\x45\x23'                # MSDU序列号
frame[24] = 0x00                           # MSDU类型=0 管理消息
write_bits(frame, mb+64, 11, 4 + 88)       # MSDU长度=4+88=92
frame[30] = 0x01                           # 组网序列号
# 管理消息头 4B
frame[33:35] = b'\x00\x00'                # MMTYPE=0x0000 关联请求 小端
frame[35:37] = b'\x00\x00'                # 保留
# 消息体 88B
frame[37:37+88] = body

data = build_pcap([frame])
with open('test_assoc_req.pcap', 'wb') as fp:
    fp.write(data)
print("assoc req frame hex:", frame.hex())
print("pcap written: test_assoc_req.pcap (%d bytes)" % len(data))
