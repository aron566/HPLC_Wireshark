#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""BPLC 串口抓包 extcap: 在 Wireshark 捕获接口列表提供 NW_2021_Capture / GW_2022_Capture.

选好串口号/波特率后点"开始", Wireshark 实时接收并自动用对应 Lua 解析器解析:
  - NW_2021_Capture → packet-nw_2021.lua  (linktype 载波 149 USER2 / 无线 150 USER3)
  - GW_2022_Capture → packet-gw_2022.lua  (linktype 载波 147 USER0 / 无线 148 USER1)

由 Wireshark 经 BPLC_Serial_Capture.bat 自动调用(也可手动测试):
  bplc_serial_extcap.py --extcap-interfaces
  bplc_serial_extcap.py --extcap-interface=nw2021 --extcap-dlts
  bplc_serial_extcap.py --extcap-interface=nw2021 --extcap-config
  bplc_serial_extcap.py --extcap-interface=nw2021 --capture --fifo=xxx --com COM8 --baud 460800 --media auto

帧格式(与 serial2pcap.py / serialreader.cpp 一致):
  0x3C ... 0x3E 哨兵帧, 帧内 0x3C/0x3D/0x3E 转义为 0x3D + (0xFF^字节)
  反转义后: data_len(2B小端) + timestamp(4B小端NTB) + 媒介头4B + 纯MPDU
