#!/bin/bash
# run_crash_tests.sh - 两套后端崩溃捕获实测
#
# 前置:
#   1. 3rdparty/build_crash_deps.sh all        (构建第三方依赖)
#   2. 本脚本自动编译 src/crash + crash_test
#
# 流程(每个后端):
#   - 启动 mock_sentry(仅 sentry 后端需要,验证上传)
#   - 跑 crash_test <backend>,故意 SIGSEGV
#   - 检查 crashpad_db/reports/*.dmp 是否生成
#   - sentry 后端:再检查 mock_sentry 是否收到 envelope
set -e
cd "$(dirname "$0")/../../.."
ROOT="$PWD"

SENTRY_ROOT="$ROOT/3rdparty/install/sentry"
CRASHPAD_ROOT="$ROOT/3rdparty/install/crashpad"
BUILD_DIR="$ROOT/src/crash/tests/build"

mkdir -p "$BUILD_DIR"

compile_one() {
    local backend="$1"   # sentry | crashpad
    local macro="$2"     # CRASH_HAVE_SENTRY | CRASH_HAVE_CRASHPAD
    local inc="$3" lib="$4" libs="$5" extra_inc="$6" extra_inc2="$7"
    local out="$BUILD_DIR/crash_test_$backend"
    echo "=== compiling test for $backend ==="
    g++ -std=c++17 -g -O0 \
        -D"$macro" \
        -I"$ROOT/src/crash" -I"$inc" \
        ${extra_inc:+-I"$extra_inc"} ${extra_inc2:+-I"$extra_inc2"} \
        "$ROOT/src/crash/crash_handler.cpp" \
        "$ROOT/src/crash/crash_util.cpp" \
        "$ROOT/src/crash/backend_stub.cpp" \
        "$ROOT/src/crash/backend_sentry.cpp" \
        "$ROOT/src/crash/backend_crashpad.cpp" \
        "$ROOT/src/crash/tests/crash_test.cpp" \
        -L"$lib" $libs \
        -o "$out"
    # handler 二进制放测试程序同目录(后端按 exe 同目录查找)
    cp "$lib/../bin/crashpad_handler" "$BUILD_DIR/" 2>/dev/null || \
    cp "$inc/../bin/crashpad_handler" "$BUILD_DIR/"
    echo "built: $out"
}

run_one() {
    local backend="$1"
    local db="$BUILD_DIR/db_$backend"
    rm -rf "$db"
    echo "=== running crash test: $backend ==="
    # 故意崩溃,退出码非零是预期的
    set +e
    (cd "$BUILD_DIR" && SENTRY_DSN="${SENTRY_DSN:-}" ./crash_test_$backend "$backend" "$db")
    local code=$?
    set -e
    echo "[test] exit code: $code (crashed as expected)"
    local dumps
    dumps=$(find "$db" -name "*.dmp" 2>/dev/null | head -5)
    if [ -z "$dumps" ]; then
        echo "[test] FAIL: no .dmp found in $db"
        find "$db" -type f 2>/dev/null | head -10
        return 1
    fi
    echo "[test] PASS: dump(s) generated:"
    echo "$dumps"
    ls -la $dumps
}

# 检查 received/ 里是否有带 minidump 的上报(兼容 crashpad 的 gzip 压缩)
check_upload_received() {
    python3 - "$ROOT/3rdparty/received" <<'EOF'
import os, sys, gzip
d = sys.argv[1]
for f in sorted(os.listdir(d)):
    p = os.path.join(d, f)
    try:
        with open(p, 'rb') as fh:
            body = fh.read()
    except OSError:
        continue
    # 去掉 mock 写的文件头("### PATH: ...\n### AUTH: ...\n\n")
    body = body.split(b"\n\n", 1)[-1]
    if b"upload_file_minidump" in body:
        print(p)
        sys.exit(0)
    if body[:2] == b"\x1f\x8b":
        try:
            if b"upload_file_minidump" in gzip.decompress(body):
                print(p + " (gzip)")
                sys.exit(0)
        except Exception:
            pass
sys.exit(1)
EOF
}

