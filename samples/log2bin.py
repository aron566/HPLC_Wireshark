#!/usr/bin/env python3
"""log2bin.py - 把 BPLCMonitorGW 的 .log 转回原始字节流 .bin

逆向 main.py 的写日志逻辑,从 log 还原出 0x3C...0x3E 哨兵格式的字节流,
供 Qt 上位机 FilePlayback 模式回放测试用。

格式对应:
  原始帧 = 0x3C + 转义后的 [Len(2)|Timestamp(4)|媒介头(4)|payload] + 0x3E

log 里的 hex 行 = payload = isRF 字节 + MPDU 字节
"""

import sys
import re
import struct

HEX_LINE_RE = re.compile(r'^(0x[0-9a-fA-F]{2}\s*)+$')
SEP_LINE_RE = re.compile(r'^\[(\*{20})')  # [**********
TIME_LINE_RE = re.compile(r'^TIME:\s+(.*?)\s+get timestamp is 0x([0-9a-fA-F]+)\s+\(([-0-9.]+)\)')
PARA_HPLC_RE = re.compile(r'^PARA:\s*baud\s*=\s*(\d+)')
PARA_HRF_RE  = re.compile(
    r'^PARA:\s*phr_mcs\s*=\s*(\d+);\s*option\s*=\s*(\d+);\s*channel\s*=\s*(\d+)')


def special_handle(buf: bytes) -> bytes:
    """与 main.py::comdrv.send 一致:0x3C/0x3D/0x3E 转义为 0x3D + ~byte"""
    out = bytearray()
    for b in buf:
        if b in (0x3C, 0x3D, 0x3E):
            out.append(0x3D)
            out.append(0xFF ^ b)
        else:
            out.append(b)
    return bytes(out)


def parse_hex_line(line: str) -> bytes:
    """'0x01 0x02 0x03' → b'\\x01\\x02\\x03'"""
    parts = line.strip().split()
    return bytes(int(p, 16) for p in parts)


def convert(src_log: str, dst_bin: str) -> int:
    """返回成功转换的帧数。"""
    with open(src_log, 'r', encoding='utf-8', errors='ignore') as fin, \
         open(dst_bin, 'wb') as fout:
        frame_count = 0
        timestamp = 0
        is_rf = False
        media = (0, 0, 0)  # phr_mcs, option, channel

        lines = fin.readlines()
        i = 0
        while i < len(lines):
            line = lines[i].rstrip('\n')
            i += 1

            # 时间戳行
            m = TIME_LINE_RE.match(line)
            if m:
                timestamp = int(m.group(2), 16)
                continue

            # 媒介参数行
            m_h = PARA_HRF_RE.match(line)
            m_p = PARA_HPLC_RE.match(line)
            if m_h:
                phr_mcs = int(m_h.group(1)) & 0xFF
                option  = int(m_h.group(2)) & 0xFF
                channel = int(m_h.group(3)) & 0xFF
                media = (phr_mcs, option, channel)
                is_rf = True
                continue
            if m_p:
                baud = int(m_p.group(1))
                # baud < 256 时是 band(0-3),直接作为 channel
                # baud 数值较大时仍是 PLC,但 channel = baud
                # 这里简化:baud 直接当 channel 用(原 STA 行为)
                channel = baud & 0xFF
                media = (0, 0, channel)  # phr_mcs/option 占位 0
                is_rf = False
                continue

            # hex 载荷行
            if HEX_LINE_RE.match(line):
                payload = parse_hex_line(line)
                if not payload:
                    continue
                # main.py 的 log hex 行 = data(弹掉 phr_mcs/option/channel 后)
                #   = [isRF 1B] + MPDU 净荷,首字节即 isRF(0=PLC,1=HRF)。
                # 媒介头只需补 3 字节(phr_mcs/option/channel),与线上帧一致:
                #   0x3C | Len(2) | TS(4) | phr_mcs | option | channel | isRF | MPDU... | 0x3E
                media_header = bytes([media[0], media[1], media[2] & 0xFF])

                # 帧内部 = Len(2 LE) + Timestamp(4 LE) + 媒介头(3) + payload(含 isRF)
                inner_len = len(payload) + 3
                inner = struct.pack('<HI', inner_len, timestamp) + media_header + payload

                # 整体 = 0x3C + 转义(inner) + 0x3E
                frame = bytes([0x3C]) + special_handle(inner) + bytes([0x3E])
                fout.write(frame)
                frame_count += 1
                continue

        return frame_count


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.log> <output.bin>")
        sys.exit(1)
    n = convert(sys.argv[1], sys.argv[2])
    print(f"Converted {n} frames to {sys.argv[2]}")


if __name__ == '__main__':
    main()