时间轴: 首帧=本地接收时刻, 后续=上一帧+(NTB差)×40ns(mod 2^32 处理回绕), 断流超环绕周期重置.
"""
import argparse
import os
import re
import struct
import sys
import time

LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bplc_extcap_debug.log')


def log(msg):
    """运行日志落文件: Wireshark 把 extcap 的 stderr 输出当错误弹窗, 常规信息不能走 stderr"""
    try:
        with open(LOG_PATH, 'a', encoding='utf-8') as f:
            f.write(time.strftime('[%Y-%m-%d %H:%M:%S] ') + msg + '\n')
    except Exception:
        pass


TS_VERSION = '1.0'
NTB_TICK_NS = 40               # 25MHz, 1 tick = 40ns
NTB_WRAP_TICKS = 1 << 32
NTB_WRAP_NS = NTB_WRAP_TICKS * NTB_TICK_NS  # ≈171.8s

# 接口 → linktype 映射 (与 packet-gw_2022.lua / packet-nw_2021.lua 的注册一致)
INTERFACES = {
    'nw2021': {
        'display': 'NW_2021_Capture (南网串口, 载波/无线混合)',
        'dlt': 152,                             # USER5: 媒介头+MPDU, 逐帧 isRF 自选载波/无线
        'dlt_name': 'USER5',
        'dlt_display': 'CSG NW_2021 dual-mode serial, mixed PLC/RF (media header + MPDU)',
    },
    'gw2022': {
        'display': 'GW_2022_Capture (国网串口, 载波/无线混合)',
        'dlt': 151,                             # USER4: 媒介头+MPDU
        'dlt_name': 'USER4',
        'dlt_display': 'SGCC GW_2022 dual-mode serial, mixed PLC/RF (media header + MPDU)',
    },
}

BAUDS = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]


def out(msg):
    sys.stdout.buffer.write((msg + '\n').encode('utf-8'))
    sys.stdout.buffer.flush()


# ── extcap 握手 ─────────────────────────────────────────
def extcap_interfaces():
    out('extcap {version=%s}{help=https://wireshark.org/docs/wsug_html_chunked/ChCaptureInterfacesSection.html}' % TS_VERSION)
    for key in ('nw2021', 'gw2022'):
        out('interface {value=%s}{display=%s}' % (key, INTERFACES[key]['display']))


def extcap_dlts(interface):
    cfg = INTERFACES[interface]
    out('dlt {number=%d}{name=%s}{display=%s}' % (cfg['dlt'], cfg['dlt_name'], cfg['dlt_display']))


def enum_com_ports():
    """枚举 COM 口: comports() + SERIALCOMM 注册表补充.
    com0com 等虚拟串口挂在自定义设备类(CNCPorts)下, comports() 看不到它们;
    SERIALCOMM 注册表与 .NET GetPortNames 同源, 能看到全部(含虚拟口)."""
    ports = []
    try:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports()]
    except Exception:
        pass
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM") as key:
            i = 0
            while True:
                try:
                    ports.append(winreg.EnumValue(key, i)[1])
                    i += 1
                except OSError:
                    break
    except (ImportError, OSError):
        pass
    def port_sort_key(n):
        m = re.fullmatch(r'COM(\d+)', n)
        return (0, int(m.group(1)), n) if m else (1, 0, n)

    # 去重 + 数字序排序 (COM2 排在 COM10 前)
    return sorted(set(ports), key=port_sort_key)


def extcap_config(interface):
    ports = enum_com_ports()
    out('arg {number=0}{call=--com}{display=串口(COM口)}{type=selector}'
        '{tooltip=设备管理器中查看串口号(含 com0com 虚拟串口)}')
    if ports:
        for i, p in enumerate(ports):
            out('value {arg=0}{value=%s}{display=%s}%s' % (p, p, '{default=true}' if i == 0 else ''))
    else:
        out('value {arg=0}{value=COM8}{display=COM8}{default=true}')
    out('arg {number=1}{call=--baud}{display=波特率}{type=selector}'
        '{tooltip=监控器串口波特率, 默认 460800}')
    for b in BAUDS:
        out('value {arg=1}{value=%d}{display=%d}%s' % (b, b, '{default=true}' if b == 460800 else ''))
    # (媒介无需选择: 记录含媒介头, 解析器逐帧按 isRF 自选载波/无线, 支持混合)


# ── 串口帧流 → pcap ─────────────────────────────────────
def stderr_msg(msg):
    """extcap 错误信息: 显式 UTF-8 写 stderr (Wireshark 按 UTF-8 解码, 避免中文乱码)"""
    sys.stderr.buffer.write((msg + '\n').encode('utf-8'))
    sys.stderr.buffer.flush()


def open_serial(com, baud):
    import serial
    try:
        return serial.Serial(com, baud, timeout=0.5)
    except Exception as e:
        stderr_msg(
            '[extcap] 无法打开串口 %s @ %d:\n'
            '[extcap]   %s\n'
            '[extcap] 常见原因:\n'
            '[extcap]   1. 端口被其他程序占用(监控器/串口助手/另一次抓包正在使用)\n'
            '[extcap]   2. com0com 虚拟串口选了另一端(成对端口如 COM90/COM91, 抓包端应选\n'
            '[extcap]      与数据发送端配对的另一个口)\n'
            '[extcap]   3. 波特率不匹配或设备未上电\n'
            '[extcap] Cannot open %s @ %d: %s' % (com, baud, e, com, baud, e))
        sys.exit(1)


def pcap_header(linktype):
    return struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, linktype)


def parent_alive():
    """父进程(bat 的 cmd.exe)是否存活. Wireshark 停止抓包时杀掉 bat, 但孙进程 python
    会孤儿化继续占用串口 —— 周期检查父进程句柄, 已终止则返回 False 触发退出."""
    try:
        import ctypes
        SYNCHRONIZE = 0x00100000
        WAIT_OBJECT_0 = 0
        k32 = ctypes.windll.kernel32
        h = k32.OpenProcess(SYNCHRONIZE, False, os.getppid())
        if not h:
            return False
        try:
            return k32.WaitForSingleObject(h, 0) != WAIT_OBJECT_0
        finally:
            k32.CloseHandle(h)
    except Exception:
        return True   # 无法判断时保守视为存活


def run_capture(interface, fifo, com, baud, media='auto'):
    cfg = INTERFACES[interface]
    ser = open_serial(com, baud)
    log('%s: %s @ %d (mixed PLC/RF)' % (interface, com, baud))
    out_fh = open(fifo, 'wb', buffering=0)
    out_fh.write(pcap_header(cfg['dlt']))   # linktype 固定(媒介头格式), 立即写头

    buf = bytearray()
    header_written = True
    linktype = cfg['dlt']
    frames = 0
    last_ntb = None
    last_abs_ns = None

    def record_bytes(abs_ns, mpdu):
        s, us = abs_ns // 1_000_000_000, (abs_ns % 1_000_000_000) // 1000
        return struct.pack('<IIII', s, us, len(mpdu), len(mpdu)) + bytes(mpdu)

    try:
        while True:
            data = ser.read(4096)
            if not data:
                if not parent_alive():
                    log('parent (bat/cmd.exe) exited -> stop')
                    break
                continue
            buf.extend(data)
            # 切帧: 0x3C ... 0x3E (帧内 0x3D 转义跳过)
            while True:
                idx = buf.find(0x3C)
                if idx < 0:
                    if len(buf) > 2:
                        buf = buf[-2:]
                    break
                if idx > 0:
                    del buf[:idx]
                if len(buf) < 2:
                    break
                end, i = -1, 1
                while i < len(buf):
                    if buf[i] == 0x3C:
                        del buf[:i]
                        break
                    if buf[i] == 0x3E:
                        end = i
                        break
                    if buf[i] == 0x3D and i + 1 < len(buf):
                        i += 2
                    else:
                        i += 1
                if end < 0:
                    break  # 帧未完整
                # 反转义
                esc, j = bytes(buf[1:end]), 0
                unesc = bytearray()
                while j < len(esc):
                    b = esc[j]
                    if b == 0x3D and j + 1 < len(esc):
                        j += 1
                        unesc.append(0xFF ^ esc[j])
                    else:
                        unesc.append(b)
                    j += 1
                del buf[:end + 1]
                # 帧结构: data_len(2B) + ts(4B) + 媒介头4B + MPDU
                if len(unesc) < 10:
                    continue
                # 记录 = 媒介头4B + MPDU (解析器逐帧读 isRF, 载波/无线混合均可解析)
                mpdu = bytes(unesc[6:])
                if len(mpdu) <= 4:
                    continue
                # 时间轴: 首帧=本地时刻; 后续=上帧+NTB差×40ns; 断流超环绕周期重置
                cur_ntb = struct.unpack('<I', bytes(unesc[2:6]))[0]
                now_ns = int(time.time() * 1e9)
                if last_abs_ns is None:
                    abs_ns = now_ns
                else:
                    delta = (cur_ntb - last_ntb) % NTB_WRAP_TICKS
                    abs_ns = now_ns if now_ns - last_abs_ns > NTB_WRAP_NS else last_abs_ns + delta * NTB_TICK_NS
                last_ntb, last_abs_ns = cur_ntb, abs_ns
                rec = record_bytes(abs_ns, mpdu)
                out_fh.write(rec)
                frames += 1
    except (KeyboardInterrupt, BrokenPipeError, ValueError, OSError):
        pass
    finally:
        try:
            out_fh.close()
        except Exception:
            pass
        try:
            ser.close()
        except Exception:
            pass
        log('stopped, %d frames' % frames)


def main():
    ap = argparse.ArgumentParser(description='BPLC serial extcap')
    ap.add_argument('--extcap-interfaces', action='store_true')
    ap.add_argument('--extcap-interface')
    ap.add_argument('--extcap-dlts', action='store_true')
    ap.add_argument('--extcap-config', action='store_true')
    ap.add_argument('--extcap-capture-filter')       # 兼容 Wireshark 传入, 忽略
    ap.add_argument('--capture', action='store_true')
    ap.add_argument('--fifo')
    ap.add_argument('--com', default='COM8')
    ap.add_argument('--baud', type=int, default=460800)
    ap.add_argument('--media', default='auto')
    args, _ = ap.parse_known_args()

    if args.extcap_interfaces:
        extcap_interfaces()
        return 0
    if args.extcap_interface not in INTERFACES:
        sys.stderr.write('[extcap] unknown/missing interface\n')
        return 1
    if args.extcap_dlts:
        extcap_dlts(args.extcap_interface)
        return 0
    if args.extcap_config:
        extcap_config(args.extcap_interface)
        return 0
    if args.capture and args.fifo:
        run_capture(args.extcap_interface, args.fifo, args.com, args.baud, args.media)
        return 0
    sys.stderr.write('[extcap] no action\n')
    return 1


if __name__ == '__main__':
    sys.exit(main())
