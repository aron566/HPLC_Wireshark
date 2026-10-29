#!/usr/bin/env python3
"""Split GW_2026 0x3C capture into [u32 LE len][isRF 1B][MPDU] records.

Container: 8B segment annotations (BCD, never contain 0x3C/0x3D/0x3E)
+ 0x3C frames with 0x3D escaping (0x3D + b -> 0xFF - b, serialreader.cpp).
Inner after unescape: [len u16 LE][NTB 4B][media 4B][MPDU], media[3] != 0 => RF.
len == 4 (NTB) + len(MPDU); validated per frame.
"""
import struct
import sys

SRC = "/home/hatch/workspace/your_files/BPLC_20260914_105445.bin"
DST = "/home/hatch/workspace/gw_learn_probe/gw_mpdus.bin"


def unescape(raw: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(raw):
        b = raw[i]
        if b == 0x3D and i + 1 < len(raw):
            i += 1
            out.append(0xFF - raw[i])
        else:
            out.append(b)
        i += 1
    return bytes(out)


def main():
    data = open(SRC, "rb").read()
    recs = []
    bad = 0
    i = 0
    n = len(data)
    while i < n:
        j = data.find(b"\x3c", i)
        if j < 0:
            break
        k = data.find(b"\x3e", j + 1)
        if k < 0:
            break
        inner = unescape(data[j + 1:k])
        i = k + 1
        if len(inner) < 10:
            bad += 1
            continue
        ln = struct.unpack_from("<H", inner, 0)[0]
        if len(inner) != 2 + ln + 4:
            bad += 1
            continue
        is_rf = 1 if inner[9] != 0 else 0
        mpdu = inner[10:]
        assert len(mpdu) == ln - 4, (len(mpdu), ln)
        recs.append((is_rf, mpdu))
    with open(DST, "wb") as f:
        for is_rf, mpdu in recs:
            body = bytes([is_rf]) + mpdu
            f.write(struct.pack("<I", len(body)))
            f.write(body)
    print(f"frames={len(recs)} bad={bad} bytes={len(data)}")
    # quick breakdown by first MPDU byte is left to the C++ probe


if __name__ == "__main__":
    main()
