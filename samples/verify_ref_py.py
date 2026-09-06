"""权威对照:用 Python 原版 MPDU_Process 解析 replay_test.bin,得到帧类型分布。

对照 headless_test.exe(Qt 端)输出,两者应一致:
Qt 期望: Frames 2457 / Accepted 2457 / Dropped 0 / BEACON 871 / SOF 347 /
         ACK 170 / COORD 1069 / NetID 0xCDA1D5
"""
import sys
import os
from collections import Counter

BPLCM_DIR = r"D:\code\HPLC_HRF\监控器\BPLCMonitor"
sys.path.insert(0, BPLCM_DIR)
sys.path.insert(0, os.path.join(BPLCM_DIR, "data"))

import comdrv
import MPDU_Class

# MSDU_Param: [rcv_num, msdu_len, reassembled] 全局状态
MSDU_Param = [0, 0, 0]
MSDU = []


def verify(path):
    driver = comdrv.comdrv(com=None, file_name=path, timeoutarg=0, logflag=0, baudrate=460800)
    frames = 0
    accepted = 0
    dropped = 0
    ftypes = Counter()
    nids = Counter()

    while True:
        buf = driver.readtill3E()
        if not buf:
            break
        frames += 1
        try:
            unesc = driver.array_post_handle(buf)
        except Exception:
            dropped += 1
            continue
        if len(unesc) < 10 + 16:
            dropped += 1
            continue

        # 与 main.py 相同:data_raw[hdr+6:] 后弹 phr/opt/ch,剩 [isRF][MPDU...]
        data = list(unesc[6:])           # phr_mcs option channel isRF MPDU...
        data.pop(0)  # phr_mcs
        data.pop(0)  # option
        data.pop(0)  # channel
        # data[0] = isRF
        mpdu = MPDU_Class.MPDU_Process(data, MSDU_Param, MSDU, "", 0)
        ftype = mpdu.FrameType.bit_field_content
        ftypes[ftype] += 1
        nids[mpdu.NetID.bit_field_content] += 1
        accepted += 1

    print("=" * 60)
    print(f"Frames:   {frames}")
    print(f"Accepted: {accepted}")
    print(f"Dropped:  {dropped}")
    names = {0: "BEACON", 1: "SOF", 2: "ACK", 3: "COORD", 5: "SEARCH", 6: "SWITCH"}
    for ft in sorted(ftypes):
        print(f"  {names.get(ft, '???'):<10}({ft}): {ftypes[ft]}")
    print(f"Unique NetIDs: {len(nids)}")
    for nid, c in nids.items():
        print(f"  0x{nid:06X}  (count={c})")
    print("=" * 60)
    return frames == 2457 and dropped == 0


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\replay_test.bin"
    ok = verify(path)
    print("PY-REF-VERDICT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
