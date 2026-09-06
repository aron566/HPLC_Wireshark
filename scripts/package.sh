#!/usr/bin/env bash
# 打包脚本:构建 → windeployqt 收集运行时 → NSIS 生成安装包
# 用法:bash scripts/package.sh [版本号,默认 1.0.1]
set -e
cd "$(dirname "$0")/.."
VER="${1:-1.0.1}"
QTDIR="/c/Qt/6.10.1/mingw_64"
TOOLDIR="/c/Qt/Tools/mingw1310_64/bin"

export PATH="$TOOLDIR:$QTDIR/bin:$PATH"

echo "== 1/3 qmake + 构建"
rm -rf release Makefile* .qmake.stash
qmake BPLC_STA_Monitor.pro
mingw32-make -j4

echo "== 2/3 windeployqt 收集 Qt 运行时"
windeployqt --release --no-translations --no-system-d3d-compiler \
    release/BPLC_STA_Monitor.exe
# 防本机 config.ini 被打包:用户配置文件永不属于安装包(升级时旧配置保留)
rm -f release/config.ini

echo "== 3/3 NSIS 打包"
mkdir -p dist
SRCWIN=$(cygpath -w "$PWD/release")
"./tools/nsis-3.09/makensis.exe" -DVERSION="$VER" "-DSRC=$SRCWIN" scripts/installer.nsi
ls -la "dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
echo "DONE: dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
