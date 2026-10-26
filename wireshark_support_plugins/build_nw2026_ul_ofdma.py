#!/usr/bin/env python3
"""Build spec-conforming NW_2026 (SPLC) OFDMA type-1 frames + trigger companion.

Source: SPLC part 4 (data link layer) md tables, 261023 version, in-repo at
docs/protocol_doc/nw/SPLC/:
- Table 26 OFDMA FC variable field: multi (4,0,1); OFDMA type (4,1,2):
  0 DL-OFDMA / 1 DL-OFDMA SACK or UL-OFDMA / 2 UL-OFDMA trigger / 3 UL-OFDMA
  SACK; band (4,3,3); station count (4,6,2): 0=1,1=2,2=3,3=4 stations;
  eFC symbol count (5,0,2); PL symbol count (6,0,9); frame length (8,0,12)
  x10us; SNID high (12,0,1); version (12,4,4) = 2 for SPLC.
- Type 1 carries NO eFC. DL-OFDMA SACK sub-case: single PB, fixed TMI 26
  (16B block, table 49/50). UL-OFDMA sub-case: payload carries data, PB TMI
  indicated by the trigger frame's eFC TMI (table 28).
- Table 28 trigger eFC (16B): TF count (0,0,2); per station: PB count 1b
  (0->1 block, 1->2 blocks), TEI 12b, TMI 5b, RU 4b, TxPowerBackoff 3b
  (every station, same geometry); CRC24 at bytes 13-15.
- PB block (table 47 + 5.1.3.2.1): head 4B (SeqNum 16b LE + agg flag byte2
  bit0 + RSV 15b) + body (pb_size-8) + RSV 1B + CRC24 3B; CRC covers the
  first pb_size-3 bytes (head+body+RSV), stored LE in the last 3 bytes.
- SPLC TMI -> PB size (nw_2026_pb_table.h, SPLC table 23 / PHY table 13):
  0-8 -> 520 / 9-15 -> 264 / 16-20 -> 136 / 21-25 -> 72 / 26-27 -> 16.
- FCH FCS: crc24_lsb over bytes 0..12, stored at 13..15 (same convention
  as the C++ parser's parse_mpdu_base: crc24_lsb(p, 16) vs LE p[13..15]).

Bit order: LSB-first continuous bitstream, matching get_bits() in the C++
code (field bit i -> byte (bit+i)//8, bit (bit+i)%8).
CRC: crc24_lsb (poly=0xC60001, init=0, LSB-first), as verified against
the C++ parser on the GW_2026 simulated frames.

Frames built (MPDU = FCH byte 0 onward; feed via from_raw with a leading
isRF=0 byte, or via the bare-hex replay entry):
1. trigger (type 2, 32B): FCH + table-28 eFC. Schedules 2 stations:
   sta0 TEI 2, TMI 18 (->136B), 1 PB, RU 5; sta1 TEI 3, TMI 21 (->72B),
   2 PBs, RU 6. No data PBs (correct: a trigger carries only eFC).
2. ul_data (type 1, 296B): FCH (src TEI 0xFFFF broadcast, dst TEI 1 CCO,
   multi=1, type=1, 2 stations, eFC-symbol field 0 = invalid/default per
   table 26 note, PL symbols 100, frame length 512 x10us) + NO eFC +
   station payloads from byte 16: sta0 1x136B PB + sta1 2x72B PBs, sizes
   and counts exactly as the trigger's eFC prescribes.
3. dl_sack (type 1, 32B): FCH (src 0xFFFF, dst 1, multi=1, type=1,
   1 station) + single 16B PB, fixed TMI 26: body MsgID 0 (table 49/50),
   SrcTEI 2, RxStatus 0b11 (both DL-OFDMA PBs OK).
4. trigger_hi (type 2, 32B) + ul_data_hi (type 1, 296B): same layout as
   1+2 but with SNID high bit set (FCH byte12 bit0 = 1, full SNID 0x10)
   and station TEIs 4/5. The pair proves the trigger-context bucket key
   uses the full 5-bit SNID: a low-SNID trigger must NOT serve a
   high-SNID UL frame (and vice versa).

Test values chosen in legal range (not document values): NID/SNID 0
(0x10 for the hi pair), TEI 1/2/3 (4/5 for the hi pair), RU 5/6,
PL symbols 100, frame lengths, letter-filled PB bodies, PB sequence
numbers.
"""

