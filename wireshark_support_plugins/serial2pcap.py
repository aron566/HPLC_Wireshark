"""实时串口抓包 → pcap

从 COM 口实时读取 HPLC/RF 帧(0x3C...0x3E 哨兵帧), 直接落成 pcap 供 Wireshark 实时/事后解析.

用法:
  python serial2pcap.py COM8 460800 capture.pcap
  python serial2pcap.py COM8              # 默认 460800, 文件名自动带时间戳
  python serial2pcap.py --nw COM8         # 南网 NW_2021 (USER2/3 = linktype 149/150)
  python serial2pcap.py --gw COM8         # 国网 GW_2022 (USER0/1 = linktype 147/148, 默认)

帧格式(与 bin 回放一致, 依据 serialreader.cpp):
  0x3C ... 0x3E 哨兵帧, 帧内 0x3C/0x3D/0x3E 转义为 0x3D + (0xFF^字节)
  反转义后: data_len(2B小端) + timestamp(4B小端NTB) + 媒介头4B + 纯MPDU
  实时串口帧: 无时间标签, arrival = 本地时刻

协议识别: 未指定 --gw/--nw 时, 按前若干帧的结构(信标/SOF 预计帧长)投票,
首个有判别信号的帧决定协议并写出 pcap 头(之前的帧暂存内存), 超过 50 帧无信号回退国网.

时间轴:
  首帧 = 本地接收时刻.
  后续帧 = 上一帧本地时刻 + (当前NTB - 上一帧NTB) × 40ns (mod 2^32 处理回绕).
  若当前实时本地时刻 - 上一帧本地时刻 > NTB 环绕周期(2^32×40ns≈171.8s),
  说明断流太久/NTB 已至少回绕一次, 差值不可信 → 用本地时刻重置.
"""
import sys, time, os
import serial
import struct

NTB_TICK_NS = 40               # 25MHz, 1 tick = 40ns
NTB_WRAP_TICKS = 1 << 32       # NTB 32bit 完整回绕周期 (tick)
NTB_WRAP_NS = NTB_WRAP_TICKS * NTB_TICK_NS  # 171798691840ns ≈ 171.8s

# ── 单帧协议判别 (与 bin2pcap.py 一致): 信标/SOF 的 预计帧长==实际帧长 ──
def _gw_beacon_pb(tmi):
    if tmi in (0, 1): return 520
    if 2 <= tmi <= 6: return 136
    if 7 <= tmi <= 10: return 520
    if tmi in (11, 12): return 264
    if tmi in (13, 14): return 72
    return None

def _gw_sof_pb(tmi, ext):
    s = _gw_beacon_pb(tmi)
    if s: return s
    if 1 <= ext <= 6: return 520
    if 10 <= ext <= 14: return 136
    return None

def _nw_plc_pb(tmi, ext):
    if tmi in (0, 1): return 520
    if 2 <= tmi <= 6: return 136
    if 7 <= tmi <= 10: return 520
    if tmi in (11, 12): return 264
    if 1 <= ext <= 6: return 520
    if 10 <= ext <= 14: return 136
    return None

def _nw_rf_pb(pblen):
    return (16, 40, 72, 136, 264, 520)[pblen] if 0 <= pblen <= 5 else None

def _match(mpdu, expect):
    return expect is not None and len(mpdu) == 16 + expect

def vote_protocol(mpdu):
    """返回 'gw'/'nw'/None (无判别信号)."""
    if len(mpdu) < 16:
        return None
    dt = mpdu[0] & 0x07
    if dt == 0:  # 信标: GW TMI@字节9高4bit; NW 载波 TMI@字节10高4bit / 无线 PBLen@字节11低4bit
        gw = _gw_beacon_pb(mpdu[9] >> 4)
        nw = _nw_plc_pb(mpdu[10] >> 4, 0) or _nw_rf_pb(mpdu[11] & 0x0F)
        g, w = _match(mpdu, gw), _match(mpdu, nw)
        if g and not w: return 'gw'
        if w and not g: return 'nw'
    elif dt == 1 and len(mpdu) > 12:  # SOF
        gsz = _gw_sof_pb(mpdu[11] >> 4, mpdu[12] & 0x0F)
        g = _match(mpdu, (mpdu[9] >> 4) * gsz if gsz else None)
        w1 = (mpdu[7] & 0x0F) == 1 and _match(mpdu, _nw_plc_pb(mpdu[7] >> 4, mpdu[12] & 0x0F))
        w2 = _match(mpdu, _nw_rf_pb(mpdu[6] >> 4))
        if g and not (w1 or w2): return 'gw'
        if (w1 or w2) and not g: return 'nw'
    return None

def build_pcap_header(linktype=147):
    return struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, linktype)

