#!/usr/bin/env bash
# CI 专用打包脚本(与 scripts/package.sh 分开,本地打包仍用 package.sh):
# 不硬编码本机 Qt/MinGW/NSIS 路径 —— 三者由 workflow 装入 PATH。
# 用法: bash scripts/package_ci.sh <版本号>
set -e
cd "$(dirname "$0")/.."
VER="${1:?用法: package_ci.sh <版本号>}"

echo "== 1/3 qmake + 构建"
rm -rf release Makefile* .qmake.stash
qmake BPLC_STA_Monitor.pro
mingw32-make -j4

echo "== 2/3 windeployqt 收集运行时"
windeployqt --release --no-translations --no-system-d3d-compiler \
    release/BPLC_STA_Monitor.exe
# 防 config.ini 被打包:用户配置文件永不属于安装包(升级时旧配置保留)
rm -f release/config.ini

echo "== 3/3 NSIS 打包"
mkdir -p dist
SRCWIN=$(cygpath -w "$PWD/release")
makensis -DVERSION="$VER" "-DSRC=$SRCWIN" scripts/installer.nsi
ls -la "dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
echo "DONE: dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
