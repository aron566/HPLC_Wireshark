#!/usr/bin/env bash
# 打包脚本:构建 → windeployqt 收集运行时 → NSIS 生成安装包
# 用法:bash scripts/package.sh [版本号,默认取 .pro 的 VERSION]
set -e
cd "$(dirname "$0")/.."
VER="${1:-$(grep -oP '^VERSION\s*=\s*\K[0-9.]+' BPLC_STA_Monitor.pro)}"
QTDIR="/c/Qt/6.11.2/mingw_64"
TOOLDIR="/c/Qt/Tools/mingw1310_64/bin"

export PATH="$TOOLDIR:$QTDIR/bin:$PATH"

echo "== 1/3 qmake + 构建"
rm -rf release Makefile* .qmake.stash
qmake BPLC_STA_Monitor.pro
mingw32-make -j"$(nproc)"
# 插件宿主进程 bplc-plugin-host.exe:独立工程,主程序经 QLocalServer IPC
# 启动它(须与主程序同目录),不在主 .pro 里,须单独构建并复制。
( cd src/plugin/host && qmake bplc-plugin-host.pro && mingw32-make -j"$(nproc)" )
cp -f src/plugin/host/release/bplc-plugin-host.exe release/

echo "== 1.5/3 符号分离:存档带符号 exe(供符号化),再 strip 发布版"
rm -rf symbols_tmp
bash scripts/symbol_split.sh release/BPLC_STA_Monitor.exe "$VER"
# 宿主 exe 同样符号分离:带符号版累积进同一个符号包,发布版 strip
bash scripts/symbol_split.sh release/bplc-plugin-host.exe "$VER"
rm -rf symbols_tmp

echo "== 2/3 windeployqt 收集 Qt 运行时"
windeployqt --release --no-translations --no-system-d3d-compiler \
    release/BPLC_STA_Monitor.exe
# 防本机 config.ini 被打包:用户配置文件永不属于安装包(升级时旧配置保留)
rm -f release/config.ini
# crashpad_handler.exe:崩溃转储的进程外组件,必须与主 exe 同目录,
# 否则 CrashHandler::install() 失败回退(无崩溃转储)。
HANDLER_BIN="3rdparty/install/crashpad/bin/crashpad_handler.exe"
if [ -f "$HANDLER_BIN" ]; then
    cp "$HANDLER_BIN" release/
    echo "已附带 crashpad_handler.exe"
else
    echo "::warning::未找到 $HANDLER_BIN,崩溃转储不可用(后端将回退)"
fi
# 附 Wireshark 解析插件(方便用户配合 Wireshark 用)
mkdir -p release/wireshark_support_plugins
cp -r wireshark_support_plugins/. release/wireshark_support_plugins/
rm -f release/wireshark_support_plugins/*.c   # 排除已落后的 C 版(README 标注勿用)
rm -rf release/wireshark_support_plugins/__pycache__  # Python 缓存垃圾不进包
rm -f release/wireshark_support_plugins/*.log          # 调试日志不进包
# 大文件不进包: 插件目录里的 *.pcap / *.bin 是开发回归样本, 仓库入库的最大只有 2.3MB
# (TESTSPLC_mixed_check.pcap 等); 而工作区遗留的四个巨文件(74MB / 73MB / 40MB 实帧 / 5MB 切片)
# 并未入库, 却被上面的 cp -r 一并带进包里 —— 安装包多带 ~190MB, 用户装完根本用不上。
# 阈值取 3MB: 保证所有入库样例(<2.3MB)保留, 所有未入库巨文件(>5MB)排除。
find release/wireshark_support_plugins -type f \( -name '*.pcap' -o -name '*.bin' \) -size +3M -delete
# 打包后自检: release/ 里的插件必须与仓库源逐字节一致(与 scripts/package_ci.sh 同款校验)。
# 本地与 CI 两套打包脚本都做这道校验, 保证"本地打的包"与"CI 打的包"插件来源一致,
# 杜绝安装包携带旧解析器 —— 用户装完发现"仓库里修了协议 bug 但 Wireshark 里没生效"。
for f in wireshark_support_plugins/*.lua; do
    b=$(basename "$f")
    if ! cmp -s "$f" "release/wireshark_support_plugins/$b"; then
        echo "错误: 打包插件与仓库源不一致: $b" >&2
        exit 1
    fi
done
echo "打包插件与仓库源一致性自检通过"
# 中英文使用说明 md → pdf 附带
bash scripts/md2pdf.sh wireshark_support_plugins/README.md    "release/wireshark_support_plugins/Wireshark插件使用说明.pdf"
bash scripts/md2pdf.sh wireshark_support_plugins/README_EN.md "release/wireshark_support_plugins/Wireshark_Plugin_Manual_EN.pdf"
# 本软件使用说明书(中英文)md → pdf 附带
mkdir -p release/docs
bash scripts/md2pdf.sh docs/USER_MANUAL_zh-CN.md "release/docs/BPLC_STA_Monitor使用说明书.pdf"
bash scripts/md2pdf.sh docs/USER_MANUAL_en.md    "release/docs/BPLC_STA_Monitor_User_Manual_EN.pdf"

echo "== 3/3 NSIS 打包"
mkdir -p dist
SRCWIN=$(cygpath -w "$PWD/release")
"./tools/nsis-3.09/makensis.exe" -DVERSION="$VER" "-DSRC=$SRCWIN" scripts/installer.nsi
ls -la "dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
echo "DONE: dist/BPLC_STA_Monitor_Setup_v${VER}.exe"