def main():
    pos_args, proto = [], None
    for a in sys.argv[1:]:
        if a == '--nw': proto = 'nw'
        elif a == '--gw': proto = 'gw'
        else: pos_args.append(a)
    port = pos_args[0] if len(pos_args) > 0 else 'COM8'
    baud = int(pos_args[1]) if len(pos_args) > 1 else 460800
    out_path = pos_args[2] if len(pos_args) > 2 else \
        f"capture_{time.strftime('%Y%m%d_%H%M%S')}.pcap"

    ser = serial.Serial(port, baud, timeout=0.5)
    print(f"打开 {port} @ {baud}, 输出 {out_path}")
    print("Ctrl+C 停止")
    if proto is None:
        print("协议自动识别中 (可用 --gw/--nw 强制指定)...")

    out = open(out_path, 'wb')
    header_written = False
    pending = []   # 协议未定时暂存的帧记录 (abs_s, abs_us, mpdu)
    proto_state = {'proto': proto}  # 识别后固定; None = 仍未定

    buf = bytearray()
    frames = 0
    last_ntb = None
    last_abs_ns = None

    try:
        while True:
            data = ser.read(4096)
            if not data:
                continue
            buf.extend(data)

            # 切帧: 0x3C ... 0x3E
            while True:
                idx = buf.find(0x3C)
                if idx < 0:
                    # 无帧起始, 保留末尾可能的半帧(最多留 1 字节 0x3D 转义)
                    if len(buf) > 2:
                        buf = buf[-2:]
                    break
                if idx > 0:
                    del buf[:idx]  # 丢弃帧前噪声
                if len(buf) < 2:
                    break
                # 找 0x3E (注意转义: 0x3D 后的字节跳过)
                end = -1
                i = 1
                while i < len(buf):
                    if buf[i] == 0x3C:
                        del buf[:i]  # 又找到帧头，丢弃帧前噪声
                        break                    
                    if buf[i] == 0x3E:
                        end = i
                        break
                    if buf[i] == 0x3D and i + 1 < len(buf):
                        i += 2  # 跳过转义字节
                    else:
                        i += 1
                if end < 0:
                    break  # 帧未完整, 等更多数据

                # 反转义
                esc = bytes(buf[1:end])
                unesc = bytearray()
                j = 0
                while j < len(esc):
                    b = esc[j]
                    if b == 0x3D and j + 1 < len(esc):
                        j += 1
                        unesc.append(0xFF ^ esc[j])
                    else:
                        unesc.append(b)
                    j += 1

                del buf[:end + 1]

                # 帧结构: data_len(2B) + ts(4B) + 媒介头4B(phr_mcs/option/channel/isRF) + MPDU
                if len(unesc) < 10:
                    continue
                mpdu = bytes(unesc[10:])   # 载荷 = 纯 MPDU
                if not mpdu:
                    continue

                # 首帧确定 linktype (媒介 + 协议) 后写 pcap 头
                # GW_2022: 载波 147 (USER0) / 无线 148 (USER1); NW_2021: 载波 149 (USER2) / 无线 150 (USER3)
                if not header_written:
                    is_rf = unesc[9] != 0
                    if proto_state['proto'] is None:
                        v = vote_protocol(mpdu)
                        if v is None and len(pending) < 50:
                            pending.append((None, None, bytes(mpdu)))  # 暂存, 等协议确定
                            continue
                        proto_state['proto'] = v or 'gw'
                        print(f"协议识别: {proto_state['proto'].upper()}")
                    if proto_state['proto'] == 'nw':
                        linktype = 150 if is_rf else 149
                    else:
                        linktype = 148 if is_rf else 147
                    out.write(build_pcap_header(linktype))
                    header_written = True
                    for _, _, pm in pending:  # 刷新暂存帧
                        out.write(struct.pack('<IIII', 0, 0, len(pm), len(pm)))
                        out.write(pm)
                    pending.clear()

                # 时间轴: 首帧=本地; 后续=上帧+NTB差×40ns; 断流超环绕周期则本地重置
                cur_ntb = struct.unpack('<I', unesc[2:6])[0]
                now_ns = int(time.time() * 1e9)

                if last_abs_ns is None:
                    abs_ns = now_ns  # 首帧: 本地时间
                else:
                    delta_ticks = (cur_ntb - last_ntb) % NTB_WRAP_TICKS  # mod 2^32 处理回绕
                    cand_ns = last_abs_ns + delta_ticks * NTB_TICK_NS
                    if now_ns - last_abs_ns > NTB_WRAP_NS:
                        abs_ns = now_ns  # 断流超 171.8s, NTB 差值不可信 → 重置
                    else:
                        abs_ns = cand_ns
                last_ntb = cur_ntb
                last_abs_ns = abs_ns

                abs_s = abs_ns // 1_000_000_000
                abs_us = (abs_ns % 1_000_000_000) // 1000
                out.write(struct.pack('<IIII', abs_s, abs_us,
                                      len(mpdu), len(mpdu)))
                out.write(mpdu)
                frames += 1
                if frames % 100 == 0:
                    print(f"  已捕获 {frames} 帧", end='\r')
    except KeyboardInterrupt:
        pass
    finally:
        out.close()
        ser.close()
        print(f"\n共捕获 {frames} 帧 → {out_path}")

if __name__ == '__main__':
    main()
