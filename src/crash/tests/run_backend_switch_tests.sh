#!/bin/bash
# run_backend_switch_tests.sh - 后端切换矩阵测试(Linux)
#
# 验证"正常切换后预期行为都正常实现":
#   编译变体 none/sentry/crashpad/both × backend 参数 auto/sentry/crashpad
#
# 每个用例断言两件事:
#   1. 选型正确:crash_test 打印的 active= 值符合预期
#   2. 行为正确:崩溃→dump 落盘;sentry+DSN→mock 收到 envelope 上报;
#      未编译的后端→优雅失败(exit 2,不崩溃、不产生 dump)
#
# 前置: 3rdparty/build_crash_deps.sh all
set -e
cd "$(dirname "$0")/../../.."
ROOT="$PWD"

SENTRY_ROOT="$ROOT/3rdparty/install/sentry"
CRASHPAD_ROOT="$ROOT/3rdparty/install/crashpad"
BUILD_DIR="$ROOT/src/crash/tests/build_switch"
RECEIVED="$ROOT/3rdparty/received"

if [ ! -f "$SENTRY_ROOT/include/sentry.h" ]; then
    echo "SKIP: sentry 未构建,先跑 3rdparty/build_crash_deps.sh all"
    exit 0
fi
if [ ! -f "$CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h" ]; then
    echo "SKIP: crashpad 未构建,先跑 3rdparty/build_crash_deps.sh all"
    exit 0
fi

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); echo "[PASS] $1"; }
fail() { FAIL=$((FAIL+1)); echo "[FAIL] $1"; }

# --- 编译 4 种变体(与 qmake CONFIG none/sentry/crashpad/both 对应) ---
CRASH_SRCS="$ROOT/src/crash/crash_handler.cpp $ROOT/src/crash/crash_util.cpp \
    $ROOT/src/crash/backend_stub.cpp $ROOT/src/crash/backend_sentry.cpp \
    $ROOT/src/crash/backend_crashpad.cpp $ROOT/src/crash/tests/crash_test.cpp"
SENTRY_LIBS="-lsentry -lcrashpad_client -lcrashpad_handler_lib -lcrashpad_minidump \
    -lcrashpad_snapshot -lcrashpad_tools -lcrashpad_util \
    -lcrashpad_mpack -lcrashpad_compat -lmini_chromium -lunwind"
CRASHPAD_LIBS=""
for a in "$CRASHPAD_ROOT/lib"/libcrashpad_*.a "$CRASHPAD_ROOT/lib"/libmini_chromium*.a; do
    [ -f "$a" ] || continue
    n=$(basename "$a" .a); CRASHPAD_LIBS="$CRASHPAD_LIBS -l${n#lib}"
done
SYS_LIBS="-lcurl -lz -ldl -lpthread"

build_variant() { # $1=变体名 $2=宏定义(可空,空格分隔)
    local v="$1" macros="$2"
    local d="$BUILD_DIR/v_$v"
    rm -rf "$d"; mkdir -p "$d"
    local defs=""
    for m in $macros; do defs="$defs -D$m"; done
    # both 变体按 qmake 的真实行为链接两套库(静态库重复符号按首个定义解析,不报错)
    echo "=== compiling variant: $v ==="
    # shellcheck disable=SC2086
    g++ -std=c++17 -g -O0 $defs \
        -I"$ROOT/src/crash" \
        -I"$SENTRY_ROOT/include" \
        -I"$CRASHPAD_ROOT/include" \
        -I"$CRASHPAD_ROOT/include/crashpad" \
        -I"$CRASHPAD_ROOT/include/crashpad/mini_chromium" \
        $CRASH_SRCS \
        -L"$SENTRY_ROOT/lib" -L"$CRASHPAD_ROOT/lib" \
        $SENTRY_LIBS $CRASHPAD_LIBS $SYS_LIBS \
        -o "$d/crash_test"
    echo "built: $d/crash_test"
}

build_variant none ""
build_variant sentry "CRASH_HAVE_SENTRY"
build_variant crashpad "CRASH_HAVE_CRASHPAD"
build_variant both "CRASH_HAVE_SENTRY CRASH_HAVE_CRASHPAD"

# handler 配对:sentry 系用 sentry 自带的 crashpad_handler(会转 envelope 上报),
# crashpad 系用独立构建的 handler;both 变体按要测的后端分别配
cp "$SENTRY_ROOT/bin/crashpad_handler" "$BUILD_DIR/v_sentry/"
cp "$CRASHPAD_ROOT/bin/crashpad_handler" "$BUILD_DIR/v_crashpad/"
rm -rf "$BUILD_DIR/v_both_sentry" "$BUILD_DIR/v_both_crashpad"
cp -r "$BUILD_DIR/v_both" "$BUILD_DIR/v_both_sentry"
cp -r "$BUILD_DIR/v_both" "$BUILD_DIR/v_both_crashpad"
cp "$SENTRY_ROOT/bin/crashpad_handler" "$BUILD_DIR/v_both_sentry/"
cp "$CRASHPAD_ROOT/bin/crashpad_handler" "$BUILD_DIR/v_both_crashpad/"

