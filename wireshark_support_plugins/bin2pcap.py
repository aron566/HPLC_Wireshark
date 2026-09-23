"""BPLC bin 回放文件 → pcap 转换器

bin 格式 (依据 BPLC_STA_QtMonitor 的 BplcParser::decode_envelope):
  文件头 8B BCD 时间标注 (年 月 日 时 分 秒 毫秒高 毫秒低)
  + 0x3C...0x3E 哨兵帧流 (帧内 0x3C/0x3D/0x3E 转义为 0x3D + (0xFF^字节))

每帧反转义后:
  data_len(2B 小端) + timestamp(4B 小端 NTB) + 媒介头4B(phr_mcs/option/channel/isRF) + 纯MPDU

pcap linktype (由帧内 isRF 决定载波/无线, 由协议决定 USER 组):
  国网 GW_2022: 载波 147 (USER0) / 无线 148 (USER1)  → packet-gw_2022.lua
  南网 NW_2021: 载波 149 (USER2) / 无线 150 (USER3)  → packet-nw_2021.lua

用法:
  python bin2pcap.py BPLC_xxx.bin            # 协议自动识别 (按帧结构投票, 平票回退国网)
  python bin2pcap.py --nw BPLC_xxx.bin       # 强制南网 NW_2021
  python bin2pcap.py --gw BPLC_xxx.bin       # 强制国网 GW_2022
"""
import struct, sys, os, time

def bcd(b):
    return ((b >> 4) & 0xF) * 10 + (b & 0xF)

# ── PB 块大小表 (与 packet-gw_2022.lua / packet-nw_2021.lua 一致) ──
def _gw_beacon_pb(tmi):
    if tmi in (0, 1): return 520
    if 2 <= tmi <= 6: return 136
    if 7 <= tmi <= 10: return 520
    if tmi in (11, 12): return 264
    if tmi in (13, 14): return 72
    return None

def _gw_sof_pb(tmi, ext):
    s = _gw_beacon_pb(tmi)
    if s: return s
    if 1 <= ext <= 6: return 520
    if 10 <= ext <= 14: return 136
    return None

def _nw_plc_pb(tmi, ext):
    if tmi in (0, 1): return 520
    if 2 <= tmi <= 6: return 136
    if 7 <= tmi <= 10: return 520
    if tmi in (11, 12): return 264
    if 1 <= ext <= 6: return 520
    if 10 <= ext <= 14: return 136
    return None

def _nw_rf_pb(pblen):
    return (16, 40, 72, 136, 264, 520)[pblen] if 0 <= pblen <= 5 else None

def _frame_len_match(mpdu, expect):
    return expect is not None and len(mpdu) == 16 + expect

def _vote_protocol(mpdu):
    """单帧协议判别: 信标/SOF 的 预计帧长==实际帧长 只在一种解释下成立时投票."""
    if len(mpdu) < 16:
        return None
    dt = mpdu[0] & 0x07
    if dt == 0:  # 信标: GW TMI@字节9高4bit; NW 载波 TMI@字节10高4bit / 无线 PBLen@字节11低4bit
        gw = _gw_beacon_pb(mpdu[9] >> 4)
        nw = _nw_plc_pb(mpdu[10] >> 4, 0) or _nw_rf_pb(mpdu[11] & 0x0F)
        g = _frame_len_match(mpdu, gw); w = _frame_len_match(mpdu, nw)
        if g and not w: return 'gw'
        if w and not g: return 'nw'
    elif dt == 1 and len(mpdu) > 12:  # SOF
        gsz = _gw_sof_pb(mpdu[11] >> 4, mpdu[12] & 0x0F)
        g = _frame_len_match(mpdu, (mpdu[9] >> 4) * gsz if gsz else None)
        if (mpdu[7] & 0x0F) == 1:
            wsz_plc = _nw_plc_pb(mpdu[7] >> 4, mpdu[12] & 0x0F)
            w1 = _frame_len_match(mpdu, wsz_plc)
        else:
            w1 = False
        wsz_rf = _nw_rf_pb(mpdu[6] >> 4)
        w2 = _frame_len_match(mpdu, wsz_rf)
        if g and not (w1 or w2): return 'gw'
        if (w1 or w2) and not g: return 'nw'
    return None

def detect_protocol(frames):
    """按帧结构投票识别协议; 无信号/平票回退国网 GW_2022 (兼容旧行为)."""
    votes = {'gw': 0, 'nw': 0}
    for f in frames:
        if len(f) < 10:
            continue
        v = _vote_protocol(f[10:])
        if v:
            votes[v] += 1
    return 'nw' if votes['nw'] > votes['gw'] else 'gw', votes

