"""实时串口抓包 → pcap

从 COM 口实时读取 HPLC/RF 帧(0x3C...0x3E 哨兵帧), 直接落成 pcap 供 Wireshark 实时/事后解析.

用法:
  python serial2pcap.py COM8 460800 capture.pcap
  python serial2pcap.py COM8              # 默认 460800, 文件名自动带时间戳

帧格式(与 bin 回放一致, 依据 serialreader.cpp):
  0x3C ... 0x3E 哨兵帧, 帧内 0x3C/0x3D/0x3E 转义为 0x3D + (0xFF^字节)
  反转义后: data_len(2B小端) + timestamp(4B小端NTB) + 媒介头4B + 纯MPDU
  实时串口帧: 无时间标签, arrival = 本地时刻

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

def build_pcap_header(linktype=147):
    return struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, linktype)

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM8'
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 460800
    out_path = sys.argv[3] if len(sys.argv) > 3 else \
        f"capture_{time.strftime('%Y%m%d_%H%M%S')}.pcap"

    ser = serial.Serial(port, baud, timeout=0.5)
    print(f"打开 {port} @ {baud}, 输出 {out_path}")
    print("Ctrl+C 停止")

    out = open(out_path, 'wb')
    header_written = False
    linktype = 147  # 默认 HPLC, 首帧 isRF 后确定

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

                # 首帧确定 linktype (isRF: 0=HPLC→147, 非0=RF→148), 之后写 pcap 头
                if not header_written:
                    linktype = 148 if unesc[9] != 0 else 147
                    out.write(build_pcap_header(linktype))
                    header_written = True

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
