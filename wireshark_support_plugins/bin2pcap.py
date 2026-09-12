"""BPLC bin 回放文件 → pcap 转换器

bin 格式 (依据 BPLC_STA_QtMonitor 的 BplcParser::decode_envelope):
  文件头 8B BCD 时间标注 (年 月 日 时 分 秒 毫秒高 毫秒低)
  + 0x3C...0x3E 哨兵帧流 (帧内 0x3C/0x3D/0x3E 转义为 0x3D + (0xFF^字节))

每帧反转义后:
  data_len(2B 小端) + timestamp(4B 小端 NTB) + 媒介头4B(phr_mcs/option/channel/isRF) + 纯MPDU

pcap: linktype=147 (DLT_USER0/HPLC) 或 148 (DLT_USER1/RF), 由帧内 isRF 决定
     载荷=纯MPDU, 时间戳=NTB换算(40ns/tick, 处理回绕)
"""
import struct, sys, os, time

def bcd(b):
    return ((b >> 4) & 0xF) * 10 + (b & 0xF)

def convert(bin_path, pcap_path=None):
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

    # 探测首帧 isRF 决定 linktype: 0=HPLC→147(DLT_USER0), 非0=RF→148(DLT_USER1)
    linktype = 147
    for f in frames:
        if len(f) > 9:
            if f[9] != 0:
                linktype = 148
            break

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
    src = sys.argv[1] if len(sys.argv) > 1 else \
        r"C:\Users\work\Desktop\645工具\BPLC_20260909_235120.bin"
    sys.exit(convert(src))
