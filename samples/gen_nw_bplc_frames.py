#!/usr/bin/env python3
"""构造南网 BPLC 1.0 (nw_2021) 3 个最小验证帧 + CRC 自校验。

帧布局依据 packet-nw_2021.lua / nw_2021_parser.cpp / nw_2021_msdu_parser.cpp:
  .bin 帧 = 0x3C | dlen(2 LE) | ts(4 LE) | phr_mcs(1) | option(1) | channel(1) | isRF(1) | MPDU | 0x3E
  MPDU = FCH(16B) [+ PB 块(信标/SOF)]
  SOF PB = PB头(4B,全0) + 块体(pbsize-8) + 保留(1B) + CRC24(3B)
  块体 = MAC帧(长32B/短12B/单跳4B) + MSDU + ICV(CRC32,4B)

3 帧:
  frame1 信标(dt=0):       CCO(TEI=1) 中央信标
  frame2 关联确认(dt=1):   CCO→STA(TEI=2), MMe 0x0031, link_type=载波, result=0
  frame3 单跳帧(dt=1):     CCO→STA 单跳无线发现列表, 表217 站点属性 14B
"""

# ---- CRC24 (poly 0xC60001, LSB-first, init 0) ----
_T24 = []
for _v in range(256):
    _c = _v
    for _ in range(8):
        _c = (_c >> 1) ^ 0xC60001 if (_c & 1) else (_c >> 1)
    _T24.append(_c & 0xFFFFFF)

def crc24(data: bytes) -> int:
    c = 0
    for b in data:
        c = (c >> 8) ^ _T24[(c ^ b) & 0xFF]
    return c & 0xFFFFFF

# ---- CRC32 (poly 0xEDB88320, LSB-first, init 0xFFFFFFFF, 末取反) ----
_T32 = []
for _v in range(256):
    _c = _v
    for _ in range(8):
        _c = (_c >> 1) ^ 0xEDB88320 if (_c & 1) else (_c >> 1)
    _T32.append(_c)

def crc32(data: bytes) -> int:
    c = 0xFFFFFFFF
    for b in data:
        c = (c >> 8) ^ _T32[(c ^ b) & 0xFF]
    return (c ^ 0xFFFFFFFF) & 0xFFFFFFFF

def le16(v): return bytes([v & 0xFF, (v >> 8) & 0xFF])
def le24(v): return bytes([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF])
def le32(v): return bytes([v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF])

# 12bit TEI 写入 (LSB-first)
def put_tei12(buf, off, tei):   # @ byte off bit0
    buf[off] |= tei & 0xFF
    buf[off + 1] |= (tei >> 8) & 0x0F

def put_tei12h(buf, off, tei):  # @ byte off bit4
    buf[off] |= (tei & 0x0F) << 4
    buf[off + 1] |= (tei >> 4) & 0xFF

# ---- 载波 PB 大小 (与 packet-nw_2021.lua plc_pb_size 一致) ----
def plc_pb_size(tmi, ext):
    if tmi in (0, 1): return 520
    if 2 <= tmi <= 6: return 136
    if 7 <= tmi <= 10: return 520
    if tmi in (11, 12): return 264
    if 1 <= ext <= 6: return 520
    if 10 <= ext <= 14: return 136
    return None

SNID = 0x0E
CCO_MAC = bytes([0xCE, 0xCE, 0xCE, 0xCE, 0xCE, 0xCE])   # 真值: 发现列表 TEI=1 (CCO)
STA_MAC = bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x03])   # 真值: 发现列表 TEI=2 (STA)

def build_fch(dt, vf12, snid=SNID, conind=0):
    """FCH 16B: byte0 = dt|conind|snid, byte1-12 = vf, byte13-15 = FCCS(CRC24 over 0-12)."""
    fch = bytearray(16)
    fch[0] = (dt & 0x07) | ((conind & 1) << 3) | ((snid & 0x0F) << 4)
    fch[1:13] = vf12
    fch[13:16] = le24(crc24(bytes(fch[:13])))
    return bytes(fch)


