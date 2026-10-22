#!/usr/bin/env python3
# gen_gw2026_test_pcap.py — 生成 HDC 2.0 (GW_2026) 测试 pcap (USER4 DLT 151, 媒介头+MPDU)
# 用例:
#   1. SOF 数据帧(版本=1, 分集拷贝 TMI=3→PB136, 单块) 携带单跳 MAC 帧:无线发现列表
#      (IE type=0 站点属性 14B + type=3 位图版: StartTEI=8 位图0x25 + 3 邻居 8b 接收率)
#   2. SOF 数据帧(版本=1, 动态加载模式 参数=1→PB136) 携带标准 MAC 帧+管理消息 MMe 0x0011
#      (Bitloading 训练请求 4B) 与 0x0013(分组请求: 8B 保留+1 邻居)
#   3. 版本=0 的 1.0 帧(委托回归对照)
# 验证: FCH 表4 NID 16b / 表7 SOF VF / PB CRC24 / MAC 表2 新字段 / 表3 单跳+聚合位
#       / RFDL 表92/102-103 逐字段 / MMe 表43 新报文 / 版本委托
import struct

def read_bits_le(data, start_bit, nbits):
    """字节内 bit0=LSB, 跨字节连续的小端位流取值(与 dissector read_bits 一致)"""
    v = 0
    for i in range(nbits):
        byte = (start_bit + i) // 8
        bit = (start_bit + i) % 8
        if data[byte] >> bit & 1:
            v |= 1 << i
    return v

def set_bits_le(data, start_bit, nbits, value):
    for i in range(nbits):
        byte = (start_bit + i) // 8
        bit = (start_bit + i) % 8
        if value >> i & 1:
            data[byte] |= 1 << bit
        else:
            data[byte] &= ~(1 << bit) & 0xFF

def crc24_lsb(data):
    """与 dissector crc24_lsb 一致: poly=0xC60001(反射), init=0, 校验前 len-3 字节, 结果末 3B 小端"""
    tbl = []
    for v in range(256):
        crc = v
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xC60001) if (crc & 1) else (crc >> 1)
        tbl.append(crc & 0xFFFFFF)
    crc = 0
    for b in data[:-3]:
        idx = (crc ^ b) & 0xFF
        crc = ((crc >> 8) ^ tbl[idx]) & 0xFFFFFF
    return crc

def crc32_le(data):
    import zlib
    # dissector crc32_le: 从 LSB 开始的逐位 CRC32(初值 0xffffffff, 结果异或前不反转?)
    # 与 C++ crc32_le 一致: 逐位右移, poly 0xEDB88320, 初值/终值处理同 zlib 的反序
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF

# ── FCH (16B, HDC 2.0 表4) ──
def make_fch(dt, nid16, vf_bytes):
    fch = bytearray(16)
    fch[0] = dt & 0x07                      # DT(0,0,3) + 网络类型
    fch[1] = nid16 & 0xFF; fch[2] = (nid16 >> 8) & 0xFF   # NID 16b LE
    fch[3] = 0                              # 保留
    fch[4:13] = vf_bytes                    # 可变区域 9B (byte4-12)
    fch[12] = (fch[12] & 0x0F) | 0x10       # 标准版本号(byte12 bit4-7) = 1 (HDC 2.0)
    fccs = crc24_lsb(fch)                   # crc24_lsb 校验前 len-3=13 字节
    fch[13] = fccs & 0xFF; fch[14] = (fccs >> 8) & 0xFF; fch[15] = (fccs >> 16) & 0xFF
    return bytes(fch)

def make_pb(body, seq, start, end, pbsize=136):
    """PB: 头 1B(END|START|seq) + 体(补零至 pbsize-4) + CRC24 3B"""
    head = (0x80 if end else 0) | (0x40 if start else 0) | (seq & 0x3F)
    blk = bytearray([head]) + bytearray(body)
    blk += bytearray(pbsize - 4 - len(body))          # 填充至 pbsize-4(头1B+CRC24 3B)
    crc = crc24_lsb(bytes(blk) + b'\x00\x00\x00')     # 校验前 len-3 = 头+体
    blk += bytes([crc & 0xFF, (crc >> 8) & 0xFF, (crc >> 16) & 0xFF])
    return bytes(blk)

