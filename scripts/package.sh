#!/usr/bin/env bash
# 打包脚本:构建 → windeployqt 收集运行时 → NSIS 生成安装包
# 用法:bash scripts/package.sh [版本号,默认取 .pro 的 VERSION]
set -e
cd "$(dirname "$0")/.."
# 版本号: 用 sed 而非 grep -oP —— GNU grep 的 PCRE 只支持 unibyte 与特定 UTF-8 locale,
# 在 LC_CTYPE=C.UTF-8 下会直接报 "grep: -P supports only unibyte and UTF-8 locales" 并
# 返回空, 导致产物名丢掉版本号(变成 BPLC_STA_Monitor_Setup_v.exe)。sed 是 POSIX 基本正则, 处处可用。
VER="${1:-$(sed -n 's/^VERSION[[:space:]]*=[[:space:]]*\([0-9.]*\).*/\1/p' BPLC_STA_Monitor.pro | head -1)}"
if [ -z "$VER" ]; then
    echo "错误: 无法从 BPLC_STA_Monitor.pro 解析 VERSION, 请显式传入版本号" >&2
    exit 2
fi
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
# 示范配置(参考 Linux 包的 config.ini.example): 让用户看到可配置项与默认值。
# 内容与 src/app/appconfig.h 的程序模板逐键一致 —— 下面的自检保证两者不漂移。
cat > release/config.ini.example <<'EOF'
; BPLC STA Monitor 配置示范文件
; BPLC STA Monitor configuration example
; 复制为 config.ini 后按需修改; 正式配置文件由程序首次启动自动生成(内容同本示范)。
; Copy to config.ini and edit as needed; the program auto-generates config.ini on first run.
;
; 段名带 % 的组段是 QSettings 的组段标记, 不可去掉 —— 否则程序读不到本段里的键。
; Section names prefixed with % are QSettings group markers; removing them breaks key lookup.

[app]
; 语言:auto=跟随系统 / zh=中文 / en=English
lang=auto
; 更新检查地址(update.json 清单)
update_url=https://raw.githubusercontent.com/aron566/HPLC_Wireshark/main/update.json
; 显示过滤器(启动时自动应用,留空=不过滤)
filter=
; 界面主题:auto=跟随系统(默认)/ dark=深色 / light=浅色
theme=auto
; 启动时自动检查更新(true=检查 / false=不检查)
auto_check=true
; 更新下载加速:auto=按出口IP区域自动(中国IP走代理)/ native=始终原生GitHub / proxy=始终走代理
mirror_mode=auto
; 代理前缀列表(逗号分隔,按序探活取首个可用;留空=内置 ghfast.top/gh-proxy.com/ghproxy.net)
mirror_list=
; 协议:gw_2026=国网双模标准2026(默认) / gw_2022=国网双模标准2022 / nw_2021=南网双模2021报批版 / nw_2026=南网双模2026
protocol=gw_2026

[reader]
; 输入源:0=串口 1=文件回放(bin) 2=裸hex文本
mode=0
; 串口参数
com=COM3
baud=460800
; 回放/裸hex 文件路径
file_path=

[crash]
; 崩溃捕获后端:sentry=上报Sentry服务 / crashpad=仅本地minidump / auto=自动(默认)
; 编译时用 qmake CONFIG+=crash_sentry / CONFIG+=crash_crashpad 选择包含的后端
backend=auto
; Sentry DSN(backend=sentry 时上报用);留空则只本地落盘不上报
dsn=
; 本地 dump 目录(留空=exe 同目录 crashpad_db)
db_path=
EOF
# 自检: 示范配置的键集合必须与程序模板(appconfig.h)的键集合完全一致, 防两处漂移
_example_keys=$(grep -oE '^[a-z_]+=' release/config.ini.example | sort -u)
# 只提取模板字面量里的键(行首空白 + 引号开头), 排除注释里出现的 "key=value" 之类
_tmpl_keys=$(grep -oE '^[[:space:]]+"[a-z_]+=' src/app/appconfig.h | grep -oE '[a-z_]+=' | sort -u)
if [ "$_example_keys" != "$_tmpl_keys" ]; then
    echo "::error::示范配置与 appconfig.h 模板的键不一致"
    diff <(echo "$_tmpl_keys") <(echo "$_example_keys") || true
    exit 1
fi
# 运行期产物: 程序启动时会在 exe 同目录建 logs/(QsLog 日志目录) 与
# crashpad_db/(崩溃转储数据库), 而 installer.nsi 的 File /r 是递归的
# ⇒ 增量打包时会把上一轮的运行日志与转储数据库打进安装包(用户机器上会带着
#   别人的运行痕迹)。两者一并清理。
rm -rf release/logs release/crashpad_db
# 产物新鲜度自检: exe 必须不早于最新源码 —— 否则"改了头文件但 make 只重链接、
# 依赖的 .cpp 没重编"就会让修复悄悄不进包(code-review-bot 实测踩到过)。
# 判据用 mtime 比较, 不用字符串查找(源码里是 path + ".bak" 拼接, 字符串不连续)。
STALE=$(find src main.cpp BPLC_STA_Monitor.pro -newer release/BPLC_STA_Monitor.exe 2>/dev/null | head -1)
if [ -n "$STALE" ]; then
    echo "::error::release/BPLC_STA_Monitor.exe 比源码旧($STALE) —— 修复可能未编入包"
    exit 1
fi
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
# 按**白名单**分发, 不用 cp -r 整目录: 整目录复制会把开发期的回归样本
# (几十个 test_*.pcap / *_fields.txt / 生成器 / DESIGN.md)一起塞进安装包 ——
# 用户装完只看到一堆用不上的文件, 安装目录不干净, 包体积也白涨。
# 白名单 = 4 个 dissector + 采集/转换工具 + 中英文说明(10 个);
# 另加下面 md2pdf 生成的 2 份 PDF, 插件目录最终 12 个文件。
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
    echo "错误: 插件分发件应为 10 个, 实际 $n" >&2
    exit 1
fi
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
# 断言: 插件目录最终 10 个分发件 + 2 份 PDF = 12(多了少了都要立刻失败, 不让脏包流出去)
n=$(find release/wireshark_support_plugins -type f | wc -l)
if [ "$n" -ne 12 ]; then
    echo "错误: 插件目录应恰为 12 个文件(10 分发件 + 2 PDF), 实际 $n" >&2
    exit 1
fi
echo "插件目录分发件计数自检通过: 12 个"
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
