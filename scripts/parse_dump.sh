#!/bin/bash
# parse_dump.sh — 一键解析 crashpad 生成的 minidump
# 用法:
#   ./scripts/parse_dump.sh <dump文件.dmp>              # 异常上下文 + 当前线程栈
#   ./scripts/parse_dump.sh <dump文件.dmp> all          # 附加所有线程栈
#   ./scripts/parse_dump.sh <dump文件.dmp> modules      # 附加模块列表
# 依赖: Windows SDK 的 cdb.exe (通常已随 SDK 安装)
set -e

CDB="C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe"

if [ ! -f "$CDB" ]; then
  echo "未找到 cdb.exe,请安装 Windows SDK Debuggers (windbg/cdb)" >&2
  exit 1
fi
if [ $# -lt 1 ]; then
  echo "用法: $0 <dump文件.dmp> [all|modules]" >&2
  exit 1
fi

DUMP="$1"
[ ! -f "$DUMP" ] && { echo "dump 文件不存在: $DUMP" >&2; exit 1; }

# 关键命令:
#   .sympath ""   清空符号路径,不联网(否则卡在微软符号服务器)
#   .ecxr         异常上下文:崩溃时的寄存器,rip=0 + rax=0 即 NULL 函数指针调用
#   kn            当前线程调用栈(模块名+偏移)
#   ~*k           所有线程调用栈
#   lm            模块列表
CMDS=".sympath \"\"; .ecxr; kn"
case "${2:-}" in
  all)     CMDS="$CMDS; ~*k" ;;
  modules) CMDS="$CMDS; lm" ;;
esac

"$CDB" -z "$DUMP" -c "$CMDS; q" 2>&1 | grep -vE "^\*|NatVis|Symbol search|Executable search|^Loading|^For analysis|quit:|^$"