# ── 单跳 MSDU: 头 4B + 载荷 ──
def sh_head(msg_type, payload_len):
    h = bytearray(4)
    h[0] = 0x01                              # 版本=1(单跳帧协议)
    h[1] = msg_type
    set_bits_le(h, 2*8, 11, payload_len)     # MSDULen(2,0,11)
    # byte3: bit0-2 保留 + bit3-6 RSV1 + bit7 聚合MAC帧标识=0
    return bytes(h)

def make_rfdl_payload():
    """无线发现列表(表91): STA MAC + Seq + IE(type=0 属性14B) + IE(type=3 位图版)"""
    p = bytearray()
    p += bytes.fromhex("345A11223344")       # STA MAC
    p.append(0x09)                           # 统计序号
    # IE type=0 站点属性(表92, 14B): CCO MAC 6 + 代理TEI 2 + 角色/层级 1 + 层级/RF跳 1 + 5 接收率/周期
    ie0 = bytearray()
    ie0 += bytes.fromhex("AABBCCDDEEFF")     # CCO MAC
    ie0.append(0x34); ie0.append(0x12)       # byte6=代理TEI低8b=0x34; byte7=代理高4b(2)+角色(1)
    ie0.append(0x20)                         # byte8: 层级(低半)=0 + RF 跳数(高半)=2
    ie0 += bytes([90, 80, 70, 30, 4])        # 上行/下行/最小接收率 + 周期 + 老化
    p.append(0x00); p.append(len(ie0))       # type=0, 长度类型0(1B), 长度
    p += ie0
    # IE type=3 位图版(表102/103): 组合类型0(8b 接收率) + StartTEI=8 + 位图1B=0x25 + 3 邻居
    ie3 = bytearray()
    ie3.append(0x00)                         # 组合类型 0
    ie3.append(0x08); ie3.append(0x00)       # StartTEI=8 (12b LE)
    ie3.append(0x01)                         # 位图大小 1B
    ie3.append(0x25)                         # bit0/2/5 → TEI 8/10/13
    ie3 += bytes([90, 75, 60])               # 3 条 8b 接收率
    p.append(0x03); p.append(len(ie3))
    p += ie3
    return bytes(p)

def build_sof_frame(mac_bytes, mod_param=3, dynamic=False, nid16=0x1234):
    """版本=1 SOF(表7) + 单物理块承载 mac_bytes (PB136)"""
    vf = bytearray(9)                        # vf[0]=FCH byte4 ... vf[8]=FCH byte12
    set_bits_le(vf, 0, 12, 0x001)            # 源TEI(4,0,12)=1 (CCO)
    set_bits_le(vf, 12, 12, 0xFFF)           # 目的TEI(5,4,12)=广播
    vf[3] = 0                                # byte7: 帧类型0(数据)/TF位置0/TF符号数0
    set_bits_le(vf, 32, 12, 200)             # 帧长 FL(8,0-7 + 9,0-3)=200 (×10µs) → vf bit32-43
    # FCH符号数(9,7)+(10,0)=vf bit47+48; 调制方式(10,1)=vf bit49; 参数(10,2-6)=vf bit50-54
    # PB个数(10,7)+(11,0)=vf bit55+56; 广播/加密/重传(11,1-3)=vf bit57-59
    # 频段(11,4-6)=vf bit60-62; 增益(11,7)+(12,0-2)=vf bit63-66; 加载表ID(12,3)=vf bit67
    set_bits_le(vf, 47, 1, 1); set_bits_le(vf, 48, 1, 0)  # FCH符号数 = 标准(1)
    set_bits_le(vf, 49, 1, 1 if dynamic else 0)           # 调制方式
    set_bits_le(vf, 50, 5, mod_param)        # 参数: TMI(分集) / 物理块类型(动态)
    set_bits_le(vf, 55, 1, 0); set_bits_le(vf, 56, 1, 0)  # PB个数编码0 = 1 块
    fch = make_fch(1, nid16, bytes(vf))
    pb = make_pb(mac_bytes, seq=0, start=True, end=True, pbsize=136)
    return fch + pb

def make_case1():
    """版本=1 SOF + 单跳 RFDL 帧 (PB136, 单块)"""
    msdu = sh_head(0x00, 0) + make_rfdl_payload()
    msdu = sh_head(0x00, len(msdu) - 4) + msdu[4:]
    # 单跳帧 MSDU CRC32: 覆盖 MSDU(不含 4B 单跳头), 与 C++ append_msdu_crc(body,4,msdu_len) 一致
    mac = msdu + struct.pack('<I', crc32_le(msdu[4:]))
    return build_sof_frame(mac)

