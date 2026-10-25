#!/usr/bin/env python3
"""生成含 IE 类型 1/2/3 的单跳无线发现列表帧(表214/215), 用于验证 Lua 字段树展开。

单跳帧结构: MAC 单跳头4B(version=2,msg_type=2,msdu_len) + MSDU + ICV(CRC32)
MSDU = 站点MAC(6B) + 统计序号(1B) + IE TLV 序列(type 1B + len 1B + content)
  IE0 = 站点属性(表217, 14B)   — 已由 build_singlehop_rfdl 覆盖
  IE1 = 站点路由信息(表218)    — 每条 2B = 下一跳TEI(12b) + 路由类型(4b)
  IE2 = 邻居信道非位图(表219/220) — combo(4b)+RSV(4b) + 每邻居 TEI12b+接收率8b+[SNR]+[RSSI]
  IE3 = 邻居信道位图(表226/227)   — combo(4b)+RSV(4b) + 每组 起始TEI12b+RSV4b+位图大小1B+位图+条目
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_nw_bplc_frames import le16, le32, put_tei12, build_sof, crc32, STA_MAC, wrap_frame


def bit_pack(fields):
    """fields: [(value, nbits), ...] LSB-first 打包为 bytes。"""
    out = bytearray()
    acc = 0
    n = 0
    for v, bits in fields:
        for i in range(bits):
            if (v >> i) & 1:
                acc |= 1 << n
            n += 1
            if n == 8:
                out.append(acc)
                acc = 0
                n = 0
    if n > 0:
        out.append(acc)
    return bytes(out)


def route_entry(tei, rtype):
    """表218 单条: 下一跳TEI(12b @ byte0 bit0) + 路由类型(4b @ byte1 bit4)。"""
    b = bytearray(2)
    put_tei12(b, 0, tei)
    b[1] |= (rtype & 0x0F) << 4
    return bytes(b)


def build_rfdl_ie123():
    src_tei = 1
    dst_tei = 2
    msdu = bytearray()
    msdu += STA_MAC               # 站点MAC 6B
    msdu += bytes([0x01])         # 统计序号 1B

    # IE1: 站点路由信息(表218) — 2 条 2B
    ie1 = bytearray()
    ie1 += route_entry(3, 2)      # NextTEI=3, RouteType=2(上级备份)
    ie1 += route_entry(4, 1)      # NextTEI=4, RouteType=1(同级备份)
    msdu += bytes([0x01])         # type=1
    msdu += bytes([len(ie1)])     # len=4
    msdu += bytes(ie1)

    # IE2: 邻居信道非位图(表219/220) — combo=2(SNR6b+RSSI6b), 2 条(每 32b 对齐)
    ie2 = bytearray()
    ie2 += bytes([0x02])          # combo=2(bits0-3), RSV=0(bits4-7)
    ie2 += bit_pack([(5, 12), (80, 8), (7, 6), (40, 6)])   # TEI=5, rcv=80, SNR=7, RSSI=40
    ie2 += bit_pack([(6, 12), (90, 8), (10, 6), (30, 6)])  # TEI=6, rcv=90, SNR=10, RSSI=30
    msdu += bytes([0x02])         # type=2
    msdu += bytes([len(ie2)])     # len=9 (1 头 + 4+4)
    msdu += bytes(ie2)

    # IE3: 邻居信道位图(表226/227) — combo=2(RSSI8b), 1 组, 2 邻居
    ie3 = bytearray()
    ie3 += bytes([0x02])          # combo=2(bits0-3), RSV=0(bits4-7)
    b = bytearray(2)
    put_tei12(b, 0, 7)            # StartTEI=7 (12b @ byte0)
    ie3 += bytes(b)               # byte1 bit4-7 = RSV(0)
    ie3 += bytes([1])             # bitmap_size = 1
    ie3 += bytes([0b00000101])    # bit0 + bit2 → TEI 7, 9
    ie3 += bit_pack([(70, 8), (100, 8)])  # TEI7: rcv=70, RSSI=100(→-11dBm)
    ie3 += bit_pack([(60, 8), (90, 8)])   # TEI9: rcv=60, RSSI=90(→-21dBm)
    msdu += bytes([0x03])         # type=3
    msdu += bytes([len(ie3)])
    msdu += bytes(ie3)

    msdu_len = len(msdu)
    mac = bytearray(4 + msdu_len + 4)
    mac[0] = 2 << 1               # version=2 (单跳)
    mac[1] = 2                    # msg_type=2 (无线发现列表)
    mac[2:4] = le16(msdu_len)
    mac[4:4 + msdu_len] = msdu
    mac[4 + msdu_len:4 + msdu_len + 4] = le32(crc32(bytes(mac[4:4 + msdu_len])))
    return build_sof(src_tei, dst_tei, bytes(mac), tmi=4)


if __name__ == "__main__":
    frame = build_rfdl_ie123()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nw_rfdl_ie123.bin")
    with open(out, "wb") as f:
        f.write(bytes(8))          # 8B 文件头占位
        f.write(wrap_frame(frame)) # 10B 封装头 + 帧
    print(f"written {out} ({len(frame)} bytes MPDU)")
