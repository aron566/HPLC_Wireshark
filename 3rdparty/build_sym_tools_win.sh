#!/usr/bin/env bash
# build_sym_tools_win.sh - 构建 Windows 版 dump_syms(读 MSVC PDB,产 .sym)
#
# 与 build_sym_tools.sh(Linux 版 dump_syms,读 ELF)互补:本脚本产出能读 PDB 的
# Windows dump_syms。MSVC 分支的符号分离(symbol_split.sh)依赖它。
#
# 前置条件(MinGW 环境无法编译,必须 MSVC):
#   1. 在「x64 Native Tools Command Prompt for VS」里运行(cl/nmake 在 PATH);
#   2. DIA SDK(Visual Studio 自带,提供 dia2.tlb 与 diaguids.lib)。
#
# 用法: bash 3rdparty/build_sym_tools_win.sh
# 产物: 3rdparty/install/symtools/dump_syms.exe
set -e
cd "$(dirname "$0")"
ROOT="$PWD"
SRC="$ROOT/breakpad-src"
OUT="$ROOT/install/symtools"

if [ -x "$OUT/dump_syms.exe" ]; then
    echo "Windows dump_syms 已存在: $OUT/dump_syms.exe"
    exit 0
fi

if ! command -v cl >/dev/null 2>&1; then
    echo "需要 MSVC 工具链:请在 'x64 Native Tools Command Prompt for VS' 里运行本脚本" >&2
    exit 1
fi

if [ ! -d "$SRC/src/tools/windows/dump_syms" ]; then
    echo "== 拉取 breakpad 源码(与 build_sym_tools.sh 共享)"
    rm -rf "$SRC"
    git clone --depth 1 https://github.com/google/breakpad.git "$SRC"
    (cd "$SRC" && git submodule update --init --depth 1 src/third_party/lss)
fi

mkdir -p "$OUT"
cd "$SRC"

# DIA SDK 头:dump_syms.cc 里 #import "dia2.tlb",需要 dia2.tlb 在 INCLUDE 能找到。
# VS 默认路径(示例,实际按本机 VS 版本调整):
#   VS2022: "C:\Program Files\Microsoft Visual Studio\2022\<edition>\DIA SDK\include"
# 若 cl 报 "cannot open dia2.tlb",把上述目录加进 INCLUDE 再跑。

echo "== 构建 Windows dump_syms (MSVC + DIA SDK)"
cl /EHsc /O2 /I src /I src/common/windows \
   src/tools/windows/dump_syms/dump_syms.cc \
   src/common/windows/omap.cc \
   src/common/windows/dia_util.cc \
   src/common/windows/guid_string.cc \
   src/common/windows/string_utils.cc \
   src/common/windows/pe_source_line_writer.cc \
   /Fe:"$OUT/dump_syms.exe" \
   ole32.lib oleaut32.lib diaguids.lib

echo "DONE: $OUT/dump_syms.exe"
echo "提示:与 Linux 版 dump_syms 同名不同格式,Windows 版读 PDB,Linux 版读 ELF;"
echo "      若同机两种都装,注意 3rdparty/install/symtools/ 下别互相覆盖。"