# --- mock sentry(验证 sentry 后端的 envelope 上报) ---
fuser -k 9000/tcp 2>/dev/null || true
sleep 1
rm -rf "$RECEIVED"; mkdir -p "$RECEIVED"
python3 "$ROOT/3rdparty/mock_sentry.py" 9000 >/tmp/mock_sentry_switch.log 2>&1 &
MOCK_PID=$!
sleep 1
trap 'kill $MOCK_PID 2>/dev/null || true' EXIT
DSN="http://testkey@127.0.0.1:9000/1"

received_snapshot() { ls "$RECEIVED" 2>/dev/null | sort; }
wait_for_upload() { # $1=超时秒数:等 received/ 出现新文件
    local timeout="$1" before="$2" f
    for _ in $(seq 1 "$timeout"); do
        f=$(comm -13 <(echo "$before") <(received_snapshot) | head -1)
        if [ -n "$f" ]; then echo "$RECEIVED/$f"; return 0; fi
        sleep 1
    done
    return 1
}

# --- 用例执行 ---
# run_case <用例名> <变体目录> <backend参数> <DSN:yes|no> <期望active> <期望崩溃:yes|no> <期望dump:yes|no> <期望上报:yes|no|skip>
run_case() {
    local name="$1" vdir="$2" backend="$3" dsn="$4"
    local exp_active="$5" exp_crash="$6" exp_dump="$7" exp_upload="$8"
    local d="$BUILD_DIR/$vdir" db="$BUILD_DIR/db_${vdir}_${backend}"
    rm -rf "$db"
    local before
    before=$(received_snapshot)
    echo "--- 用例: $name ---"
    local out code
    set +e
    if [ "$dsn" = "yes" ]; then
        out=$(cd "$d" && SENTRY_DSN="$DSN" ./crash_test "$backend" "$db" 2>&1)
        code=$?
    else
        out=$(cd "$d" && env -u SENTRY_DSN ./crash_test "$backend" "$db" 2>&1)
        code=$?
    fi
    set -e
    echo "$out" | head -3
    local active
    active=$(echo "$out" | grep -oP 'active=\K[^ ]*' | head -1)
    # 1. 选型断言
    if [ "$active" = "$exp_active" ]; then
        pass "$name: 选中后端 active='$active' 符合预期"
    else
        fail "$name: 选中后端 active='$active',期望 '$exp_active'"
        return
    fi
    # 2. 崩溃/优雅失败断言
    if [ "$exp_crash" = "yes" ]; then
        if [ "$code" -eq 139 ]; then
            pass "$name: 进程以 SIGSEGV 崩溃(exit 139)"
        else
            fail "$name: 期望崩溃 exit 139,实际 exit $code"
            return
        fi
    else
        if [ "$code" -eq 2 ] && echo "$out" | grep -q "install FAILED"; then
            pass "$name: 未编译后端优雅失败(exit 2,无崩溃)"
        else
            fail "$name: 期望优雅失败 exit 2,实际 exit $code"
            return
        fi
    fi
    # 3. dump 断言(仅崩溃用例)
    if [ "$exp_dump" = "yes" ]; then
        local dumps
        dumps=$(find "$db" -name "*.dmp" 2>/dev/null | head -3)
        if [ -n "$dumps" ]; then
            pass "$name: dump 已生成"
        else
            fail "$name: $db 下无 .dmp"
            return
        fi
    fi
    # 4. 上报断言
    if [ "$exp_upload" = "yes" ]; then
        local got
        if got=$(wait_for_upload 120 "$before"); then
            pass "$name: mock 收到上报 $(basename "$got")"
        else
            fail "$name: 120s 内 mock 未收到上报"
            return
        fi
    elif [ "$exp_upload" = "no" ]; then
        sleep 10
        local got
        got=$(comm -13 <(echo "$before") <(received_snapshot) | head -1)
        if [ -z "$got" ]; then
            pass "$name: 无 DSN 时无上报(仅本地落盘)"
        else
            fail "$name: 不应上报,但 mock 收到了 $got"
            return
        fi
    fi
}

# ===== 矩阵 =====
# 注意:所有 dsn=yes 的用例都必须等上报排空(exp_upload=yes),否则 handler
# 的后台异步上报会落到下一个用例的时间窗里,污染"无上报"断言。
# both 变体
run_case "both+auto=>sentry+上报"        v_both_sentry   auto     yes "sentry"   yes yes yes
run_case "both+sentry显式=>sentry+上报"   v_both_sentry   sentry   yes "sentry"   yes yes yes
run_case "both+crashpad=>crashpad本地"   v_both_crashpad crashpad no  "crashpad" yes yes no
# sentry-only 变体
run_case "sentry-only+auto=>sentry+上报"  v_sentry        auto     yes "sentry"   yes yes yes
run_case "sentry-only+sentry无DSN=>本地" v_sentry        sentry   no  "sentry"   yes yes no
run_case "sentry-only+crashpad=>优雅失败" v_sentry       crashpad no  ""         no  no  skip
# crashpad-only 变体
run_case "crashpad-only+auto=>crashpad"  v_crashpad      auto     no  "crashpad" yes yes skip
run_case "crashpad-only+crashpad"         v_crashpad      crashpad no  "crashpad" yes yes skip
run_case "crashpad-only+sentry=>回退crashpad" v_crashpad sentry   no  "crashpad" yes yes skip
# none 变体
run_case "none+auto=>优雅失败"           v_none          auto     no  ""         no  no  skip

echo "=== 切换矩阵: PASS=$PASS FAIL=$FAIL ==="
[ "$FAIL" -eq 0 ]