# ============ frame1: CCO 中央信标 (dt=0, TMI=4 → PB 136B) ============
def build_beacon():
    tmi = 4
    pbsz = plc_pb_size(tmi, 0)  # 136
    src_tei = 1  # CCO

    # FCH vf (byte1-12): BTS(4)+BPC(4)+srcTEI(12b@9)+TMI(byte10高4)+符号数(9b@11)+相线(byte12低4)
    vf = bytearray(12)
    vf[0:4] = le32(0x12345678)        # BTS
    vf[4:8] = le32(0)                 # BPC
    put_tei12(vf, 8, src_tei)         # src TEI @ byte9 (vf[8])
    vf[9] |= (tmi & 0x0F) << 4        # TMI @ byte10 高4 (vf[9])
    vf[10] = 0                        # 符号数=0 (byte11)
    vf[11] = 0x10                     # byte12: std_ver=1(高4) | 相线=0(低4)

    fch = build_fch(0, bytes(vf))

    # 信标载荷 PB (pbsz 字节): 固定头6B + 管理条目(0) + 填充 + BPCS(4B) + 保留(1B) + PB CRC24(3B)
    gb_end = pbsz - 3  # 载荷区末尾(不含 PB CRC24)
    payload = bytearray(pbsz)
    # 固定头 6B: beacon_type=2(中央信标,0x07) | net_cplt(0x08)
    payload[0] = 0x02 | 0x08          # 中央信标 + 组网完成
    payload[1] = 0x00                 # net_seq
    payload[2] = SNID                 # snid
    payload[3:6] = b'\x00\x00\x00'    # head_rsv
    # 管理条目区: off+6 .. gb_end-5 (此处为空 → 全填充0)
    # BPCS @ gb_end-5 (4B): CRC32 over 固定头+管理条目+填充 = payload[0:gb_end-5]
    bpcs = crc32(bytes(payload[:gb_end - 5]))
    payload[gb_end - 5:gb_end - 1] = le32(bpcs)
    # 保留字节 @ gb_end-1
    payload[gb_end - 1] = 0
    # PB CRC24 @ pbsz-3 (3B): CRC24 over payload[0:pbsz-3]
    payload[pbsz - 3:pbsz] = le24(crc24(bytes(payload[:pbsz - 3])))

    return fch + bytes(payload)


# ============ SOF 通用: FCH + PB (载波) ============
def build_sof(src_tei, dst_tei, mac_frame, tmi=4):
    pbsz = plc_pb_size(tmi, 0)
    assert pbsz and len(mac_frame) <= pbsz - 8, f"MAC帧 {len(mac_frame)} > 块体 {pbsz-8}"

    # FCH vf (byte1-12): srcTEI(12b@1)+dstTEI(12b@2h)+LID+rsv+PBNum/TMI(byte7)+frame_len(12b@8)+...
    vf = bytearray(12)
    put_tei12(vf, 0, src_tei)          # byte1 (vf[0])
    put_tei12h(vf, 1, dst_tei)         # byte2 bit4 (vf[1])
    vf[3] = 0                          # LID
    vf[4:6] = b'\x00\x00'              # rsv (byte5-6)
    vf[6] = (1 & 0x0F) | ((tmi & 0x0F) << 4)   # byte7: PBNum=1(低4) + TMI(高4)
    # frame_len(12b@8) = 0 (帧时长×10µs, 信息性)
    vf[7] = 0                          # byte8
    vf[8] = 0                          # byte9
    vf[9] = 0                          # byte10 (tei_filter/resend/符号数)
    vf[10] = 0                         # byte11 (符号数)
    vf[11] = 0x10                      # byte12: std_ver=1(高4) | TMI_EXT=0(低4)

    fch = build_fch(1, bytes(vf))

    # PB 块: PB头(4B,全0) + 块体(MAC帧+填充) + 保留(1B) + CRC24(3B)
    pb = bytearray(pbsz)
    pb[0:4] = b'\x00\x00\x00\x00'      # PB头
    pb[4:4 + len(mac_frame)] = mac_frame  # 块体: MAC帧
    # 填充(块体剩余) + 保留字节 自动为 0
    pb[pbsz - 3:pbsz] = le24(crc24(bytes(pb[:pbsz - 3])))  # PB CRC24

    return fch + bytes(pb)


