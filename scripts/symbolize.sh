#!/usr/bin/env bash
# 一键符号化: minidump -> 带函数名/行号的堆栈
# 用法:
#   bash scripts/symbolize.sh <xxx.dmp> <带调试信息的二进制> [符号输出目录]
#   bash scripts/symbolize.sh <xxx.dmp> --symdir <已生成好的符号目录>
#
# 流程: dump_syms 从二进制提取 .sym -> 按 模块名/hash/模块名.sym 布局
#       -> minidump_stackwalk 输出堆栈
set -e
cd "$(dirname "$0")/.."

DMP="${1:?用法: symbolize.sh <xxx.dmp> <二进制| --symdir 符号目录>}"
SECOND="${2:?用法: symbolize.sh <xxx.dmp> <二进制| --symdir 符号目录>}"
SYMOUT="${3:-symbols}"

TOOLS="3rdparty/install/symtools"
if [ ! -x "$TOOLS/dump_syms" ] || [ ! -x "$TOOLS/minidump_stackwalk" ]; then
    echo "符号工具缺失,先构建..."
    bash 3rdparty/build_sym_tools.sh
fi

if [ "$SECOND" = "--symdir" ]; then
    SYMDIR="$SYMOUT"
    [ -d "$SYMDIR" ] || { echo "符号目录不存在: $SYMDIR"; exit 1; }
else
    BIN="$SECOND"
    [ -f "$BIN" ] || { echo "二进制不存在: $BIN"; exit 1; }
    # 二进制必须带调试信息,否则 dump_syms 只能给出偏移量
    if ! file "$BIN" | grep -q "not stripped\|with debug_info"; then
        echo "警告: $BIN 似乎被 strip 过,符号化可能只有偏移量无函数名"
    fi
    SYMDIR="$SYMOUT"
    MOD=$(basename "$BIN")
    echo "== dump_syms 提取符号: $BIN"
    SYMFILE=$(mktemp)
    "$TOOLS/dump_syms" "$BIN" > "$SYMFILE"
    HASH=$(awk 'NR==1{print $4}' "$SYMFILE")
    [ -n "$HASH" ] || { echo "dump_syms 未输出 MODULE 行"; exit 1; }
    mkdir -p "$SYMDIR/$MOD/$HASH"
    mv "$SYMFILE" "$SYMDIR/$MOD/$HASH/$MOD.sym"
    echo "符号已生成: $SYMDIR/$MOD/$HASH/$MOD.sym"
fi

echo "== minidump_stackwalk 解析堆栈"
"$TOOLS/minidump_stackwalk" "$DMP" "$SYMDIR"
