#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""原始报文 → 0x3C/0x3E 封装 txt 转换工具（GUI + 自检 CLI）。

封装格式与程序内 `src/io/playbackwriter.h` 的 RawHex 导出**逐字节同构**：

    data  = [dlen 2B LE][ts 4B LE][phr_mcs][option][channel][isRF][MPDU]
    frame = 0x3C + escape(data) + 0x3E
    escape: 0x3C/0x3D/0x3E -> 0x3D + (0xFF - b)   即 0xC3 / 0xC2 / 0xC1
    行文本 = 每字节 "0x%02x"，单空格分隔，每行一帧

用法：
    python frame_wrap_gui.py                启动 GUI
    python frame_wrap_gui.py --selftest     仅跑内置自检（不需要图形环境）
"""
import sys

# ---------------- 核心逻辑（与 GUI 解耦，便于自检/复用） ----------------

START = 0x3C
ESC = 0x3D
END = 0x3E
SPECIAL = (START, ESC, END)
# 回放端最小 data 长度(serialreader.cpp: data.size() < 26 拒绝)
kMinPlaybackDataBytes = 26


def parse_hex(text):
    """把用户粘贴的十六进制文本解析为 bytes。

    容忍：空格/换行/逗号/分号分隔、0x/0X 前缀、\\x 前缀、大写、连写（3c9c0024）。
    遇到非十六进制字符直接忽略（常见于从日志里连注释一起复制）。
    """
    s = text.strip()
    if not s:
        return b''
    for pref in ("0x", "0X", "\\x", "\\X"):
        s = s.replace(pref, " ")
    out = bytearray()
    token = []

    def flush():
        if not token:
            return
        hexstr = "".join(token)
        token.clear()
        # 每 2 个 hex 字符 = 1 字节；奇数位左补 0（如 "9" -> 0x09）
        if len(hexstr) % 2:
            hexstr = "0" + hexstr
        for i in range(0, len(hexstr), 2):
            out.append(int(hexstr[i:i + 2], 16) & 0xFF)

    for ch in s:
        if ch in "0123456789abcdefABCDEF":
            token.append(ch)
        else:
            flush()
    flush()
    return bytes(out)


def escape_frame_data(data):
    """转义：0x3C/0x3D/0x3E -> 0x3D + (0xFF - b)。与 C++ escape_frame_data 一致。"""
    out = bytearray()
    for b in data:
        if b in SPECIAL:
            out.append(ESC)
            out.append(0xFF - b)
        else:
            out.append(b)
    return bytes(out)


def unescape_frame_data(esc):
    """宽松反转义（方案 A）：非法/悬空 0x3D 当普通数据。仅供自检使用。"""
    out = bytearray()
    i = 0
    n = len(esc)
    while i < n:
        b = esc[i]
        if b != ESC:
            out.append(b)
            i += 1
            continue
        if i + 1 >= n:
            out.append(ESC)
            i += 1
            continue
        nxt = esc[i + 1]
        if nxt in (0xC1, 0xC2, 0xC3):
            out.append(0xFF - nxt)
            i += 2
        else:
            out.append(ESC)
            i += 1
    return bytes(out)


def build_data(mpdu, ts=0, phr_mcs=0, option=0, channel=0, is_rf=0, already_full=False):
    """构造 data 段。

    already_full=False（默认）：mpdu 即 MPDU，自动加 [dlen][ts][phr][option][channel][isRF]，
                                dlen = len(MPDU) + 4（与 C++ 导出一致）。
    already_full=True：入参已是完整 data（含 6B 头），只把 ts 4B 置 0 并重算 dlen，
                       头字节保持入参不变。
    """
    if already_full:
        if len(mpdu) < 6:
            raise ValueError("已含完整 data 模式要求至少 6 字节（2B dlen + 4B ts）")
        body = bytearray(mpdu)
        # 重算 dlen：dlen = 剩余长度 - 4（去掉 ts 后与 MPDU 等长时的等价关系）
        dlen = (len(body) - 2) - 4
        if dlen < 0:
            raise ValueError("data 过短，无法计算 dlen")
        body[0] = dlen & 0xFF
        body[1] = (dlen >> 8) & 0xFF
        # ts 4B 置 0
        for k in range(2, 6):
            body[k] = 0
        return bytes(body)

    mpdu = bytes(mpdu)
    dlen = len(mpdu) + 4
    if dlen > 0xFFFF:
        raise ValueError("MPDU 过长：dlen 超出 2 字节（MPDU > 65531 字节）")
    data = bytearray()
    data.append(dlen & 0xFF)
    data.append((dlen >> 8) & 0xFF)
    data.extend(b'\x00\x00\x00\x00')            # ts = 0（时间值为 0）
    data.append(phr_mcs & 0xFF)
    data.append(option & 0xFF)
    data.append(channel & 0xFF)
    data.append(1 if is_rf else 0)
    data.extend(mpdu)
    return bytes(data)


def wrap_frame(data):
    """0x3C + escape(data) + 0x3E。"""
    return bytes([START]) + escape_frame_data(data) + bytes([END])


def format_line(frame):
    """与 C++ build_raw_hex_text 相同的行格式：0xNN 小写、单空格分隔。"""
    return " ".join("0x%02x" % b for b in frame)


def convert(text, is_rf=0, phr_mcs=0, option=0, channel=0, already_full=False):
    """把多行原始报文转换成输出文本行。返回 (lines, stats)。

    输入约定：每个非空行 = 一帧原始报文。以 # 开头的行视为注释。
    """
    lines = []
    frames = 0
    bytes_out = 0
    errors = []
    warns = []
    for idx, raw in enumerate(text.splitlines(), 1):
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        try:
            payload = parse_hex(s)
            if not payload:
                continue
            data = build_data(payload, ts=0, phr_mcs=phr_mcs, option=option,
                              channel=channel, is_rf=is_rf,
                              already_full=already_full)
            fr = wrap_frame(data)
            lines.append(format_line(fr))
            frames += 1
            bytes_out += len(fr)
            # 回放端(serialreader::process_raw_hex_line)要求 data.size() >= 26
            # 即 10B 采集头 + 16B FCH；不足会被静默丢弃
            if len(data) < kMinPlaybackDataBytes:
                warns.append("第 %d 行: data 仅 %d 字节(< %d)，回放端会丢弃"
                             % (idx, len(data), kMinPlaybackDataBytes))
        except ValueError as e:
            errors.append("第 %d 行: %s" % (idx, e))
    return lines, {"frames": frames, "bytes": bytes_out, "errors": errors, "warns": warns}


# ---------------- 自检 ----------------

def selftest():
    """无 GUI 自检：转义/反转义互逆、dlen/ts/头布局、行格式。"""
    fails = []

    def ck(cond, msg):
        print(("  ok  " if cond else "  FAIL ") + msg)
        if not cond:
            fails.append(msg)

    # 1) 转义互逆（含 3C/3D/3E 全部特殊字节）
    raw = bytes([0x00, 0x3C, 0x3D, 0x3E, 0xFF, 0x3D, 0x3C])
    esc = escape_frame_data(raw)
    ck(esc[:1] == b'\x00', "普通字节不转义")
    ck(esc[1:3] == bytes([0x3D, 0xFF - 0x3C]), "0x3C -> 3D C3")
    ck(esc[3:5] == bytes([0x3D, 0xFF - 0x3D]), "0x3D -> 3D C2")
    ck(esc[5:7] == bytes([0x3D, 0xFF - 0x3E]), "0x3E -> 3D C1")
    ck(unescape_frame_data(esc) == raw, "转义/反转义互逆")

    # 2) data 布局：dlen/ts/phr/option/channel/isRF
    mpdu = bytes([0xAA, 0xBB, 0xCC])
    data = build_data(mpdu, ts=0, phr_mcs=0x12, option=0x03, channel=0x05, is_rf=1)
    ck(len(data) == len(mpdu) + 10, "data 长度 = MPDU + 10(2 dlen + 4 ts + 4 头)")
    dlen = data[0] | (data[1] << 8)
    ck(dlen == len(mpdu) + 4, "dlen = MPDU + 4（与 C++ 导出一致）")
    ck(data[2:6] == b'\x00\x00\x00\x00', "ts 4B 全 0")
    ck(list(data[6:10]) == [0x12, 0x03, 0x05, 0x01], "phr/option/channel/isRF 顺序正确")
    ck(data[10:] == mpdu, "MPDU 原样附加")

    # 3) isRF 选择生效
    f0 = wrap_frame(build_data(mpdu, is_rf=0))
    f1 = wrap_frame(build_data(mpdu, is_rf=1))
    ck(f0 != f1, "isRF 0/1 生成不同帧")
    ck(f1[1 + 9] == 0x01, "isRF=1 落在 data[9](帧内下标 10)")

    # 4) 帧封装：首尾哨兵
    fr = wrap_frame(build_data(mpdu))
    ck(fr[0] == 0x3C and fr[-1] == 0x3E, "帧以 3C 开头 3E 结尾")
    ck(unescape_frame_data(fr[1:-1]) == build_data(mpdu), "帧体反转义可还原 data")

    # 5) 行格式
    line = format_line(bytes([0x3C, 0x9C, 0x00, 0x3E]))
    ck(line == "0x3c 0x9c 0x00 0x3e", "行格式 0xNN 小写单空格")

    # 6) 解析容错
    ck(parse_hex("3C 9C\t00\n0x24,3a;44") == bytes([0x3C, 0x9C, 0x00, 0x24, 0x3A, 0x44]),
       "parse_hex 容忍 0x/逗号/分号/制表/换行")
    ck(parse_hex("3c9c0024") == bytes([0x3C, 0x9C, 0x00, 0x24]), "parse_hex 支持连写")

    # 7) already_full 模式：仅置 ts=0 并重算 dlen
    full = bytes([0xFF, 0xFF]) + bytes([1, 2, 3, 4]) + bytes([0x11, 0x22, 0x33, 0x01]) + b'\xAA\xBB'
    out = build_data(full, already_full=True)
    ck(out[2:6] == b'\x00\x00\x00\x00', "already_full 模式 ts 置 0")
    ck(list(out[6:]) == list(full[6:]), "already_full 模式保留 6B 之后内容")
    ck((out[0] | out[1] << 8) == len(full) - 6, "already_full 模式重算 dlen")

    # 8) 多行转换 + 注释跳过
    lines, st = convert("# 注释\n3C 9C 00 24\n\n3a44", is_rf=1)
    ck(len(lines) == 2 and st["frames"] == 2, "多行每行一帧，注释/空行跳过")
    ck(all(l.startswith("0x3c ") and l.endswith(" 0x3e") for l in lines), "每行都是完整 0x3c…0x3e")

    # 9) 回放端最小长度约束
    lines, st = convert("AA BB CC", is_rf=0)
    ck(len(lines) == 1 and len(st["warns"]) == 1, "MPDU 过短时给出回放端丢弃警告")
    lines, st = convert("AA " * 16, is_rf=0)
    ck(len(st["warns"]) == 0, "MPDU=16B 时无警告(data=26B 恰好满足)")

    print()
    if fails:
        print("RESULT: FAILURES (%d)" % len(fails))
        return 1
    print("RESULT: ALL PASS")
    return 0


# ---------------- GUI ----------------

def launch_gui():
    import os
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title("原始报文 → 0x3C/0x3E 封装 txt")
    root.geometry("880x720")

    pad = {"padx": 8, "pady": 4}

    tk.Label(root, text="① 原始报文（每行一帧，十六进制，可含 0x/空格/逗号；# 开头为注释）",
             anchor="w").pack(fill="x", **pad)
    src = tk.Text(root, height=10, wrap="none", font=("Consolas", 10))
    src.pack(fill="both", expand=False, **pad)

    hdr = tk.Frame(root)
    hdr.pack(fill="x", **pad)

    tk.Label(hdr, text="② isRF：").pack(side="left")
    is_rf_var = tk.IntVar(value=0)
    tk.Radiobutton(hdr, text="0 (PLC)", variable=is_rf_var, value=0).pack(side="left")
    tk.Radiobutton(hdr, text="1 (RF)", variable=is_rf_var, value=1).pack(side="left")

    tk.Label(hdr, text="    时间值：").pack(side="left")
    tk.Label(hdr, text="0（固定）", fg="#0a7").pack(side="left")

    opt = tk.Frame(root)
    opt.pack(fill="x", **pad)
    tk.Label(opt, text="③ 头字段：phr_mcs").pack(side="left")
    phr_var = tk.StringVar(value="0")
    tk.Entry(opt, textvariable=phr_var, width=6).pack(side="left", padx=(2, 10))
    tk.Label(opt, text="option").pack(side="left")
    opt_var = tk.StringVar(value="0")
    tk.Entry(opt, textvariable=opt_var, width=6).pack(side="left", padx=(2, 10))
    tk.Label(opt, text="channel").pack(side="left")
    ch_var = tk.StringVar(value="0")
    tk.Entry(opt, textvariable=ch_var, width=6).pack(side="left", padx=(2, 10))
    tk.Label(opt, text="（十六进制，默认 0）", fg="#888").pack(side="left")

    mode = tk.Frame(root)
    mode.pack(fill="x", **pad)
    tk.Label(mode, text="④ 输入模式：").pack(side="left")
    full_var = tk.IntVar(value=0)
    tk.Radiobutton(mode, text="报文为 MPDU（自动加 6B 头，dlen=MPDU+4）",
                   variable=full_var, value=0).pack(side="left")
    tk.Radiobutton(mode, text="报文已含完整 data（仅把时间 4B 置 0 并重算 dlen）",
                   variable=full_var, value=1).pack(side="left")

    outf = tk.Frame(root)
    outf.pack(fill="x", **pad)
    tk.Label(outf, text="⑤ 存储位置：").pack(side="left")
    path_var = tk.StringVar(value=os.path.join(os.getcwd(), "wrapped.txt"))
    tk.Entry(outf, textvariable=path_var).pack(side="left", fill="x", expand=True, padx=(2, 4))

    def browse():
        f = filedialog.asksaveasfilename(
            title="选择保存位置", defaultextension=".txt",
            filetypes=[("文本文件", "*.txt"), ("所有文件", "*.*")],
            initialfile=os.path.basename(path_var.get() or "wrapped.txt"))
        if f:
            path_var.set(f)

    tk.Button(outf, text="浏览…", command=browse).pack(side="left")

    tk.Label(root, text="⑥ 转换结果预览", anchor="w").pack(fill="x", **pad)
    prev = tk.Text(root, height=10, wrap="none", font=("Consolas", 9), state="disabled")
    prev.pack(fill="both", expand=True, **pad)

    status = tk.Label(root, text="就绪", anchor="w", fg="#333")
    status.pack(fill="x", **pad)

    def _int_of(s, name):
        s = (s or "").strip()
        if s.lower().startswith("0x"):
            s = s[2:]
        try:
            v = int(s, 16)
        except ValueError:
            raise ValueError("%s 不是合法的十六进制值：%r" % (name, s))
        if not (0 <= v <= 0xFF):
            raise ValueError("%s 超出 0xFF：0x%X" % (name, v))
        return v

    def do_convert():
        try:
            phr = _int_of(phr_var.get(), "phr_mcs")
            opv = _int_of(opt_var.get(), "option")
            chv = _int_of(ch_var.get(), "channel")
        except ValueError as e:
            messagebox.showerror("参数错误", str(e))
            return
        text = src.get("1.0", "end")
        lines, st = convert(text, is_rf=is_rf_var.get(), phr_mcs=phr, option=opv,
                            channel=chv, already_full=bool(full_var.get()))
        prev.configure(state="normal")
        prev.delete("1.0", "end")
        prev.insert("1.0", "\n".join(lines))
        prev.configure(state="disabled")
        if st.get("warns"):
            status.config(text="转换完成：%d 帧 / %d 字节；%d 行回放端会丢弃（见弹窗）"
                               % (st["frames"], st["bytes"], len(st["warns"])), fg="#c80")
            messagebox.showwarning("部分帧过短", "\n".join(st["warns"][:20]))
            if not st["errors"]:
                path = path_var.get().strip()
                if path:
                    try:
                        with open(path, "w", encoding="ascii", newline="\n") as f:
                            f.write("\n".join(lines) + "\n")
                    except OSError as e:
                        messagebox.showerror("写文件失败", str(e))
                return
        if st["errors"]:
            status.config(text="转换完成：%d 帧 / %d 字节；%d 行有问题"
                               % (st["frames"], st["bytes"], len(st["errors"])), fg="#c00")
            messagebox.showwarning("部分行失败", "\n".join(st["errors"][:20]))
            return
        if not lines:
            status.config(text="没有可用输入（请粘贴十六进制报文）", fg="#c00")
            return
        path = path_var.get().strip()
        if not path:
            status.config(text="已转换 %d 帧，但未选择存储位置" % st["frames"], fg="#c80")
            return
        try:
            with open(path, "w", encoding="ascii", newline="\n") as f:
                f.write("\n".join(lines) + "\n")
        except OSError as e:
            messagebox.showerror("写文件失败", str(e))
            return
        status.config(text="已转换 %d 帧 / %d 字节，已写入 %s"
                           % (st["frames"], st["bytes"], path), fg="#0a7")

    tk.Button(root, text="开始转换", height=2, bg="#2d7", fg="#000",
              command=do_convert).pack(fill="x", **pad)

    root.mainloop()
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    return launch_gui()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
