#!/usr/bin/env python3
"""BPLC bin(0x3C..0x3E 帧流) → 标准 pcap (linktype 149 / USER2 载波, 南网 NW_2021) 转换。

用途: 构造帧数据集(如 samples/test_topology_nw_p2.bin)需要 pcap 形态才能直接
被 tshark / Wireshark 插件回归。bin 里每帧是 wrap_frame() 的封装:
    0x3C + special_handle(le16(len) + le32(ts) + media3B + isRF1B + MPDU) + 0x3E
pcap 里存的是剥壳后的纯 MPDU。

用法: py bin_to_pcap.py <in.bin> <out.pcap> [dlt]   # dlt 默认 149(USER2 载波, packet-nw_2021.lua 注册 encap 47)
"""
import struct
import sys


def unescape(buf: bytes) -> bytes:
    """special_handle 反向: 0x3D + (0xFF ^ b) → b"""
    out = bytearray()
    i = 0
    while i < len(buf):
        b = buf[i]
        if b == 0x3D:
            i += 1
            out.append(buf[i] ^ 0xFF)
        else:
            out.append(b)
        i += 1
    return bytes(out)


def read_frames(bin_path):
    data = open(bin_path, "rb").read()
    if len(data) < 8:
        raise SystemExit("bin 太短")
    data = data[8:]                      # 跳过 8B BCD 时间头
    frames = []
    i = 0
    while i < len(data):
        if data[i] != 0x3C:
            raise SystemExit(f"偏移 {i} 期望 0x3C, 实得 0x{data[i]:02X}")
        i += 1
        buf = bytearray()
        while i < len(data) and data[i] != 0x3E:
            b = data[i]
            if b == 0x3D:
                i += 1
                buf.append(data[i] ^ 0xFF)
            else:
                buf.append(b)
            i += 1
        i += 1                            # 跳过 0x3E
        # buf 已在上面的循环里完成反转义, 不可再解一次
        inner = bytes(buf)
        ilen = struct.unpack("<H", inner[0:2])[0]
        # inner = le16(len) + le32(ts) + media3B + isRF1B + MPDU
        #   len 字段覆盖 ts 之后全部 = 4(ts) + 4(media+isRF) + len(MPDU)
        # pcap 帧用纯 MPDU(不含 4B media 头), 配 linktype 149(USER2, 载波)
        #   依据: packet-nw_2021.lua 注册 wtap_encap 47(→pcap 149 USER2 载波)/48(→150 无线)
        #   /50(→151 串口混合采集), 其中 47/48 期望纯 MPDU; 仓库既有 pcap 亦用 149
        #   (如 wireshark_support_plugins/test_nw_frames.pcap)
        #   注: 152 + 含 media 头的帧同样可被 tshark 解析(encap 层会剥离 media 头),
        #   但 149 + 纯 MPDU 与仓库惯例一致, 且避免依赖 media 头字段布局
        mpdu_len = ilen - 8
        if mpdu_len < 0 or 10 + mpdu_len > len(inner):
            raise SystemExit(f"帧长度异常: ilen={ilen} inner={len(inner)}")
        frames.append(bytes(inner[10:10 + mpdu_len]))
    return frames


def build_pcap(payloads, dlt=149):
    gheader = struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, dlt)
    out = gheader
    for p in payloads:
        out += struct.pack("<IIII", 0, 0, len(p), len(p)) + p
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    dlt = int(sys.argv[3]) if len(sys.argv) > 3 else 149
    frames = read_frames(src)
    data = build_pcap(frames, dlt)
    with open(dst, "wb") as f:
        f.write(data)
    print(f"{dst}: {len(frames)} 帧, {len(data)} B, linktype={dlt}")
    for i, fr in enumerate(frames, 1):
        print(f"  [{i}] {len(fr)}B")


if __name__ == "__main__":
    main()