def convert(bin_path, pcap_path=None, proto=None):
    data = open(bin_path, 'rb').read()
    if len(data) < 8:
        print("文件过短"); return 1

    # 文件头 BCD 时间作为基准
    hdr = data[:8]
    base_dt = time.mktime((2000 + bcd(hdr[0]), bcd(hdr[1]), bcd(hdr[2]),
                           bcd(hdr[3]), bcd(hdr[4]), bcd(hdr[5]), 0, 0, -1))
    base_us = int(base_dt) * 1000000 + (bcd(hdr[6]) * 100 + bcd(hdr[7])) * 1000

    body = data[8:]
    frames = []
    i, n = 0, len(body)
    while i < n:
        if body[i] != 0x3C:
            i += 1; continue
        i += 1
        esc = bytearray()
        while i < n and body[i] != 0x3E:
            b = body[i]
            if b == 0x3D and i + 1 < n:
                i += 1
                esc.append(0xFF ^ body[i])
            else:
                esc.append(b)
            i += 1
        i += 1  # 跳过 0x3E
        frames.append(bytes(esc))

    # 生成 pcap
    if pcap_path is None:
        pcap_path = os.path.splitext(bin_path)[0] + '.pcap'

    # 探测首帧 isRF 决定载波/无线; 协议(GW/NW)决定 USER 组:
    #   GW_2022: 载波 147 (USER0) / 无线 148 (USER1); NW_2021: 载波 149 (USER2) / 无线 150 (USER3)
    is_rf = False
    for f in frames:
        if len(f) > 9:
            is_rf = (f[9] != 0)
            break
    if proto is None:
        proto, votes = detect_protocol(frames)
        print(f"协议识别: {proto.upper()} (国网 {votes['gw']} 票 / 南网 {votes['nw']} 票)")
    elif proto not in ('gw', 'nw'):
        print(f"未知协议参数: {proto} (可选 gw/nw)"); return 1
    linktype = (149 if proto == 'nw' else 147) if not is_rf else (150 if proto == 'nw' else 148)

    # pcap 全局头
    gheader = struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, linktype)
    out = bytearray(gheader)

    # 时间轴 (依据 serialreader.cpp 权威定义):
    #   首帧 frame_time = 文件头 8B 标注时刻
    #   后续帧 = 上一帧 + (本帧NTB - 上一帧NTB)×40ns
    #   帧间 NTB 差超 kMaxNtbGapTicks(60s) 或回绕 → 断点, 重设基准
    kMaxNtbGapTicks = 25000000 * 60
    last_ft_us = None   # 上一帧绝对时间(µs)
    last_ntb = None     # 上一帧 NTB
    emitted = 0
    skipped = 0

    for f in frames:
        if len(f) < 6 + 4:
            skipped += 1
            continue
        ntb = f[2] | (f[3] << 8) | (f[4] << 16) | (f[5] << 24)
        mpdu = f[6 + 4:]   # 跳过 data_len + timestamp + 媒介头4B (载荷=纯 MPDU)
        if not mpdu:
            skipped += 1
            continue

        if last_ft_us is None:
            # 首帧 = 文件头时刻
            abs_us = base_us
        else:
            dn = (ntb - last_ntb) & 0xFFFFFFFF   # 回绕安全的无符号差
            if 0 < dn <= kMaxNtbGapTicks:
                abs_us = last_ft_us + dn * 40 // 1000
            else:
                # 断点: 无法用 NTB 差推进, 沿用上一帧时间(帧间 delta=0)
                abs_us = last_ft_us

        out += struct.pack('<IIII', abs_us // 1000000, abs_us % 1000000,
                           len(mpdu), len(mpdu))
        out += mpdu
        last_ft_us = abs_us
        last_ntb = ntb
        emitted += 1

    open(pcap_path, 'wb').write(bytes(out))
    print(f"帧总数: {len(frames)}, 输出 {emitted} 帧, 跳过 {skipped} 帧")
    print(f"pcap 写入: {pcap_path} ({len(out)} 字节)")
    return 0

if __name__ == '__main__':
    proto = None
    paths = []
    for a in sys.argv[1:]:
        if a == '--nw': proto = 'nw'
        elif a == '--gw': proto = 'gw'
        else: paths.append(a)
    src = paths[0] if paths else \
        r"C:\Users\work\Desktop\645工具\BPLC_20260909_235120.bin"
    dst = paths[1] if len(paths) > 1 else None
    sys.exit(convert(src, dst, proto))
