"""真实校验 replay_test.bin 的 FCH CRC24 通过率(用 Python 原版 cal_crc24)。

verify_bin.py 之前不校验 CRC,只做切帧+统计,所以"2457 帧通过"不说明 CRC 正确。
本脚本:comdrv 切帧 -> 反转义 -> 剥头 -> 对 MPDU_BASE 前 16B 做 CRC24 校验。
若这里也大量失败 => bin/log 转换或 log 本身有问题;
若这里全过而 Qt 端报错 => Qt 端剥头/实现有 bug。
"""
import sys
import os

BPLCM_DIR = r"D:\code\HPLC_HRF\监控器\BPLCMonitor"
sys.path.insert(0, BPLCM_DIR)
sys.path.insert(0, os.path.join(BPLCM_DIR, "data"))

import comdrv
from MPDU_Class import cal_crc24


def crc_check(path):
    driver = comdrv.comdrv(com=None, file_name=path, timeoutarg=0, logflag=0, baudrate=460800)

    frames = 0
    crc_ok = 0
    crc_bad = 0
    short = 0
    example_bad = []

    while True:
        buf = driver.readtill3E()
        if not buf:
            break
        frames += 1
        try:
            unesc = driver.array_post_handle(buf)
        except Exception:
            crc_bad += 1
            continue

        # 帧内布局: Len(2) TS(4) media(4: phr_mcs/option/channel/isRF) payload
        if len(unesc) < 10 + 16:
            short += 1
            continue
        payload = unesc[10:]

        # Python MPDU_BASE 期望 payload 从 MPDU_BASE 起始(data[0]=FrameType 字节)
        base = bytes(payload[:16])
        calc = cal_crc24(base, 16)
        rx = base[13] | (base[14] << 8) | (base[15] << 16)
        if calc == rx:
            crc_ok += 1
        else:
            crc_bad += 1
            if len(example_bad) < 5:
                example_bad.append(
                    (frames, base.hex(), hex(calc), hex(rx)))

    print("=" * 66)
    print(f"File:     {path}")
    print(f"Frames:   {frames}")
    print(f"CRC24 OK: {crc_ok}")
    print(f"CRC24 BAD:{crc_bad}")
    print(f"Short(<26B): {short}")
    if example_bad:
        print("Bad examples (frame, base16, calc, rx):")
        for f, h, c, r in example_bad:
            print(f"  #{f}: {h} calc={c} rx={r}")
    print("=" * 66)
    return crc_ok > 0 and crc_bad == 0


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\replay_test.bin"
    ok = crc_check(path)
    print("BIN-CRC-VERDICT:", "PASS(all frames CRC ok)" if ok else "FAIL")
    sys.exit(0 if ok else 1)