# ============ MAC 标准帧(长头 32B) + MSDU + MMe + ICV ============
def build_std_mac(dst_tei, src_tei, msdu: bytes, head_flag=0):
    """标准帧 MAC: 长头(32B)/短头(12B) + MSDU + ICV(CRC32 over MSDU)."""
    hdr_len = 32 if head_flag == 0 else 12
    msdu_len = len(msdu)
    mac = bytearray(hdr_len + msdu_len + 4)
    mac[0] = (head_flag & 1) | (1 << 1)   # version=1(标准帧, bit1-2), head_flag(bit0)
    mac[2:4] = le16(msdu_len)
    put_tei12(mac, 4, dst_tei)            # dst TEI @ byte4
    put_tei12h(mac, 5, src_tei)           # src TEI @ byte5 bit4
    mac[7] = 0                            # restart_cnt
    mac[8] = 0                            # bcast_dir
    mac[9] = 0                            # send_type
    mac[10:12] = le16(0)                  # msdu_seq
    if head_flag == 0:
        mac[12:32] = b'\x00' * 20         # 保留 20B
    mac[hdr_len:hdr_len + msdu_len] = msdu
    mac[hdr_len + msdu_len:hdr_len + msdu_len + 4] = le32(crc32(bytes(mac[hdr_len:hdr_len + msdu_len])))
    return bytes(mac)


def build_msdu_mme(dst_mac, src_mac, mme_type, mme_body):
    """MSDU(长头18B): dstMAC(6)+srcMAC(6)+VLAN(4,0x8100)+MSDU类型(2,0x88E1) + MMe头(6)+body."""
    msdu = bytearray()
    msdu += dst_mac                      # 6B
    msdu += src_mac                      # 6B
    msdu += le32(0x8100)                 # VLAN = 0x8100 → MMe
    msdu += le16(0x88E1)                 # MSDU 类型 = 管理消息
    msdu += bytes([0x00])                # MMe version
    msdu += le16(mme_type)               # MMType
    msdu += b'\x00\x00\x00'              # MMe RSV 24b
    msdu += mme_body
    return bytes(msdu)


# ============ frame2: 关联确认 (MMe 0x0031) ============
def build_assoc_cnf():
    dst_tei = 2   # STA TEI
    src_tei = 1   # CCO
    # MMe body (相对 MMe 头, 从 byte6 起)
    body = bytearray()
    body += STA_MAC                       # byte6-11  STA MAC
    body += bytes([0x00])                 # byte12 result = 0 (成功)
    body += bytes([0x01])                 # byte13 level = 1
    b14 = bytearray(2)
    put_tei12(b14, 0, dst_tei)            # byte14-15 sta TEI (12b @ byte14)
    body += bytes(b14)
    body += le16(1)                       # byte16-17 proxy TEI = 1 (CCO)
    body += bytes([0x01])                 # byte18 total pkgs
    body += bytes([0x00])                 # byte19 pkg idx
    body += bytes([0x01])                 # byte20 last pkg
    body += bytes([0x00])                 # byte21 link_type=0(载波)+carrier=0
    body += le32(0)                       # byte22-25 rand
    body += le32(0)                       # byte26-29 reassoc
    body += le32(0)                       # byte30-33 e2e seq
    body += le32(0)                       # byte34-37 path seq
    body += bytes([0x00])                 # byte38 net_sn
    body += bytes([0x00])                 # byte39 mme_ver
    body += b'\x00\x00'                   # byte40-41 gap
    # 路由信息 @42: sta_sum(2)+pco_sum(2)+table_size(2)+rsv(2) = 8B, 全 0
    body += b'\x00' * 8

    msdu = build_msdu_mme(STA_MAC, CCO_MAC, 0x0031, bytes(body))
    mac = build_std_mac(dst_tei, src_tei, msdu, head_flag=0)
    return build_sof(src_tei, dst_tei, mac, tmi=4)


