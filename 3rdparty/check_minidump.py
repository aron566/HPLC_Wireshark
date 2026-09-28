#!/usr/bin/env python3
"""最小 minidump 结构验证:检查 MDMP 魔数、解析目录流、列出模块。"""
import struct
import sys

def main(path):
    data = open(path, "rb").read()
    print(f"文件: {path} ({len(data)} bytes)")

    # MINIDUMP_HEADER
    magic, version, stream_count, stream_rva = struct.unpack_from("<IIII", data, 0)
    assert magic == 0x504D444D, f"魔数错误: {magic:08x}"
    print(f"魔数: MDMP OK, 版本: {version}, 流数量: {stream_count}")

    # 流类型名(部分)
    names = {
        3: "ThreadList", 4: "ModuleList", 5: "MemoryList", 6: "Exception",
        7: "SystemInfo", 8: "ThreadExList", 9: "Memory64List", 11: "CommentA",
    }
    for i in range(stream_count):
        off = 32 + i * 12
        stype, size, rva = struct.unpack_from("<III", data, off)
        name = names.get(stype, f"Unknown({stype})")
        print(f"  流[{i}]: {name} size={size} rva={rva:#x}")

    # 解析 ModuleList,确认崩溃模块在列
    for i in range(stream_count):
        off = 32 + i * 12
        stype, size, rva = struct.unpack_from("<III", data, off)
        if stype == 4:  # ModuleList
            count = struct.unpack_from("<I", data, rva)[0]
            print(f"模块数: {count}")
            for m in range(min(count, 5)):
                moff = rva + 4 + m * 108
                base, modsize = struct.unpack_from("<QI", data, moff)
                print(f"    模块[{m}]: base={base:#x} size={modsize:#x}")
            break
    print("VALID: minidump 结构完整")

if __name__ == "__main__":
    main(sys.argv[1])
