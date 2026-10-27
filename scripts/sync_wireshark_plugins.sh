#!/usr/bin/env bash
# 同步 Wireshark 解析插件到本机部署目录, 保证"部署副本"与"仓库源码"同源。
#
# 背景(为什么需要这个脚本):
#   安装包里的插件由 scripts/package_ci.sh 每次从 wireshark_support_plugins/ 复制,
#   所以 release/ 里的副本**永不分叉** —— 但用户手工装到
#   %APPDATA%\Wireshark\plugins\ 的那份是**拷贝**过去的, 仓库侧改了它不会自动更新。
#   这才是真正的分叉点: 仓库里修了协议 bug, 用户本机的 Wireshark 仍跑旧解析器,
#   于是"明明改了却看不到效果"。
#   本脚本把这一步自动化, 并提供 --check 供 CI 校验打包链未被人改坏。
#
# 用法:
#   bash scripts/sync_wireshark_plugins.sh --install   # 复制到部署目录(默认行为)
#   bash scripts/sync_wireshark_plugins.sh --check     # 只比对不复制; 有差异退出码 1
#
# 退出码: 0 = 已一致(且/或已同步); 1 = --check 模式下发现差异; 2 = 参数错误
set -e
cd "$(dirname "$0")/.."

MODE=install
case "${1:---install}" in
  --install) MODE=install ;;
  --check)   MODE=check ;;
  -h|--help)
    echo "用法: bash scripts/sync_wireshark_plugins.sh [--install|--check]"
    echo "  --install  把仓库 wireshark_support_plugins/*.lua 复制到本机 Wireshark 插件目录,并把 bplc_serial_extcap.py 复制到 extcap 目录(默认)"
    echo "  --check    只比对不复制, 有差异则退出码 1(供 CI 使用)"
    exit 0 ;;
  *) echo "未知参数: $1" >&2; exit 2 ;;
esac

SRC_DIR="wireshark_support_plugins"

# 目标部署目录: Windows 用 %APPDATA%\Wireshark\plugins, 其它平台用 XDG 约定
if [ -n "${APPDATA:-}" ]; then
  DST_DIR="$APPDATA/Wireshark/plugins"
else
  DST_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/wireshark/plugins"
fi

# extcap 脚本部署目录(与 plugins 并列,非其子目录): Windows 用 %APPDATA%\Wireshark\extcap, 其它平台用 XDG 约定
if [ -n "${APPDATA:-}" ]; then
  EXT_DST_DIR="$APPDATA/Wireshark/extcap"
else
  EXT_DST_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/wireshark/extcap"
fi

if [ ! -d "$SRC_DIR" ]; then
  echo "错误: 源目录不存在: $SRC_DIR" >&2
  exit 2
fi

total=0
changed=0
copied=0
for f in "$SRC_DIR"/*.lua; do
  [ -e "$f" ] || continue
  total=$((total + 1))
  b=$(basename "$f")
  if [ -f "$DST_DIR/$b" ] && cmp -s "$f" "$DST_DIR/$b"; then
    continue
  fi
  changed=$((changed + 1))
  if [ "$MODE" = "check" ]; then
    echo "差异: $b"
  else
    mkdir -p "$DST_DIR"
    cp -f "$f" "$DST_DIR/$b"
    copied=$((copied + 1))
    echo "已同步: $b"
  fi
done

# extcap 脚本:与插件循环同款比对/复制逻辑,计数单列(不混入插件总数)
ext_changed=0
ext_copied=0
for f in "$SRC_DIR"/bplc_serial_extcap.py; do
  [ -e "$f" ] || continue
  b=$(basename "$f")
  if [ -f "$EXT_DST_DIR/$b" ] && cmp -s "$f" "$EXT_DST_DIR/$b"; then
    continue
  fi
  ext_changed=$((ext_changed + 1))
  if [ "$MODE" = "check" ]; then
    echo "差异(extcap): $b"
  else
    mkdir -p "$EXT_DST_DIR"
    cp -f "$f" "$EXT_DST_DIR/$b"
    ext_copied=$((ext_copied + 1))
    echo "已同步(extcap): $b"
  fi
done

if [ "$MODE" = "check" ]; then
  if [ "$changed" -gt 0 ] || [ "$ext_changed" -gt 0 ]; then
    echo "::error::部署副本与仓库源不一致(插件 $changed/$total 个, extcap $ext_changed 个), 存在分叉风险"
    exit 1
  fi
  echo "部署副本与仓库源一致($total 个插件, extcap 已一致)"
  exit 0
fi

echo "完成: 共 $total 个插件, 与部署目录不同 $changed 个, 已复制 $copied 个 -> $DST_DIR"
echo "extcap: 与部署目录不同 $ext_changed 个, 已复制 $ext_copied 个 -> $EXT_DST_DIR"
if [ "$changed" -gt 0 ]; then
  echo "提示: Wireshark 需重启才会加载新插件"
fi