# ============ frame3: 单跳帧 无线发现列表 (MAC version=2, msg_type=2, 表217) ============
def build_singlehop_rfdl():
    # 例外测试(有意构造,非错误):载波信道上的单跳帧。
    # 规约上单跳仅无线:《计量自动化系统双模通信技术规范第2部分:无线通信接入协议》
    # (BPLC)§8.1.2.4/表107(版本与信道对应关系:单跳载波不支持、无线支持)/表113
    # (单跳 MAC 帧头格式);SPLC 第4部分 5.1.2.4/表12 同结论。
    # 本帧用纯载波 SOF 外壳(build_sof:byte7=PBNum1+TMI4、byte12=0x10)且
    # wrap_frame 默认 is_rf=0,覆盖解析器载波信道收到 version=2 时的 qWarning +
    # 照常解析路径(nw_2021_parser.cpp)。切勿“修正”为 is_rf=1:RF 分支会按无线
    # 坐标误读载波头而垮。IE 与 BPLC 表217 站点属性信息(14B)逐字节对上;
    # 注意不是 SPLC 第4部分表217(SPLC4 表217=帧类型取值,站点属性是表142)。
    dst_tei = 2
    src_tei = 1
    # MSDU(单跳): 站点MAC(6)+统计序号(1) + IE TLV(type=0站点属性,len=14,BPLC 表217)
    ie = bytearray()
    ie += bytes([0x00])                   # IE type = 0 (站点属性, BPLC 表217)
    ie += bytes([0x0E])                   # IE len = 14 (ltype bit7=0 → 1B len)
    # BPLC 表217 站点属性 14B
    attr = bytearray()
    attr += CCO_MAC                       # 0-5  CCO MAC
    b = bytearray(2)
    put_tei12(b, 0, 1)                    # 6-7  代理 TEI = 1 (12b @ byte6)
    b[1] |= 0x10                          # 7 高4  角色=1 (mask 0xF0)
    attr += bytes(b)
    attr += bytes([0x12])                 # 8   层级=2(低4) | RF跳数=1(高4)  (STA4 在 3 级拓扑中为 L2)
    attr += bytes([0x64])                 # 9   代理上行接收率 = 100 (真值)
    attr += bytes([0x64])                 # 10  代理下行接收率 = 100 (真值)
    attr += bytes([0x64])                 # 11  最小接收率 = 100 (真值)
    attr += bytes([0x0A])                 # 12  发现列表周期 = 10
    attr += bytes([0x03])                 # 13  老化周期 = 3

    msdu = bytearray()
    msdu += STA_MAC                       # 站点 MAC 6B
    msdu += bytes([0x01])                 # 统计序号 1B
    msdu += bytes(ie)                     # IE type + len
    msdu += bytes(attr)                   # 表217 14B

    # MAC 单跳头 4B: version=2(bit1-2) + msg_type=2(byte1) + msdu_len(byte2-3)
    msdu_len = len(msdu)
    mac = bytearray(4 + msdu_len + 4)
    mac[0] = 2 << 1                       # version=2 (单跳)
    mac[1] = 2                            # msg_type = 2 (无线发现列表)
    mac[2:4] = le16(msdu_len)
    mac[4:4 + msdu_len] = msdu
    mac[4 + msdu_len:4 + msdu_len + 4] = le32(crc32(bytes(mac[4:4 + msdu_len])))
    return build_sof(src_tei, dst_tei, bytes(mac), tmi=4)


# ============ .bin 封装 (0x3C..0x3E 哨兵 + 转义) ============
def special_handle(buf: bytes) -> bytes:
    out = bytearray()
    for b in buf:
        if b in (0x3C, 0x3D, 0x3E):
            out.append(0x3D)
            out.append(0xFF ^ b)
        else:
            out.append(b)
    return bytes(out)

def wrap_frame(mpdu: bytes, ts=0, channel=1, is_rf=0) -> bytes:
    # 内层 = le16(len) + le32(ts) + media3B + isRF1B + MPDU
    # 长度字段覆盖 ts 之后全部字节, 即 4(ts) + 4(media+isRF) + len(mpdu)
    inner_len = len(mpdu) + 8            # 4B ts + 3B media + 1B isRF
    inner = le16(inner_len) + le32(ts) + bytes([0, 0, channel]) + bytes([is_rf]) + mpdu
    return bytes([0x3C]) + special_handle(inner) + bytes([0x3E])


def main():
    frames = {
        "beacon": build_beacon(),
        "assoc_cnf": build_assoc_cnf(),
        "singlehop_rfdl": build_singlehop_rfdl(),
    }
    import os
    out_dir = os.path.dirname(os.path.abspath(__file__))
    bin_path = os.path.join(out_dir, "nw_bplc_3frames.bin")
    with open(bin_path, "wb") as f:
        f.write(bytes(8))                # 文件头 8B BCD 时间 (全 0, 占位)
        for name, mpdu in frames.items():
            f.write(wrap_frame(mpdu))
            print(f"{name}: MPDU {len(mpdu)}B")
            print(f"  {mpdu.hex(' ')}")
    print(f"\n写 bin: {bin_path}")


if __name__ == "__main__":
    main()