def make_std_mac_mme_frame(mmtype, body):
    """标准 MAC 帧(表2) + 管理消息(表43)"""
    msdu_len = 4 + len(body)
    m = bytearray(16)
    m[0] = 0x00                              # 版本=0(标准帧协议)
    set_bits_le(m, 0*8+4, 12, 0x001)         # 原始源TEI=1
    set_bits_le(m, 2*8, 12, 0x002)           # 原始目的TEI=2
    m[3] |= 0x10                             # 发送类型=单播(bit4-7=0? 表2: 发送类型(3,4-7))
    m[3] = (m[3] & 0x0F) | (0x00 << 4)       # 单播
    m[4] = 0x03                              # 发送次数限值=3 + 聚合标识(4,5)=0
    set_bits_le(m, 5*8, 16, 0x0001)          # MSDU 序列号
    m[7] = 0x00                              # MSDU 类型=0 管理消息
    set_bits_le(m, 8*8, 11, msdu_len)
    m[9] = 0x00                              # 重启次数/主代理标志
    m[10] = 0x00                             # 路由跳数
    m[11] = 0x00                             # 广播方向/路径修复/MACAddrFlag=0
    set_bits_le(m, 11*8+4, 12, 0x001)        # 发送帧序号=1 (HDC 2.0 新增)
    m[13] = 0x2A                             # 组网序列号
    m[14] = 0x00
    m[15] = 0x77                             # 链路标识符 (HDC 2.0 新增)
    msdu = bytes(m)
    msdu += struct.pack('<H', mmtype) + b'\x00\x00'   # 管理消息头: MMTYPE + 保留
    msdu += body
    # ICV CRC32 覆盖 MSDU(不含 MAC 帧头), 与 C++ crc32_le(msdu_off, msdu_len+4) 一致
    msdu_payload = msdu[16:]
    return msdu + struct.pack('<I', crc32_le(msdu_payload))

def make_case2():
    """版本=1 SOF(动态加载 参数=1→PB136) + 标准 MAC + MMe 0x0011 Bitloading 训练请求(表104, 4B)"""
    body11 = bytearray(4)
    set_bits_le(body11, 0, 12, 0x001)        # 源TEI(0,12)
    set_bits_le(body11, 16, 12, 0x002)       # 目的TEI(2,12)
    body11[3] = 0x01                         # 子载波分组大小=1 → 8 个一组
    mac = make_std_mac_mme_frame(0x0011, bytes(body11))
    return build_sof_frame(mac, mod_param=1, dynamic=True)

def make_case3():
    """版本=1 SOF + 标准 MAC + MMe 0x0013 分组请求(表109/110)"""
    body13 = bytearray(8)
    body13.append(0x02)                      # 邻居个数=2(含本站)
    body13 += bytes([0x23, 0x01, 0x5A])      # 邻居1: TEI=0x123, 类型0(组成员), PLC质量0xA/RF质量0x5
    body13 += bytes([0x34, 0x02, 0x6B])      # 邻居2: TEI=0x234, 类型0(组成员), PLC质量0xB/RF质量0x6
    mac = make_std_mac_mme_frame(0x0013, bytes(body13))
    return build_sof_frame(mac)

def wrap_user4(mpdu, is_rf=False):
    """USER4 串口混合格式: [phr_mcs][option][channel][isRF] + MPDU"""
    return bytes([0x07, 0x01, 0x05, 0x01 if is_rf else 0x00]) + mpdu

def write_pcap(path, frames, linktype=151):
    with open(path, 'wb') as f:
        f.write(struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, linktype))
        ts = 1000000000
        for fr in frames:
            f.write(struct.pack('<IIII', ts, 0, len(fr), len(fr)))
            f.write(fr)
            ts += 1

if __name__ == '__main__':
    frames = [
        wrap_user4(make_case1()),   # 版本1 单跳 RFDL
        wrap_user4(make_case2()),   # 版本1 标准 MAC + MMe 0x0011
        wrap_user4(make_case3()),   # 版本1 标准 MAC + MMe 0x0013
    ]
    write_pcap('test_2026/test_gw2026.pcap', frames)
    print('test_2026/test_gw2026.pcap written,', len(frames), 'frames')
