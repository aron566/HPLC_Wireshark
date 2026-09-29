#!/usr/bin/env bash
# 构建崩溃符号化工具: dump_syms + minidump_stackwalk (来自 google/breakpad)
# 用法: bash 3rdparty/build_sym_tools.sh
# 产物: 3rdparty/install/symtools/{dump_syms,minidump_stackwalk}
set -e
cd "$(dirname "$0")"
ROOT="$PWD"
SRC="$ROOT/breakpad-src"
OUT="$ROOT/install/symtools"

if [ -x "$OUT/dump_syms" ] && [ -x "$OUT/minidump_stackwalk" ]; then
    echo "符号工具已存在: $OUT"
    exit 0
fi

if [ ! -d "$SRC/src/processor/minidump_stackwalk.cc" ]; then
    echo "== 拉取 breakpad 源码"
    rm -rf "$SRC"
    git clone --depth 1 https://github.com/google/breakpad.git "$SRC"
    (cd "$SRC" && git submodule update --init --depth 1 src/third_party/lss)
fi

mkdir -p "$OUT" /tmp/bp_inc
touch /tmp/bp_inc/config.h   # breakpad 的 config.h 占位(autotools 生成,此处不需要)
cd "$SRC"

echo "== 构建 dump_syms"
g++ -O2 -std=c++17 -I src -I src/third_party/lss -DHAVE_A_OUT_H \
    src/common/dwarf_cfi_to_module.cc src/common/dwarf_cu_to_module.cc \
    src/common/dwarf_line_to_module.cc src/common/dwarf_range_list_handler.cc \
    src/common/language.cc src/common/module.cc src/common/path_helper.cc \
    src/common/stabs_reader.cc src/common/stabs_to_module.cc \
    src/common/dwarf/bytereader.cc src/common/dwarf/dwarf2diehandler.cc \
    src/common/dwarf/dwarf2reader.cc src/common/dwarf/elf_reader.cc \
    src/common/linux/crc32.cc src/common/linux/dump_symbols.cc \
    src/common/linux/elf_symbols_to_module.cc src/common/linux/elfutils.cc \
    src/common/linux/file_id.cc src/common/linux/linux_libc_support.cc \
    src/common/linux/memory_mapped_file.cc src/common/linux/safe_readlink.cc \
    src/tools/linux/dump_syms/dump_syms.cc \
    -o "$OUT/dump_syms" -lz

echo "== 构建 libdisasm"
(cd src/third_party/libdisasm && \
    gcc -O2 -c ia32_implicit.c ia32_insn.c ia32_invariant.c ia32_modrm.c \
        ia32_opcode_tables.c ia32_operand.c ia32_reg.c ia32_settings.c \
        x86_disasm.c x86_format.c x86_imm.c x86_insn.c x86_misc.c x86_operand_list.c && \
    ar rcs "$OUT/libdisasm.a" *.o && rm -f *.o)

echo "== 构建 minidump_stackwalk"
SRCS=$(ls src/processor/*.cc | grep -v unittest | grep -v selftest \
    | grep -v minidump_dump | grep -v microdump_stackwalk | grep -v synth_minidump \
    | tr '\n' ' ')
g++ -O2 -std=c++17 -I src -I /tmp/bp_inc -I src/third_party/lss \
    $SRCS src/common/path_helper.cc \
    src/common/linux/scoped_tmpfile.cc src/common/linux/scoped_pipe.cc \
    "$OUT/libdisasm.a" \
    -o "$OUT/minidump_stackwalk" -lz

echo "DONE: $OUT/dump_syms $OUT/minidump_stackwalk"
