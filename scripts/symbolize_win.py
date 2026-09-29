#!/usr/bin/env python3
"""symbolize_win.py - Windows minidump 符号化(MinGW DWARF + addr2line)。

MinGW 构建的 exe 用 DWARF 调试信息(无 PDB),无法走 breakpad .sym 流程。
本脚本从 dump 的 Exception 流取出崩溃地址,定位到所属模块,计算 RVA,
再用 MinGW 自带的 addr2line 解析出函数名与源码行。

用法: symbolize_win.py <xxx.dmp> <xxx.exe>
要求: addr2line 在 PATH(随 MinGW 提供),exe 构建时带 -g 且未 strip。
"""
import struct
import subprocess
import sys

STREAM_EXCEPTION = 6
STREAM_MODULELIST = 4


def read_cstr_u16(data, rva):
    (length,) = struct.unpack_from("<I", data, rva)
    raw = data[rva + 4:rva + 4 + length]
    return raw.decode("utf-16-le", errors="replace")


def parse_dump(path):
    data = open(path, "rb").read()
    magic, _, stream_count, stream_rva = struct.unpack_from("<IIII", data, 0)
    assert magic == 0x504D444D, f"非 minidump: {magic:08x}"

    exc_rva = mod_rva = None
    for i in range(stream_count):
        off = stream_rva + i * 12
        stype, _, rva = struct.unpack_from("<III", data, off)
        if stype == STREAM_EXCEPTION:
            exc_rva = rva
        elif stype == STREAM_MODULELIST:
            mod_rva = rva
    assert exc_rva is not None, "dump 缺少 Exception 流"
    assert mod_rva is not None, "dump 缺少 ModuleList 流"

    # MINIDUMP_EXCEPTION_STREAM: ThreadId(4) + align(4) + ExceptionRecord(152)
    # ExceptionRecord 内: Code(4) Flags(4) Record(8) Address(8) ...
    (code,) = struct.unpack_from("<I", data, exc_rva + 8)
    (addr,) = struct.unpack_from("<Q", data, exc_rva + 8 + 16)
    print(f"异常码: {code:#x}  崩溃地址: {addr:#x}")

    # ModuleList: count(4) + 108 字节/模块
    (count,) = struct.unpack_from("<I", data, mod_rva)
    hit = None
    for m in range(count):
        moff = mod_rva + 4 + m * 108
        base, size = struct.unpack_from("<QI", data, moff)
        name_rva = struct.unpack_from("<I", data, moff + 20)[0]
        if base <= addr < base + size:
            hit = (read_cstr_u16(data, name_rva), base, size)
            break
    assert hit, f"崩溃地址 {addr:#x} 不在任何模块内"
    name, base, size = hit
    print(f"所属模块: {name}  base={base:#x}")
    return addr - base, name


def pe_image_base(exe):
    """从 PE 可选头读 preferred ImageBase,供 addr2line 换算。"""
    with open(exe, "rb") as f:
        f.seek(0x3C)
        (pe_off,) = struct.unpack("<I", f.read(4))
        f.seek(pe_off)
        assert f.read(4) == b"PE\0\0", "非 PE 文件"
        f.seek(pe_off + 24)
        (magic,) = struct.unpack("<H", f.read(2))
        assert magic == 0x20B, "仅支持 64 位 PE32+"
        # PE32+ 可选头内偏移:Magic(2)+主次版本号(2)+SizeOfCode(4)
        #  +SizeOfInitializedData(4)+SizeOfUninitializedData(4)
        #  +AddressOfEntryPoint(4)+BaseOfCode(4) = 24,ImageBase 在 +24。
        # 注意:32 位 PE(0x10b)因多一个 BaseOfData(4),ImageBase 才在 +28,别混用。
        f.seek(pe_off + 24 + 24)
        (base,) = struct.unpack("<Q", f.read(8))
        return base


def addr2line(exe, va):
    out = subprocess.run(
        ["addr2line", "-e", exe, "-f", "-C", f"{va:#x}"],
        capture_output=True, text=True)
    lines = out.stdout.strip().splitlines()
    func = lines[0] if lines else "??"
    loc = lines[1] if len(lines) > 1 else "??:?"
    if func == "??" and out.stderr.strip():
        # 文件打不开 vs 地址无调试信息是两种完全不同的失败,打印 stderr 区分
        print(f"[addr2line {va:#x}] stderr: {out.stderr.strip()}")
    return func, loc


def main():
    dmp, exe = sys.argv[1], sys.argv[2]
    rva, mod = parse_dump(dmp)
    print(f"模块内 RVA: {rva:#x}")
    try:
        ibase = pe_image_base(exe)
        print(f"PE ImageBase: {ibase:#x}")
        va = ibase + rva
    except Exception as e:
        print(f"读 PE ImageBase 失败({e}),直接用 RVA 尝试")
        va = rva
    func, loc = addr2line(exe, va)
    if ("??:?" in loc or func == "??") and va != rva:
        func, loc = addr2line(exe, rva)  # 兜底:直接 RVA
    print(f"函数: {func}")
    print(f"位置: {loc}")
    if loc.startswith("??:") or func == "??":
        print("WARN: 未解析出函数/源码行(可能 exe 被 strip 或缺 DWARF)")
        sys.exit(2)
    print("OK: 符号化成功")


if __name__ == "__main__":
    main()
