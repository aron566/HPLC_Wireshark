#!/bin/bash
# 失败路径测试:handler 缺失 / db 不可写 / DSN 无效 / 无 DSN 本地落盘
# 未构建的后端对应的用例自动 SKIP(不计 FAIL),与 run_crash_tests.sh 保持一致。
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD_DIR="$ROOT/src/crash/tests/build"
cd "$BUILD_DIR" || exit 1

pass=0; fail=0; skip=0
ok()   { echo "[PASS] $1"; pass=$((pass+1)); }
bad()  { echo "[FAIL] $1"; fail=$((fail+1)); }
sk()   { echo "[SKIP] $1"; skip=$((skip+1)); }

HAVE_SENTRY=0;   [ -x ./crash_test_sentry ]   && HAVE_SENTRY=1
HAVE_CRASHPAD=0; [ -x ./crash_test_crashpad ] && HAVE_CRASHPAD=1

# --- 1. sentry 后端:handler 缺失时 install 不应崩溃 ---
echo "=== 1. handler 缺失 ==="
if [ "$HAVE_SENTRY" = "1" ]; then
mkdir -p no_handler_dir && cd no_handler_dir
# 把 handler 藏起来
[ -f ../crashpad_handler ] && mv ../crashpad_handler ../crashpad_handler.bak
rm -rf db1
timeout 10 ../crash_test_sentry sentry ./db1 >/dev/null 2>&1
code=$?
[ -f ../crashpad_handler.bak ] && mv ../crashpad_handler.bak ../crashpad_handler
cd "$BUILD_DIR"
# 139=崩溃且被捕获;2=install 优雅失败(返回空,程序主动退出,不 abort)
# handler 缺失时期望 install 失败但不崩溃
if [ "$code" = "2" ]; then
    ok "handler 缺失时 install 优雅失败(返回空,退出码=2)"
else
    bad "handler 缺失时异常退出码=$code(期望 2)"
fi
else
    sk "未构建 crash_test_sentry"
fi

# --- 2. db 目录不可写 ---
echo "=== 2. db 目录不可写 ==="
if [ "$HAVE_SENTRY" = "1" ]; then
rm -rf db_ro && mkdir -p db_ro && chmod 555 db_ro
timeout 10 ./crash_test_sentry sentry ./db_ro >/tmp/ro.log 2>&1
code=$?
chmod 755 db_ro
# db 不可写时期望 install 优雅失败(退出码=2),不 abort
if [ "$code" = "2" ]; then
    ok "db 不可写时 install 优雅失败(退出码=2)"
else
    bad "db 不可写时退出码=$code(期望 2)"
fi
else
    sk "未构建 crash_test_sentry"
fi

# --- 3. DSN 无效(不可达)时不阻塞崩溃流程 ---
echo "=== 3. DSN 不可达 ==="
if [ "$HAVE_SENTRY" = "1" ]; then
rm -rf db_baddsn
timeout 20 env SENTRY_DSN="http://bad@127.0.0.1:59999/9" ./crash_test_sentry sentry ./db_baddsn >/dev/null 2>&1
code=$?
if ls db_baddsn/pending/*.dmp >/dev/null 2>&1; then
    ok "DSN 不可达时 dump 仍本地落盘(退出码=$code)"
else
    bad "DSN 不可达时无本地 dump(退出码=$code)"
fi
else
    sk "未构建 crash_test_sentry"
fi

# --- 4. 无 DSN:纯本地落盘,不尝试上传 ---
echo "=== 4. 无 DSN 本地落盘 ==="
if [ "$HAVE_SENTRY" = "1" ]; then
rm -rf db_nodsn
timeout 10 env -u SENTRY_DSN ./crash_test_sentry sentry ./db_nodsn >/tmp/nodsn.log 2>&1
code=$?
if ls db_nodsn/pending/*.dmp >/dev/null 2>&1; then
    ok "无 DSN 时 dump 本地落盘(退出码=$code)"
else
    bad "无 DSN 时无本地 dump(退出码=$code)"
fi
else
    sk "未构建 crash_test_sentry"
fi

# --- 5. crashpad 后端:handler 缺失 ---
echo "=== 5. crashpad 后端 handler 缺失 ==="
if [ "$HAVE_CRASHPAD" = "1" ]; then
[ -f ./crashpad_handler ] && mv ./crashpad_handler ./crashpad_handler.bak
rm -rf db_cph
timeout 10 ./crash_test_crashpad crashpad ./db_cph >/tmp/cph.log 2>&1
code=$?
[ -f ./crashpad_handler.bak ] && mv ./crashpad_handler.bak ./crashpad_handler
# crashpad 无 handler 时期望 install 优雅失败(退出码=2)
if [ "$code" = "2" ]; then
    ok "crashpad handler 缺失时 install 优雅失败(退出码=2)"
else
    bad "crashpad handler 缺失时退出码=$code(期望 2)"
fi
else
    sk "未构建 crash_test_crashpad"
fi

echo "=== 失败路径: PASS=$pass FAIL=$fail SKIP=$skip ==="
[ "$fail" = "0" ]