import os

OUT = os.path.dirname(os.path.abspath(__file__))


def set_bits(buf, byte, bit, length, value):
    for i in range(length):
        pos = byte * 8 + bit + i
        if (value >> i) & 1:
            buf[pos // 8] |= (1 << (pos % 8))
        else:
            buf[pos // 8] &= ~(1 << (pos % 8))


def get_bits(buf, byte, bit, length):
    v = 0
    for i in range(length):
        pos = byte * 8 + bit + i
        v |= ((buf[pos // 8] >> (pos % 8)) & 1) << i
    return v


def crc24_lsb(d, length):
    """d: bytes-like; length = full length incl. 3 CRC bytes; crc over first len-3."""
    poly = 0xC60001
    crc = 0
    for i in range(length - 3):
        for j in range(8):
            bit_in = (d[i] >> j) & 1
            bit_lsb = crc & 1
            crc >>= 1
            if bit_in ^ bit_lsb:
                crc ^= poly
    return crc & 0xFFFFFF


def store_crc24(buf, start, length):
    """Compute crc over buf[start:start+length] (first len-3 bytes), store LE
    in the last 3 bytes of that span."""
    c = crc24_lsb(bytes(buf[start:start + length]), length)
    buf[start + length - 3] = c & 0xFF
    buf[start + length - 2] = (c >> 8) & 0xFF
    buf[start + length - 1] = (c >> 16) & 0xFF
    return c


def fch_base(src_tei, dst_tei, ofdma_type, station_count_code, pl_symbols,
             frame_len, snid_low=0, snid_high=0):
    """16B OFDMA FCH (table 26) with FCS filled. Band 0, version 2."""
    f = bytearray(16)
    set_bits(f, 0, 0, 3, 1)          # delimiter type 1 = SOF
    set_bits(f, 0, 3, 1, 0)          # net type 0
    set_bits(f, 0, 4, 4, snid_low)   # SNID low 4b
    set_bits(f, 1, 0, 8, src_tei & 0xFF)
    set_bits(f, 2, 0, 4, (src_tei >> 8) & 0xF)
    set_bits(f, 2, 4, 4, dst_tei & 0xF)
    set_bits(f, 3, 0, 8, (dst_tei >> 4) & 0xFF)
    set_bits(f, 4, 0, 1, 1)          # multi-site
    set_bits(f, 4, 1, 2, ofdma_type)
    set_bits(f, 4, 3, 3, 0)          # band
    set_bits(f, 4, 6, 2, station_count_code)
    set_bits(f, 5, 0, 2, 0)          # eFC symbol count (0 for UL: invalid/default)
    set_bits(f, 6, 0, 9, pl_symbols)
    set_bits(f, 8, 0, 12, frame_len)
    set_bits(f, 12, 0, 1, snid_high)  # SNID high bit (table 26 variable region)
    set_bits(f, 12, 4, 4, 2)         # standard version 2 = SPLC
    store_crc24(f, 0, 16)
    return f


def pb_block(pb_size, seq, body_pattern):
    """One SPLC PB: head 4B + body (pb_size-8) + RSV 1B + CRC24 3B."""
    b = bytearray(pb_size)
    b[0] = seq & 0xFF
    b[1] = (seq >> 8) & 0xFF
    b[2] = 0x00                      # agg flag bit0 = 0, RSV
    b[3] = 0x00
    body_len = pb_size - 8
    pat = body_pattern.encode()
    for i in range(body_len):
        b[4 + i] = pat[i % len(pat)]
    b[pb_size - 4] = 0x00            # tail RSV
    store_crc24(b, 0, pb_size)
    return b


def build_trigger():
    """Type 2, 32B: FCH + table-28 eFC (2 stations)."""
    fch = fch_base(src_tei=1, dst_tei=0xFFFF, ofdma_type=2,
                   station_count_code=1, pl_symbols=100, frame_len=0x200)
    e = bytearray(16)
    # eFC bit cursor: TF count 2b, then per station PB 1b + TEI 12b + TMI 5b
    # + RU 4b (+ TxPowerBackoff 3b for station 0 in the code's reading).
    bp = 0

    def put(nbits, value):
        nonlocal bp
        set_bits(e, bp // 8, bp % 8, nbits, value)
        bp += nbits

    put(2, 0)      # TF count code 0 -> actual (0+1)*2 = 2
    # station 0: 1 PB, TEI 2, TMI 18 (->136B), RU 5, TxBackoff 0
    put(1, 0)
    put(12, 2)
    put(5, 18)
    put(4, 5)
    put(3, 0)
    # station 1: 2 PBs, TEI 3, TMI 21 (->72B), RU 6, TxBackoff 0
    put(1, 1)
    put(12, 3)
    put(5, 21)
    put(4, 6)
    put(3, 0)
    # stations 2/3 unused + reserved: zeros; CRC over e[0..12]
    store_crc24(e, 0, 16)
    return bytes(fch) + bytes(e)


def build_ul_data():
    """Type 1 UL-OFDMA data, 296B: FCH + NO eFC + sta0 1x136B + sta1 2x72B."""
    fch = fch_base(src_tei=0xFFFF, dst_tei=1, ofdma_type=1,
                   station_count_code=1, pl_symbols=100, frame_len=0x200)
    out = bytearray(fch)
    out += pb_block(136, 0, "UL-STA0-")
    out += pb_block(72, 0, "UL-STA1-PB0-")
    out += pb_block(72, 1, "UL-STA1-PB1-")
    return bytes(out)


def build_trigger_hi():
    """Type 2, 32B, SNID high bit set (full SNID 0x10): stations TEI 4/5."""
    fch = fch_base(src_tei=1, dst_tei=0xFFFF, ofdma_type=2,
                   station_count_code=1, pl_symbols=100, frame_len=0x200,
                   snid_low=0, snid_high=1)
    e = bytearray(16)
    bp = 0

    def put(nbits, value):
        nonlocal bp
        set_bits(e, bp // 8, bp % 8, nbits, value)
        bp += nbits

    put(2, 0)
    put(1, 0)
    put(12, 4)
    put(5, 18)
    put(4, 5)
    put(3, 0)
    put(1, 1)
    put(12, 5)
    put(5, 21)
    put(4, 6)
    put(3, 0)
    store_crc24(e, 0, 16)
    return bytes(fch) + bytes(e)


def build_ul_data_hi():
    """Type 1 UL-OFDMA data, 296B, SNID high bit set: sta0 1x136B + sta1 2x72B."""
    fch = fch_base(src_tei=0xFFFF, dst_tei=1, ofdma_type=1,
                   station_count_code=1, pl_symbols=100, frame_len=0x200,
                   snid_low=0, snid_high=1)
    out = bytearray(fch)
    out += pb_block(136, 0, "UL-HI-STA0-")
    out += pb_block(72, 0, "UL-HI-STA1-PB0-")
    out += pb_block(72, 1, "UL-HI-STA1-PB1-")
    return bytes(out)


def build_trigger_tmi26():
    """Type 2, 32B, 1 station: TEI 6, TMI 26 (->16B), 1 PB.

    Companion for ul_single16: proves a single-station 1x16B UL frame with
    MsgID != 0 is NOT a DL-SACK (parser must take the UL path, tree/list
    must not label it DL-SACK).
    """
    fch = fch_base(src_tei=1, dst_tei=0xFFFF, ofdma_type=2,
                   station_count_code=0, pl_symbols=100, frame_len=0x200)
    e = bytearray(16)
    bp = 0

    def put(nbits, value):
        nonlocal bp
        set_bits(e, bp // 8, bp % 8, nbits, value)
        bp += nbits

    put(2, 0)
    put(1, 0)
    put(12, 6)
    put(5, 26)
    put(4, 5)
    put(3, 0)
    store_crc24(e, 0, 16)
    return bytes(fch) + bytes(e)


def build_ul_single16():
    """Type 1 UL-OFDMA data, 32B (same length as a DL-SACK frame):
    single 1x16B PB, body first nibble MsgID=5 (not 0), so the parser must
    take the UL path, not the DL-SACK path."""
    fch = fch_base(src_tei=0xFFFF, dst_tei=1, ofdma_type=1,
                   station_count_code=0, pl_symbols=4, frame_len=0x100)
    pb = bytearray(16)
    pb[0] = 0x00
    pb[1] = 0x00
    pb[2] = 0x00
    pb[3] = 0x00
    pb[4] = 0x15                     # MsgID 5, payload nibble 1
    pat = b"UL-SINGLE16-"
    for i in range(8):
        pb[5 + i] = pat[i % len(pat)]
    pb[12] = 0x00                    # tail RSV
    store_crc24(pb, 0, 16)
    return bytes(fch) + bytes(pb)


def build_dl_sack():
    """Type 1 DL-OFDMA SACK, 32B: FCH + single 16B PB, fixed TMI 26."""
    fch = fch_base(src_tei=0xFFFF, dst_tei=1, ofdma_type=1,
                   station_count_code=0, pl_symbols=4, frame_len=0x100)
    pb = bytearray(16)
    pb[0] = 0x00                     # SeqNum low
    pb[1] = 0x00                     # SeqNum high
    pb[2] = 0x00                     # agg 0 + RSV
    pb[3] = 0x00
    # body (table 49/50): MsgID 0 (bits0-3) + SrcTEI 12b (bits4-7 + next byte)
    # + RxStatus 2b (0b11 = both DL-OFDMA PBs OK) + RSV
    pb[4] = 0x20                     # MsgID 0, SrcTEI low 4 = 0010 (TEI 2)
    pb[5] = 0x00                     # SrcTEI high 8 = 0
    pb[6] = 0x03                     # RxStatus 0b11
    pb[7] = 0x00
    pb[8] = 0x00
    pb[9] = 0x00
    pb[10] = 0x00
    pb[11] = 0x00
    pb[12] = 0x00                    # tail RSV
    store_crc24(pb, 0, 16)
    return bytes(fch) + bytes(pb)


def build_dl_ofdma():
    """Type 0 DL-OFDMA, 168B: FCH + table-27 eFC + sta0 1x136B PB.

    eFC (table 27, same geometry as table 28): TF count 2b + sta0 PB 1b +
    TEI 12b + TMI 5b + RU 4b + SACK RU 3b; CRC24 at bytes 13-15. PB at byte
    32 (FCH 16 + eFC 16).
    """
    fch = fch_base(src_tei=1, dst_tei=0xFFFF, ofdma_type=0,
                   station_count_code=0, pl_symbols=100, frame_len=0x200)
    e = bytearray(16)
    bp = 0

    def put(nbits, value):
        nonlocal bp
        set_bits(e, bp // 8, bp % 8, nbits, value)
        bp += nbits

    put(2, 0)      # TF count code 0
    put(1, 0)      # sta0 PB 1 (0 -> 1 block)
    put(12, 2)     # TEI 2
    put(5, 18)     # TMI 18 -> 136B
    put(4, 5)      # RU 5
    put(3, 0)      # SACK RU 0
    store_crc24(e, 0, 16)
    out = bytearray(fch) + bytearray(e)
    out += pb_block(136, 0, "DL-OFDMA-")
    return bytes(out)


def build_dl_ofdma_badefc():
    """DL-OFDMA with eFC body corrupted (1 bit flipped, FCH CRC intact)."""
    d = bytearray(build_dl_ofdma())
    d[16 + 5] ^= 0x01    # flip 1 bit in eFC body (not the CRC bytes 13-15)
    return bytes(d)


def verify(frame, name):
    """Independent re-check: FCH FCS + per-block PBCS + table-26 field reads."""
    fch_ok = crc24_lsb(frame, 16) == (frame[13] | (frame[14] << 8) | (frame[15] << 16))
    print(f"{name}: len={len(frame)} FCH FCS {'OK' if fch_ok else 'FAIL'} "
          f"ver={get_bits(frame, 12, 4, 4)} multi={get_bits(frame, 4, 0, 1)} "
          f"type={get_bits(frame, 4, 1, 2)} stations={get_bits(frame, 4, 6, 2) + 1} "
          f"src=0x{get_bits(frame, 1, 0, 12):03x} "
          f"dst=0x{get_bits(frame, 2, 4, 12):03x} "
          f"snid=0x{(get_bits(frame, 12, 0, 1) << 4) | get_bits(frame, 0, 4, 4):02x}")
    return fch_ok


def main():
    trig = build_trigger()
    ul = build_ul_data()
    sack = build_dl_sack()
    trig_hi = build_trigger_hi()
    ul_hi = build_ul_data_hi()
    trig26 = build_trigger_tmi26()
    ul16 = build_ul_single16()
    dl = build_dl_ofdma()
    dl_bad = build_dl_ofdma_badefc()
    for data, name, fn in ((trig, "trigger", "nw2026_trigger.bin"),
                           (ul, "ul_data", "nw2026_ul_ofdma_data.bin"),
                           (sack, "dl_sack", "nw2026_dl_sack.bin"),
                           (trig_hi, "trigger_hi", "nw2026_trigger_hisnid.bin"),
                           (ul_hi, "ul_data_hi", "nw2026_ul_ofdma_data_hisnid.bin"),
                           (trig26, "trigger_tmi26", "nw2026_trigger_tmi26.bin"),
                           (ul16, "ul_single16", "nw2026_ul_single16.bin"),
                           (dl, "dl_ofdma", "nw2026_dl_ofdma.bin"),
                           (dl_bad, "dl_ofdma_badefc", "nw2026_dl_ofdma_badefc.bin")):
        with open(os.path.join(OUT, fn), "wb") as fh:
            fh.write(data)
        ok = verify(data, name)
        # PB CRC re-check per segment layout
        if name in ("ul_data", "ul_data_hi"):
            for off, sz, tag in ((16, 136, "sta0"), (152, 72, "sta1-pb0"),
                                 (224, 72, "sta1-pb1")):
                blk = data[off:off + sz]
                good = crc24_lsb(blk, sz) == (blk[sz - 3] | (blk[sz - 2] << 8) | (blk[sz - 1] << 16))
                print(f"  {tag} @{off} size={sz} PBCS {'OK' if good else 'FAIL'}")
        elif name == "ul_single16":
            blk = data[16:32]
            good = crc24_lsb(blk, 16) == (blk[13] | (blk[14] << 8) | (blk[15] << 16))
            print(f"  single PB @16 size=16 PBCS {'OK' if good else 'FAIL'} "
                  f"MsgID={blk[4] & 0x0F} (must be != 0 to avoid the DL-SACK path)")
        elif name == "dl_sack":
            blk = data[16:32]
            good = crc24_lsb(blk, 16) == (blk[13] | (blk[14] << 8) | (blk[15] << 16))
            body = blk[4:12]
            msg = body[0] & 0x0F
            tei = ((body[0] >> 4) & 0xF) | (body[1] << 4)
            rx = body[2] & 0x03
            print(f"  sack PB @16 size=16 PBCS {'OK' if good else 'FAIL'} "
                  f"MsgID={msg} SrcTEI={tei} RxStatus=0b{rx:02b}")
        elif name in ("trigger", "trigger_hi", "trigger_tmi26"):
            e = data[16:32]
            good = crc24_lsb(e, 16) == (e[13] | (e[14] << 8) | (e[15] << 16))
            print(f"  eFC @16 size=16 CRC {'OK' if good else 'FAIL'} "
                  f"(TFcode={get_bits(e, 0, 0, 2)}, sta0: PB={get_bits(e, 0, 2, 1) + 1} "
                  f"TEI={get_bits(e, 0, 3, 12)} TMI={get_bits(e, 1, 7, 5)} "
                  f"RU={get_bits(e, 2, 4, 4)}, sta1: PB={get_bits(e, 3, 3, 1) + 1} "
                  f"TEI={get_bits(e, 3, 4, 12)} TMI={get_bits(e, 5, 0, 5)} "
                  f"RU={get_bits(e, 5, 5, 4)})")
        assert ok, name


if __name__ == "__main__":
    main()
