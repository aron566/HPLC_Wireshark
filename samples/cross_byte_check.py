"""跨字节位域正确性对照(从 replay_test.bin 取真实帧数据)。

在 Python 里用"Qt 修复后的 get_bits"算法重算 SOF 各字段,
与 Python 原版 BitDefine 结果逐字段对比 —— 若一致则 Qt 移植正确。
"""
import sys
import os

BPLCM_DIR = r"D:\code\HPLC_HRF\监控器\BPLCMonitor"
sys.path.insert(0, BPLCM_DIR)
sys.path.insert(0, os.path.join(BPLCM_DIR, "data"))

from MPDU_Class import BitDefine

BIN = r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\replay_test.bin"


def qt_get_bits(data, start_byte, start_bit, bit_len):
    """与 Qt bplcparser.cpp 修复后完全一致的算法"""
    bits_occupied = bit_len + start_bit
    bytes_floor = bits_occupied // 8
    bytes_occupied = bytes_floor + (1 if (bits_occupied % 8) != 0 else 0)
    result = 0
    for i in range(bytes_occupied):
        b = data[start_byte + i]
        if i == 0:
            b = (b >> start_bit) << start_bit
            b &= 0xFF
        if i == bytes_occupied - 1:
            keep = bits_occupied - (bytes_occupied - 1) * 8
            if keep < 8:
                b = b & ((1 << keep) - 1)
        result |= (b << (8 * i))
    return result >> start_bit


def py_get_bits(data, start_byte, start_bit, bit_len):
    bd = BitDefine(start_byte, start_bit, bit_len)
    bd.getdata(list(data))
    return bd.bit_field_content


def get_sof_payloads(max_frames=30):
    """从 bin 切帧取前几个 SOF 帧的纯 MPDU payload"""
    import comdrv
    driver = comdrv.comdrv(com=None, file_name=BIN, timeoutarg=0,
                           logflag=0, baudrate=460800)
    out = []
    while len(out) < max_frames:
        buf = driver.readtill3E()
        if not buf:
            break
        unesc = driver.array_post_handle(buf)
        if len(unesc) < 10 + 2:
            continue
        # unesc = Len(2)|TS(4)|phr(1)|opt(1)|ch(1)|isRF(1)|MPDU...
        payload = bytes(unesc[10:])      # MPDU(剥掉媒体头+isRF)
        if len(payload) < 20:
            continue
        # FrameType in payload[0] bit0..2 == 1 => SOF
        if (payload[0] & 0x07) == 1:
            out.append(payload)
    return out


def main():
    frames = get_sof_payloads()
    if not frames:
        print("no SOF frames found")
        return 1

    fields = [
        ("MPDU FrameType",     0, 0, 3),
        ("MPDU NetType",       0, 3, 5),
        ("MPDU NetID",         1, 0, 24),   # 跨 1-3
        ("MPDU Version",       12, 4, 4),
        ("MPDU CRC24",         13, 0, 24),  # 跨 13-15
        ("SOF SourceTEI",      4, 0, 12),   # 跨 4-5
        ("SOF DestTEI",        5, 4, 12),   # 跨 5-6,start_bit=4
        ("SOF LinkID",         7, 0, 8),
        ("SOF FrameLength",    8, 0, 12),   # 跨 8-9
        ("SOF PBNum",          9, 4, 4),
        ("SOF SymbolNum",      10, 0, 9),   # 跨 10-11
        ("SOF BroadCastFlag",  11, 1, 1),
        ("SOF ReSendFlag",     11, 2, 1),
        ("SOF EncrypFlag",     11, 3, 1),
        ("SOF TMI",            11, 4, 4),
        ("SOF TMI_EXT",        12, 0, 4),
    ]

    all_ok = True
    n = min(10, len(frames))
    print(f"checking {n} real SOF frames x {len(fields)} fields\n")
    for fi in range(n):
        d = frames[fi]
        for name, sb, sbit, blen in fields:
            qt = qt_get_bits(d, sb, sbit, blen)
            py = py_get_bits(d, sb, sbit, blen)
            if qt != py:
                all_ok = False
                print(f"[FAIL] f{fi} {name:<16} ({sb},{sbit},{blen}) "
                      f"qt=0x{qt:X} py=0x{py:X}")
    if all_ok:
        print("ALL MATCH")
        # 打印前3帧关键字段供人工核对
        for fi in range(min(3, n)):
            d = frames[fi]
            print(f"  f{fi}: src={qt_get_bits(d,4,0,12)} "
                  f"dst={qt_get_bits(d,5,4,12)} "
                  f"linkid={qt_get_bits(d,7,0,8)} "
                  f"flen={qt_get_bits(d,8,0,12)} "
                  f"pbnum={qt_get_bits(d,9,4,4)} "
                  f"sym={qt_get_bits(d,10,0,9)}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
