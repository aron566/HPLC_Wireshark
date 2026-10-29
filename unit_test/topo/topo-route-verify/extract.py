import struct, json, sys
from datetime import datetime, timezone, timedelta

data = open('/home/hatch/workspace/user/files/TOPO_SPLC_20261027_095658.bin','rb').read()

def bcd2(x): return (x >> 4) * 10 + (x & 0x0F)
def valid_bcd8(b):
    if len(b) < 8: return False
    for x in b[:8]:
        if (x >> 4) > 9 or (x & 0x0F) > 9: return False
    try:
        yy, mo, dd, hh, mi, ss = (bcd2(b[0]), bcd2(b[1]), bcd2(b[2]), bcd2(b[3]), bcd2(b[4]), bcd2(b[5]))
        ms = bcd2(b[6]) * 100 + bcd2(b[7])
        dt = datetime(2000+yy, mo, dd, hh, mi, ss) + timedelta(milliseconds=ms)
    except ValueError:
        return False
    return dt

def unescape(frame):
    out = bytearray(); i = 0
    while i < len(frame):
        b = frame[i]
        if b == 0x3D and i + 1 < len(frame):
            i += 1; out.append(0xFF - frame[i])
        else:
            out.append(b)
        i += 1
    return bytes(out)

frames = []  # (unesc, base_ms, seg)
pos = 0; base_ms = None; seg = 0
# file head BCD tag
if valid_bcd8(data[:8]):
    dt = valid_bcd8(data[:8]); base_ms = int(dt.replace(tzinfo=timezone.utc).timestamp()*1000)
    # note: reader uses local time (QDateTime without tz = local); CST=UTC+8, keep both
    base_ms_cst = base_ms - 8*3600*1000
    pos = 8
    print("head BCD tag:", dt, "as CST:", dt - timedelta(hours=8))
else:
    print("NO head BCD tag"); base_ms_cst = None

n = len(data)
while pos < n:
    # segment BCD tag between frames?
    if pos + 9 <= n and valid_bcd8(data[pos:pos+8]) and data[pos+8] == 0x3C:
        dt = valid_bcd8(data[pos:pos+8]); base_ms_cst = int((dt - timedelta(hours=8)).replace(tzinfo=timezone.utc).timestamp()*1000)
        seg += 1; pos += 8
    idx = data.find(b'\x3c', pos)
    if idx < 0: break
    # fake-3C rule
    while True:
        n3c = data.find(b'\x3c', idx+1); n3e = data.find(b'\x3e', idx+1)
        if n3c >= 0 and n3e >= 0 and n3c < n3e:
            idx = n3c; continue
        break
    if n3e < 0: break
    raw = data[idx+1:n3e]
    unesc = unescape(raw)
    if valid_bcd8(unesc[:8]):  # old-format per-frame BCD tag
        unesc = unesc[8:]
    frames.append((unesc, base_ms_cst, seg))
    pos = n3e + 1

print("total frames:", len(frames))
# len-field semantics on first frames
for i in range(3):
    u = frames[i][0]
    ln = struct.unpack_from('<H', u, 0)[0]
    print(f"frame {i+1}: unesc_len={len(u)} len_field={ln} mpdu_len={len(u)-10} len==mpdu? {ln==len(u)-10} media={u[6:10].hex(' ')} ntb={struct.unpack_from('<I', u, 2)[0]}")
# NTB time accumulation (code formula: ft += (dn*40//1000)//1000 ms; reset on gap)
KMAX = 25000000*60
ft = None; last_ntb = 0; first = True; times = {}
for i, (u, base, sg) in enumerate(frames):
    ntb = struct.unpack_from('<I', u, 2)[0]
    if first or base != times.get('base'):
        ft = base; first = False; times['base'] = base
    else:
        dn = (ntb - last_ntb) & 0xFFFFFFFF
        if dn >= 0x80000000: dn -= 0x100000000
        if 0 < dn <= KMAX:
            ft = ft + ((dn*40)//1000)//1000
        else:
            ft = None  # reset point; reader would use wall clock
    last_ntb = ntb
    if i+1 in (1, 45558, 45572, 45595, 45605, 45637, len(frames)):
        times[i+1] = ft
for k in (1, 45558, 45572, 45595, 45605, 45637, len(frames)):
    v = times.get(k)
    print(f"frame {k}: ft_ms={v} -> {datetime.fromtimestamp(v/1000).strftime('%H:%M:%S.') + str(v%1000).zfill(3) if v else 'RESET/None'}")
# write container [u32 len][MPDU]
with open('/home/hatch/workspace/topo-route-verify/mpdus.bin','wb') as f:
    for u, b, s in frames:
        mpdu = u[10:]
        f.write(struct.pack('<I', len(mpdu))); f.write(mpdu)
# save per-frame meta for cross-check
with open('/home/hatch/workspace/topo-route-verify/frames_meta.json','w') as f:
    json.dump([{"idx": i+1, "len_field": struct.unpack_from('<H', u, 0)[0],
                "ntb": struct.unpack_from('<I', u, 2)[0], "media": u[6:10].hex(),
                "mpdu_len": len(u)-10} for i, (u, b, s) in enumerate(frames)], f)
print("wrote mpdus.bin + frames_meta.json")