# crashpad 后端上传测试:配 DSN,handler 应自动上报到 minidump 端点
run_upload_test() {
    local db="$BUILD_DIR/db_crashpad_upload"
    rm -rf "$db" "$ROOT/3rdparty/received"
    mkdir -p "$ROOT/3rdparty/received"
    echo "=== crashpad upload test (DSN -> mock minidump 端点) ==="
    fuser -k 9000/tcp 2>/dev/null || true
    sleep 1
    python3 "$ROOT/3rdparty/mock_sentry.py" 9000 >/tmp/mock_sentry.log 2>&1 &
    local mock_pid=$!
    sleep 1
    set +e
    (cd "$BUILD_DIR" && SENTRY_DSN="http://testkey@127.0.0.1:9000/1" \
        ./crash_test_crashpad crashpad "$db")
    set -e
    echo "[test] waiting for handler upload (handler 后台上报,约 1 分钟)..."
    local got=""
    for i in $(seq 1 150); do
        got=$(check_upload_received 2>/dev/null)
        if [ -n "$got" ]; then break; fi
        sleep 1
    done
    kill $mock_pid 2>/dev/null || true
    if [ -z "$got" ]; then
        echo "[test] FAIL: mock 未收到带 minidump 的上报"
        tail -5 /tmp/mock_sentry.log 2>/dev/null || true
        return 1
    fi
    echo "[test] PASS: mock 收到上报: $got"
    grep -a -o "### PATH: [^ ]*" "${got% (gzip)}" | head -1
}

# --- DSN -> minidump URL 单元测试(无第三方依赖) ---
echo "=== dsn url unit test ==="
g++ -std=c++17 -g -O0 \
    -I"$ROOT/src/crash" \
    "$ROOT/src/crash/crash_util.cpp" \
    "$ROOT/src/crash/tests/test_dsn_url.cpp" \
    -o "$BUILD_DIR/test_dsn_url"
"$BUILD_DIR/test_dsn_url"

# --- sentry 后端 ---
if [ -f "$SENTRY_ROOT/include/sentry.h" ]; then
    # libsentry.a 依赖 crashpad 静态库,按依赖顺序列出
    sentry_libs="-lsentry -lcrashpad_client -lcrashpad_handler_lib -lcrashpad_minidump"
    sentry_libs="$sentry_libs -lcrashpad_snapshot -lcrashpad_tools -lcrashpad_util"
    sentry_libs="$sentry_libs -lcrashpad_mpack -lcrashpad_compat -lmini_chromium -lunwind"
    compile_one sentry CRASH_HAVE_SENTRY \
        "$SENTRY_ROOT/include" "$SENTRY_ROOT/lib" \
        "$sentry_libs -lcurl -lz -ldl -lpthread"
    # mock sentry 服务(验证上传链路):先清掉占用端口的旧实例
    fuser -k 9000/tcp 2>/dev/null || true
    sleep 1
    python3 "$ROOT/3rdparty/mock_sentry.py" 9000 >/tmp/mock_sentry.log 2>&1 &
    MOCK_PID=$!
    sleep 1
    export SENTRY_DSN="http://testkey@127.0.0.1:9000/1"
    run_one sentry
    echo "--- mock_sentry received: ---"
    ls "$ROOT/3rdparty/received/" 2>/dev/null | tail -3
    cat /tmp/mock_sentry.log | tail -3
    kill $MOCK_PID 2>/dev/null || true
    unset SENTRY_DSN
else
    echo "SKIP sentry: $SENTRY_ROOT/include/sentry.h not found"
fi

# --- crashpad 原生后端 ---
if [ -f "$CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h" ]; then
    # 按实际 install 产物列出静态库
    libs=""
    for a in "$CRASHPAD_ROOT/lib"/libcrashpad_*.a; do
        name=$(basename "$a" .a); name=${name#lib}
        libs="$libs -l$name"
    done
    # mini_chromium 的 base 库
    for a in "$CRASHPAD_ROOT/lib"/libmini_chromium*.a; do
        [ -f "$a" ] || continue
        name=$(basename "$a" .a); name=${name#lib}
        libs="$libs -l$name"
    done
    compile_one crashpad CRASH_HAVE_CRASHPAD \
        "$CRASHPAD_ROOT/include" "$CRASHPAD_ROOT/lib" \
        "$libs -lcurl -lz -ldl -lpthread" \
        "$CRASHPAD_ROOT/include/crashpad" \
        "$CRASHPAD_ROOT/include/crashpad/mini_chromium"
    run_one crashpad
    run_upload_test
else
    echo "SKIP crashpad: $CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h not found"
fi

echo "ALL DONE"
