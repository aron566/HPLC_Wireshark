#!/usr/bin/env bash
# symbol_split.sh - Windows 符号分离:按编译器分支处理,再决定是否 strip 发布版
#
# MinGW(g++): DWARF 内嵌 exe → 存档带符号 exe(addr2line 符号化),strip 发布版
# MSVC(cl):   PDB 独立文件 → dump_syms 从 PDB 提取 .sym(minidump_stackwalk 符号化),
#             exe 本身不带调试信息,无需 strip
#
# 编译器检测:MSVC 链接产 .pdb(独立文件),MinGW 无 .pdb(DWARF 内嵌 exe)。
#
# 用法: bash scripts/symbol_split.sh <exe路径> <版本号>
# 产物: dist/BPLC_STA_Monitor_v<VER>_win64_symbols.tar.gz(带符号 exe 或 .sym)
set -e
cd "$(dirname "$0")/.."

EXE="${1:?用法: symbol_split.sh <exe路径> <版本号>}"
VER="${2:-0.0.0}"
[ -f "$EXE" ] || { echo "exe 不存在: $EXE" >&2; exit 1; }

EXE_DIR="$(dirname "$EXE")"
EXE_BASE="$(basename "$EXE" .exe)"
mkdir -p dist

# 检测 MSVC 的 PDB(独立调试信息文件):<exe名>.pdb 或 vc*.pdb
PDB=""
for p in "$EXE_DIR/$EXE_BASE.pdb" "$EXE_DIR"/vc*.pdb; do
    [ -f "$p" ] && PDB="$p" && break
done

if [ -n "$PDB" ]; then
    # ---- MSVC: dump_syms 从 PDB 提取 .sym ----
    echo "== 检测到 MSVC PDB: $PDB (走 dump_syms → .sym)"
    DUMP_SYMS="3rdparty/install/symtools/dump_syms.exe"
    if [ ! -x "$DUMP_SYMS" ]; then
        echo "Windows dump_syms 缺失,先构建(需 MSVC + DIA SDK):" >&2
        echo "  bash 3rdparty/build_sym_tools_win.sh" >&2
        exit 1
    fi
    SYMFILE=$(mktemp)
    if ! "$DUMP_SYMS" "$PDB" > "$SYMFILE" 2>/dev/null; then
        echo "dump_syms 读 PDB 失败: $PDB" >&2
        rm -f "$SYMFILE"
        exit 1
    fi
    HASH=$(awk 'NR==1{print $4}' "$SYMFILE")
    [ -n "$HASH" ] || { echo "dump_syms 未输出 MODULE 行" >&2; rm -f "$SYMFILE"; exit 1; }
    SYMDIR="symbols_tmp/$EXE_BASE/$HASH"
    mkdir -p "$SYMDIR"
    mv "$SYMFILE" "$SYMDIR/$EXE_BASE.sym"
    SYMPKG="dist/BPLC_STA_Monitor_v${VER}_win64_symbols.tar.gz"
    tar -czf "$SYMPKG" -C symbols_tmp .
    rm -rf symbols_tmp
    echo "符号包: $SYMPKG ($(du -h "$SYMPKG" | cut -f1))"
    echo "MSVC 分支:exe 不带调试信息,无需 strip"
else
    # ---- MinGW: 存档带符号 exe + strip 发布版 ----
    echo "== MinGW(DWARF):存档带符号 exe + strip 发布版"
    if ! command -v strip >/dev/null 2>&1; then
        echo "strip 不在 PATH(MinGW 工具链),请先 export PATH 含 mingw 的 bin" >&2
        exit 1
    fi
    mkdir -p symbols_tmp
    cp "$EXE" "symbols_tmp/$EXE_BASE.exe"
    SYMPKG="dist/BPLC_STA_Monitor_v${VER}_win64_symbols.tar.gz"
    tar -czf "$SYMPKG" -C symbols_tmp "$EXE_BASE.exe"
    rm -rf symbols_tmp
    strip "$EXE"
    echo "符号包: $SYMPKG ($(du -h "$SYMPKG" | cut -f1))"
    echo "发布版已 strip: $EXE ($(du -h "$EXE" | cut -f1))"
fi
