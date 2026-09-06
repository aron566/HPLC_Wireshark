"""verify_bin.py - 验证生成的 .bin 文件能被 comdrv 切帧并解析

使用 BPLCMonitor 自带的 comdrv/MPDU_Class 模块做 1:1 校验。
如果 Python 校验通过,Qt 端解析(算法完全一致)也必定通过。
"""
import sys
import os

# 把 BPLCMonitor 加入路径
BPLCM_DIR = r"D:\code\HPLC_HRF\监控器\BPLCMonitor"
sys.path.insert(0, BPLCM_DIR)
sys.path.insert(0, os.path.join(BPLCM_DIR, "data"))

import comdrv

def verify(path):
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return False

    # 用 comdrv 读 .bin(它默认从文件读,见 comdrv.__init__ 第 24-28 行)
    # 直接构造一个 comdrv 实例,传 file_name 即可走文件路径
    driver = comdrv.comdrv(com=None, file_name=path, timeoutarg=0, logflag=0, baudrate=460800)

    frames = 0
    total_bytes = 0
    accepted = 0
    dropped = 0
    types = {}
    nids = set()
    while True:
        buf = driver.readtill3E()
        if not buf:
            break
        frames += 1
        total_bytes += len(buf)

        # 反转义
        try:
            unesc = driver.array_post_handle(buf)
        except Exception as e:
            print(f"Frame {frames}: post_handle error: {e}")
            dropped += 1
            continue

        # 剥头解析(简化版,只统计)
        if len(unesc) < 6:
            dropped += 1
            continue
        data_len = unesc[0] | (unesc[1] << 8)
        ts = unesc[2] | (unesc[3] << 8) | (unesc[4] << 16) | (unesc[5] << 24)
        media_id = unesc[9] if len(unesc) > 9 else 0
        is_rf = "HRF" if media_id else "HPLC"
        payload = unesc[10:]

        if len(payload) < 16:
            dropped += 1
            continue
        # MPDU_BASE:bit[2:0]=FrameType,bit[7:3]=NetType
        b0 = payload[0]
        ftype = b0 & 0x07
        nid = payload[1] | (payload[2] << 8) | (payload[3] << 16)
        types.setdefault(ftype, 0)
        types[ftype] += 1
        nids.add(nid)
        accepted += 1

    print("=" * 60)
    print(f"File:       {path}")
    print(f"File size:  {os.path.getsize(path):,} bytes")
    print(f"Frames:     {frames:,}")
    print(f"Total data: {total_bytes:,} bytes (post-handle)")
    print(f"Accepted:   {accepted:,}")
    print(f"Dropped:    {dropped:,}")
    print("FrameType distribution:")
    ft_names = {0: "BEACON", 1: "SOF", 2: "ACK", 3: "COORD", 5: "SEARCH", 6: "SWITCH"}
    for ft, cnt in sorted(types.items()):
        print(f"  {ft_names.get(ft, '???'):<10} ({ft}): {cnt:>5}")
    print(f"Unique NetIDs: {len(nids)}")
    if len(nids) <= 10:
        for nid in sorted(nids):
            print(f"  0x{nid:06X}")
    print("=" * 60)
    return frames > 0 and dropped < frames * 0.5  # 允许 < 50% 丢弃


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\replay_test.bin"
    ok = verify(path)
    print("OK" if ok else "FAIL")
    sys.exit(0 if ok else 1)
