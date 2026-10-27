#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""parse_dump.py — 解析 crashpad 生成的 minidump,并用 addr2line 符号化异常调用栈。

MinGW/DWARF 构建无 PDB,cdb 只能显示 "模块+偏移";本脚本直接读 minidump 的
崩溃线程栈内存,扫描 exe 模块返回地址 → addr2line(DWARF) 映射到函数/源码行,
可精确到 serialreader.cpp:271 这种粒度(与 cdb 方案互补,不依赖 Windows SDK)。

用法:
  python parse_dump.py <dump.dmp> [exe路径]        # 异常上下文 + 崩溃线程调用链
  python parse_dump.py <dump.dmp> <exe> all        # 附加所有线程调用链

依赖:
  pip install minidump        (minidump 解析库)
  addr2line / objdump         (MinGW binutils,与构建 exe 同工具链;在 PATH 或 -addr2line 参数)

说明:
  - exe 路径缺省时尝试: dump 同目录找 BPLC_STA_Monitor.exe / 当前目录
  - ImageBase 缺省 0x140000000(MinGW 默认);可加 -imagebase 覆盖
  - "模块+偏移" 的帧(RVA 不在栈内或非 exe 模块)原样显示
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys

IMAGEBASE_DEFAULT = 0x140000000  # MinGW x64 默认;objdump -p exe | grep ImageBase 可查


def find_addr2line():
    for cand in (os.environ.get("ADDR2LINE", ""),):
        if cand and os.path.isfile(cand):
            return cand
    for name in ("addr2line", "addr2line.exe"):
        # 不要调外部 which:原生 Windows Python 没有 which,subprocess 会直接
        # 抛 FileNotFoundError;shutil.which 跨平台且能解析 .exe 后缀。
        p = shutil.which(name)
        if p:
            return p
        # Windows PATH 常见位置
        for base in ("C:/Qt/Tools/mingw1310_64/bin", "C:/Qt/Tools/mingw*/bin"):
            for g in glob.glob(base):
                f = os.path.join(g, name)
                if os.path.isfile(f):
                    return f
    return None


def parse_x64_context(ctx: bytes):
    """x64 CONTEXT: Rip@0xF8 Rsp@0x98 Rbp@0xA0"""
    rip = struct.unpack_from("<Q", ctx, 0xF8)[0]
    rsp = struct.unpack_from("<Q", ctx, 0x98)[0]
    rbp = struct.unpack_from("<Q", ctx, 0xA0)[0]
    return rip, rsp, rbp


def read_thread_stack(dump_path, th):
    """读一线程的上下文与栈内存,返回 (ctx, stack)。"""
    with open(dump_path, "rb") as f:
        f.seek(th.ThreadContext.Rva)
        ctx = f.read(th.ThreadContext.DataSize)
        f.seek(th.Stack.Rva)
        stack = f.read(th.Stack.DataSize)
    return ctx, stack


def scan_exe_addrs(stack, exe_base, exe_size):
    """栈扫描:全部 exe 模块地址(去重,按栈 offset 降序=调用链由深到浅)。
    0 RVA(保留值)直接排除,使后续“更多”计数精确;扫描含栈尾最后一个 qword。"""
    seen = set()
    cands = []
    for off in range(0, len(stack) - 7, 8):
        v = struct.unpack_from("<Q", stack, off)[0]
        if exe_base <= v < exe_base + exe_size:
            rva = v - exe_base
            if rva != 0 and rva not in seen:
                seen.add(rva)
                cands.append((off, v))
    cands.sort(reverse=True)
    return cands


def read_dump(dmp: str):
    try:
        import minidump.minidumpfile as mdf
    except ImportError:
        sys.exit("[!] 缺少 minidump 库,请先安装: pip install minidump\n"
                 "    (Windows 建议用与 hermes 同环境的 python 或 py -3 -m pip install minidump)")
    md = mdf.MinidumpFile.parse(dmp)
    if md.exception is None or not md.exception.exception_records:
        print("[!] dump 无异常流(可能不是崩溃 dump)")
        # 与成功路径同为 5 元组,调用方解包不再 ValueError。
        return None, None, None, None, None
    stream = md.exception.exception_records[0]
    tid = stream.ThreadId
    er = stream.ExceptionRecord
    code = getattr(er.ExceptionCode, "value", er.ExceptionCode)
    addr = er.ExceptionAddress
    # 模块
    mods = []
    for m in md.modules.modules:
        name = getattr(m, "name", "") or ""
        mods.append((os.path.basename(name).lower(), m.baseaddress, m.size))
    return md, tid, code, addr, mods


