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

echo "== 2/3 windeployqt 收集运行时"
windeployqt --release --no-translations --no-system-d3d-compiler \
    release/BPLC_STA_Monitor.exe
# 防 config.ini 被打包:用户配置文件永不属于安装包(升级时旧配置保留)
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
# 按**白名单**分发(与 scripts/package.sh 同款), 不用 cp -r 整目录:
# 整目录复制会把开发期回归样本(几十个 test_*.pcap / *_fields.txt / 生成器)一起塞进安装包。
mkdir -p release/wireshark_support_plugins
# 注意: 通配必须写在**源路径**上(wireshark_support_plugins/packet-*.lua) —— 写在 for 的
# 词表里会按当前目录展开, 仓库根没有 packet-*.lua, cp 会拿到字面量而失败。
cp -f wireshark_support_plugins/packet-*.lua release/wireshark_support_plugins/
for f in bplc_serial_extcap.py BPLC_Serial_Capture.bat bin2pcap.py serial2pcap.py \
         README.md README_EN.md; do
    cp -f "wireshark_support_plugins/$f" release/wireshark_support_plugins/
done
n=$(find release/wireshark_support_plugins -type f | wc -l)
if [ "$n" -ne 10 ]; then
    echo "::error::插件分发件应为 10 个, 实际 $n"
    exit 1
fi
# 打包后自检: release/ 里的插件必须与仓库源逐字节一致。
# 防止将来有人改坏上面的拷贝逻辑(漏 cp / 路径写错), 导致安装包携带旧解析器 ——
# 用户装完发现"仓库里修了协议 bug 但 Wireshark 里没生效", 根因就在这种静默分叉。
for f in wireshark_support_plugins/*.lua; do
    b=$(basename "$f")
    if ! cmp -s "$f" "release/wireshark_support_plugins/$b"; then
        echo "::error::打包插件与仓库源不一致: $b"
        exit 1
    fi
done
echo "打包插件与仓库源一致性自检通过"
# 中英文使用说明 md → pdf 附带
bash scripts/md2pdf.sh wireshark_support_plugins/README.md    "release/wireshark_support_plugins/Wireshark插件使用说明.pdf"
bash scripts/md2pdf.sh wireshark_support_plugins/README_EN.md "release/wireshark_support_plugins/Wireshark_Plugin_Manual_EN.pdf"
# 断言: 插件目录最终 10 个分发件 + 2 份 PDF = 12(多了少了都要立刻失败, 不让脏包流出去)
n=$(find release/wireshark_support_plugins -type f | wc -l)
if [ "$n" -ne 12 ]; then
    echo "::error::插件目录应恰为 12 个文件(10 分发件 + 2 PDF), 实际 $n"
    exit 1
fi
echo "插件目录分发件计数自检通过: 12 个"
# 本软件使用说明书(中英文)md → pdf 附带
mkdir -p release/docs
bash scripts/md2pdf.sh docs/USER_MANUAL_zh-CN.md "release/docs/BPLC_STA_Monitor使用说明书.pdf"
bash scripts/md2pdf.sh docs/USER_MANUAL_en.md    "release/docs/BPLC_STA_Monitor_User_Manual_EN.pdf"

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
