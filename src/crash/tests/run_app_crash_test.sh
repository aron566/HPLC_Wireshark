#!/bin/bash
# run_app_crash_test.sh - 真实应用崩溃可用性验证(Linux)
#
# 验证打包产物(而非测试小程序)的崩溃检测链路是否可用:
#   1. 解压 dist/ 下的 Linux tarball(解压即用包)
#   2. 确认 crashpad_handler 与主程序同目录且可执行(打包遗漏会静默失效)
#   3. 用 -platform offscreen 启动真实 BPLC_STA_Monitor
#      (走 main.cpp 的 CrashHandler::install,读 config.ini 默认配置)
#   4. 外部发送 SIGSEGV(等价进程内崩溃,crashpad 信号处理器同样捕获)
#   5. 断言 <exe_dir>/crashpad_db 下生成带 MDMP 魔数的 .dmp
#
# 与 run_crash_tests.sh 的区别:那个测的是独立 crash_test 小程序,
# 这里测的是用户实际拿到的发布包。
set -e
cd "$(dirname "$0")/../../.."
ROOT="$PWD"

echo "=== 1/5 查找打包产物 ==="
TARBALL=$(ls -t dist/BPLC_STA_Monitor_v*_linux_x86_64.tar.gz 2>/dev/null | head -1)
if [ -z "$TARBALL" ]; then
    echo "[FAIL] dist/ 下没有 Linux tarball,先跑 scripts/package_linux.sh"
    exit 1
fi
echo "tarball: $TARBALL"

echo "=== 2/5 解压并检查崩溃组件 ==="
WORK=/tmp/app_crash_test
rm -rf "$WORK"; mkdir -p "$WORK"
tar xzf "$TARBALL" -C "$WORK"
APP="$WORK/usr/bin/BPLC_STA_Monitor"
HANDLER="$WORK/usr/bin/crashpad_handler"
[ -x "$APP" ] || { echo "[FAIL] 主程序缺失或不可执行: $APP"; exit 1; }
[ -x "$HANDLER" ] || { echo "[FAIL] crashpad_handler 缺失或不可执行(打包遗漏,崩溃检测将静默失效)"; exit 1; }
echo "[OK] 主程序与 crashpad_handler 就位且可执行"

echo "=== 3/5 offscreen 启动真实应用 ==="
export QT_QPA_PLATFORM=offscreen
"$WORK/usr/bin/run.sh" -platform offscreen > /tmp/app_crash_test.log 2>&1 &
APPPID=$!
# run.sh 末尾 exec 主程序,所以 $! 就是应用本身的 pid
sleep 15
if ! kill -0 $APPPID 2>/dev/null; then
    echo "[FAIL] 应用启动 15s 后已退出(非崩溃,是启动失败):"
    tail -25 /tmp/app_crash_test.log
    exit 1
fi
echo "[OK] 应用运行中 pid=$APPPID (崩溃处理器已在 main() 早期安装)"

echo "=== 4/5 发送 SIGSEGV,等待 dump 落盘 ==="
kill -SEGV $APPPID
DB="$WORK/usr/bin/crashpad_db"
DMP=""
for i in $(seq 1 60); do
    DMP=$(find "$DB" -name "*.dmp" 2>/dev/null | head -1)
    [ -n "$DMP" ] && break
    sleep 1
done
if [ -z "$DMP" ]; then
    echo "[FAIL] 60s 内 $DB 下无 .dmp 生成(崩溃未被捕获)"
    find "$DB" -type f 2>/dev/null | head
    exit 1
fi

echo "=== 5/5 校验 dump 有效性 ==="
MAGIC=$(head -c 4 "$DMP")
SIZE=$(stat -c%s "$DMP")
if [ "$MAGIC" != "MDMP" ]; then
    echo "[FAIL] $DMP 魔数不是 MDMP(文件损坏?)"
    exit 1
fi
if [ "$SIZE" -lt 1024 ]; then
    echo "[FAIL] $DMP 仅 $SIZE 字节,疑似空 dump"
    exit 1
fi
echo "[PASS] 真实应用崩溃可用: $DMP ($SIZE 字节, MDMP 魔数 OK)"
echo "ALL DONE"
