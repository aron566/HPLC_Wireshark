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
# 附 Wireshark 解析插件(方便用户配合 Wireshark 用)
mkdir -p release/wireshark_support_plugins
cp -r wireshark_support_plugins/. release/wireshark_support_plugins/
rm -f release/wireshark_support_plugins/*.c   # 排除已落后的 C 版(README 标注勿用)

echo "== 3/3 NSIS 打包"
mkdir -p dist
# makensis:choco 装到 'C:\Program Files (x86)\NSIS' 但未必进当前 shell 的 PATH,
# 显式探测补 PATH(本地 tools/nsis-3.09 不在此列,仍由本地 package.sh 处理)
if ! command -v makensis >/dev/null 2>&1; then
    if [ -x "/c/Program Files (x86)/NSIS/makensis.exe" ]; then
        export PATH="/c/Program Files (x86)/NSIS:$PATH"
    elif [ -x "/c/Program Files/NSIS/makensis.exe" ]; then
        export PATH="/c/Program Files/NSIS:$PATH"
    fi
fi
SRCWIN=$(cygpath -w "$PWD/release")
makensis -DVERSION="$VER" "-DSRC=$SRCWIN" scripts/installer.nsi
ls -la "dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
echo "DONE: dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