def main():
    ap = argparse.ArgumentParser(description="crashpad minidump → addr2line 符号化调用栈")
    ap.add_argument("dump", help=".dmp 文件")
    ap.add_argument("exe", nargs="?", help="exe 路径(缺省自动找 dump 同目录 BPLC_STA_Monitor.exe)")
    ap.add_argument("mode", nargs="?", default="", help="all = 附加所有线程")
    ap.add_argument("-imagebase", type=lambda x: int(x, 16), default=IMAGEBASE_DEFAULT,
                    help=f"exe ImageBase(默认 0x{IMAGEBASE_DEFAULT:x})")
    ap.add_argument("-addr2line", help="addr2line 路径(缺省 PATH/常见位置)")
    args = ap.parse_args()

    if args.mode not in ("", "all"):
        sys.exit(f"未知 mode: {args.mode} (可用: all)")

    if not os.path.isfile(args.dump):
        sys.exit(f"dump 不存在: {args.dump}")
    if args.exe and not os.path.isfile(args.exe):
        sys.exit(f"exe 不存在: {args.exe}")

    exe = args.exe
    if not exe:
        for cand in (os.path.join(os.path.dirname(args.dump), "BPLC_STA_Monitor.exe"),
                     os.path.join(os.getcwd(), "BPLC_STA_Monitor.exe")):
            if os.path.isfile(cand):
                exe = cand
                break
    if not exe or not os.path.isfile(exe):
        print("[!] 未找到 exe,函数名/源码行将缺失(仅偏移);请传 exe 路径")
        exe = None

    a2l = args.addr2line or find_addr2line()
    if exe and not a2l:
        print("[!] 未找到 addr2line,函数名/源码行将缺失(仅偏移);请安装 MinGW binutils 或 -addr2line")

    md, tid, code, ex_addr, mods = read_dump(args.dump)
    if md is None:
        sys.exit(1)

    exe_mod = None
    for name, base, size in mods:
        if name.startswith("bplc"):
            exe_mod = (name, base, size)
            break

    print("=" * 78)
    print("异常: code=0x%08x  address=0x%x  thread_id=%d" % (code & 0xFFFFFFFF, ex_addr or 0, tid))
    if exe_mod:
        print("exe 模块: %s base=0x%x size=0x%x" % (exe_mod[0], exe_mod[1], exe_mod[2]))
    print("=" * 78)

    # 崩溃线程
    th = None
    for t in md.threads.threads:
        if t.ThreadId == tid:
            th = t
            break
    if th is None:
        sys.exit("[!] 崩溃线程不在线程列表")

    ctx, stack = read_thread_stack(args.dump, th)
    rip, rsp, rbp = parse_x64_context(ctx)
    print("崩溃线程 RIP=0x%x RSP=0x%x 栈=%d 字节" % (rip, rsp, len(stack)))
    print()

    if exe_mod is None:
        print("[!] dump 中无 exe 模块(BPLC_STA_Monitor),无法符号化偏移")
        return
    exe_base, exe_size = exe_mod[1], exe_mod[2]

    def symbol(rva):
        if not exe or not a2l:
            return "BPLC_STA_Monitor+0x%x" % rva
        r = subprocess.run([a2l, "-e", exe, "-f", "-C", hex(args.imagebase + rva)],
                           capture_output=True, text=True)
        lines = r.stdout.splitlines()
        fn = lines[0] if lines else "??"
        src = lines[1] if len(lines) > 1 else "??"
        if fn == "??" and "??" in src:
            return "BPLC_STA_Monitor+0x%x" % rva
        return "%s @ %s" % (fn, src)

    # 异常地址本身符号化(crashpad 的 ThreadContext RIP 指向 crashpad 内部,
    # ExceptionAddress 才是原始异常点;若在 exe 模块内直接映射)
    if ex_addr and exe_base <= ex_addr < exe_base + exe_size:
        print("异常地址 → %s" % symbol(ex_addr - exe_base))
        print()

    def print_chain(cands):
        shown = 0
        for off, v in cands:
            rva = v - exe_base
            print("  [0x%06x] %s" % (rva, symbol(rva)))
            shown += 1
            if shown >= 24:
                print("  ... (%d 更多)" % (len(cands) - shown))
                break
        if shown == 0:
            print("  (栈内未扫到 exe 模块返回地址;dump 可能为 DumpWithoutCrash 浅栈)")

    print("--- 崩溃线程调用链(栈深处 → 浅处,均 exe 模块) ---")
    print_chain(scan_exe_addrs(stack, exe_base, exe_size))

    if args.mode == "all":
        for t in md.threads.threads:
            if t.ThreadId == tid:
                continue
            ctx_t, stack_t = read_thread_stack(args.dump, t)
            rip_t, rsp_t, rbp_t = parse_x64_context(ctx_t)
            print()
            print("--- 线程 %d 调用链(RIP=0x%x,栈=%d 字节) ---" % (t.ThreadId, rip_t, len(stack_t)))
            print_chain(scan_exe_addrs(stack_t, exe_base, exe_size))


if __name__ == "__main__":
    main()
