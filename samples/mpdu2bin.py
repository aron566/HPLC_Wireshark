#!/usr/bin/env python3
"""mpdu2bin.py - 纯 MPDU 字节流 -> 0x3C...0x3E 哨兵 .bin

按用户提供的线上帧布局:
    0x3C | dlen(2 LE) | ts(4 LE) | phr_mcs(1) | option(1) | channel(1)
        | isRF(1)    | MPDU...                       | 0x3E
                   ^----- dlen = len(MPDU) + 4  (含 media 3B + isRF 1B)

与 samples/log2bin.py 的关键差异:
  - log2bin.py 假设 .log hex 行第一字节 = isRF, 媒介头由 PARA 行提供
  - 本脚本 hex 行 = 纯 MPDU, MPDU 内容原样透传不裁剪
  - isRF 是独立 1 字节(MPDU 内不含), 由 --isRF 参数提供
  - media(phr_mcs/option/channel) 由 --phr_mcs/--option/--channel 参数提供

只做封装/转义/格式转换,不解读 MPDU 字段含义,不猜测首字节语义。
字段含义由用户提供/文档决定。
"""

import sys
import re
import struct
import argparse

HEX_LINE_RE = re.compile(r'^(0x[0-9a-fA-F]{2}\s*)+$')


def special_handle(buf: bytes) -> bytes:
    """与 log2bin.py / main.py::comdrv.send 一致:0x3C/0x3D/0x3E -> 0x3D + ~byte"""
    out = bytearray()
    for b in buf:
        if b in (0x3C, 0x3D, 0x3E):
            out.append(0x3D)
            out.append(0xFF ^ b)
        else:
            out.append(b)
    return bytes(out)


def parse_hex_line(line: str) -> bytes:
    parts = line.strip().split()
    return bytes(int(p, 16) for p in parts)


def convert(src_log: str, dst_bin: str, phr_mcs: int, option: int,
            channel: int, is_rf: int, default_ts: int) -> int:
    media_header = bytes([phr_mcs & 0xFF, option & 0xFF, channel & 0xFF])
    with open(src_log, 'r', encoding='utf-8', errors='ignore') as fin, \
         open(dst_bin, 'wb') as fout:
        frame_count = 0
        ts = default_ts
        lines = fin.readlines()
        for line in lines:
            line = line.rstrip('\n')
            if not HEX_LINE_RE.match(line):
                continue
            mpdu = parse_hex_line(line)
            if not mpdu:
                continue
            # 帧内布局: [dlen(2)][ts(4)][media(3)][isRF(1)][MPDU...]
            # dlen = len(MPDU) + 4  (含 media 3B + isRF 1B)
            # isRF 是独立 1 字节, MPDU 内不含
            # MPDU 内容原样透传
            inner_len = len(mpdu) + 4
            payload = bytes([is_rf & 0xFF]) + mpdu
            inner = struct.pack('<HI', inner_len, ts) + media_header + payload
            frame = bytes([0x3C]) + special_handle(inner) + bytes([0x3E])
            fout.write(frame)
            frame_count += 1
        return frame_count


def main():
    p = argparse.ArgumentParser(description='纯 MPDU .log -> 0x3C..0x3E .bin')
    p.add_argument('src_log')
    p.add_argument('dst_bin')
    p.add_argument('--phr_mcs', type=int, default=0)
    p.add_argument('--option', type=int, default=0)
    p.add_argument('--channel', type=int, default=1)
    p.add_argument('--isRF', type=int, default=0,
                   help='0=PLC, 1=HRF; 默认 0')
    p.add_argument('--timestamp', type=lambda s: int(s, 16), default=0,
                   help='4B LE 时间戳, 默认 0')
    args = p.parse_args()

    n = convert(args.src_log, args.dst_bin,
                args.phr_mcs, args.option, args.channel,
                args.isRF, args.timestamp)
    print(f"Converted {n} frames to {args.dst_bin}")


if __name__ == '__main__':
    main()