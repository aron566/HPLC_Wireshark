"""从 replay_test.bin 手工切 SOF 帧,重组 PB,看 MMe 头 4B 真实字节。"""
import sys, os
BPLCM_DIR = r"D:\code\HPLC_HRF\监控器\BPLCMonitor"
sys.path.insert(0, BPLCM_DIR)
sys.path.insert(0, os.path.join(BPLCM_DIR, "data"))
import comdrv

BIN = r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\replay_test.bin"
driver = comdrv.comdrv(com=None, file_name=BIN, timeoutarg=0, logflag=0, baudrate=460800)

def gb(d, sb, bit, ln):
    # 与 BitDefine 相同的 LSB 读取
    out = 0
    i = 0
    while ln > 0:
        b = d[sb + i]
        if i == 0:
            take = 8 - bit
            if take > ln: take = ln
            out |= ((b >> bit) & ((1 << take) - 1)) << (8 * i)
            ln -= take
        else:
            take = min(8, ln)
            out |= (b & ((1 << take) - 1)) << (8 * i)
            ln -= take
        i += 1
    return out

def pbsize(tmi, ext):
    if tmi in (0,1): return 520
    if 2 <= tmi <= 6: return 136
    if 7 <= tmi <= 10: return 520
    if tmi in (11,12): return 264
    if tmi in (13,14): return 72
    if 1 <= ext <= 6: return 520
    if 10 <= ext <= 14: return 136
    return -1

seen = {}
frames = 0
sof_frames = 0
dbg_printed = 0
while True:
    buf = driver.readtill3E()
    if not buf: break
    try: unesc = driver.array_post_handle(buf)
    except Exception: continue
    if len(unesc) < 10+16: continue
    frames += 1
    m = unesc[9:]             # [isRF][MPDU...]
    isrf = m[0]
    d = m[1:]                 # MPDU 自 FrameType 起
    if len(d) < 16: continue
    ft = gb(d, 0, 0, 3)
    if ft != 1: continue      # SOF
    sof_frames += 1
    tmi = gb(d, 11, 4, 4); ext = gb(d, 12, 0, 4)
    pbs = pbsize(tmi, ext)
    if dbg_printed < 5:
        dbg_printed += 1
        print(f"DBG SOF#{sof_frames}: len(d)={len(d)} tmi={tmi} ext={ext} pbs={pbs} head={d[16]:02x} d16..19={bytes(d[16:20]).hex(' ')}")
    if pbs <= 0 or len(d) < 16 + pbs: continue
    blk = d[16:16+pbs]
    pb_head = blk[0]
    is_start = (pb_head & 0x40) != 0
    if not is_start: continue
    body = bytes(blk[1:pbs-3])
    if len(body) < 32: continue
    mac_flag = (body[11] >> 3) & 1
    head = 28 if mac_flag else 16
    mm = body[head:]
    if len(mm) < 8: continue
    key = mm[0]
    if key in seen: continue
    seen[key] = mm[:8].hex(' ')
    print(f"MMType_lo=0x{key:02x}  MMe头8B={seen[key]}")
    if len(seen) >= 8: break

print("distinct:", sorted(f"0x{k:02x}" for k in seen))
