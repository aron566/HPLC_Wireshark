#!/bin/bash
# run_app_config_matrix.sh - 正式 App 编译组合矩阵(Linux CI)
#
# 验证 BPLC_STA_Monitor.pro 的 qmake CONFIG 组合:
#   default      无附加 CONFIG(.pro 默认 CONFIG+=crash_crashpad) → 测 crashpad
#   sentry-only  CONFIG+=crash_no_default CONFIG+=crash_sentry   → 测 sentry
#   both         CONFIG+=crash_sentry                            → 测 sentry + crashpad
#   none         CONFIG+=crash_no_default                        → 无后端
#
# 注意:不要用 CONFIG-=crash_crashpad 来关默认。qmake 命令行的 -= 在 .pro
# 求值前处理,删不掉 .pro 里默认加上的开关,crash.pri 的 contains() 照样看到它
# (2026-09-30 实测).关闭默认必须用 CONFIG+=crash_no_default(见 .pro 注释)。
#
# 每种组合断言:
#   1. qmake + make 构建成功
#   2. offscreen 启动 15 秒无启动期崩溃、无 dump
#   3. 每个编译进的后端: --self-crash-test → exit 139 → dump 落盘(MDMP)
#      → 符号化 → 崩溃线程栈定位到 crash_selftest_trigger 且带 main.cpp 行号
#      (只验 dump 结构不算通过,必须证明可定位到崩溃处)
#   4. none 组合: exit 139 但无 dump(后端未编译,优雅无捕获)
#
# 运行时后端经 exe 同目录 config.ini [crash] backend 指定,db 用绝对路径,
# 与开发机既有配置隔离。
#
# 前置: Qt6 qmake 在 PATH; 3rdparty/build_crash_deps.sh all 已跑过
set -e
cd "$(dirname "$0")/../../.."
ROOT="$PWD"

SENTRY_ROOT="$ROOT/3rdparty/install/sentry"
CRASHPAD_ROOT="$ROOT/3rdparty/install/crashpad"
MATRIX_DIR="$ROOT/build_matrix"

need() { [ -e "$1" ] || { echo "[FAIL] 缺少: $1"; exit 1; }; }
need "$SENTRY_ROOT/include/sentry.h"
need "$CRASHPAD_ROOT/bin/crashpad_handler"
need "$SENTRY_ROOT/bin/crashpad_handler"
command -v qmake >/dev/null || { echo "[FAIL] qmake 不在 PATH"; exit 1; }

# Qt 运行环境(runner 上 install-qt-action 已装好 Qt 6.10.1)
QT_LIB_DIR="$(qmake -query QT_INSTALL_LIBS)"
QT_PLUGIN_DIR="$(qmake -query QT_INSTALL_PLUGINS)"
export LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$QT_PLUGIN_DIR"
export QT_QPA_PLATFORM=offscreen

PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); echo "[PASS] $1"; }
fail() { FAIL=$((FAIL+1)); echo "[FAIL] $1"; }

# test_backend <组合名> <后端> <已构建的主程序路径>
# 崩溃 → dump → 符号化 → 断言定位到崩溃处
test_backend() {
    local name="$1" backend="$2" app="$3"
    local stage="$MATRIX_DIR/stage_${name}_${backend}"
    local tag="$name/$backend"
    rm -rf "$stage"; mkdir -p "$stage"

    cp "$app" "$stage/"
    if [ "$backend" = "sentry" ]; then
        cp "$SENTRY_ROOT/bin/crashpad_handler" "$stage/"
    else
        cp "$CRASHPAD_ROOT/bin/crashpad_handler" "$stage/"
    fi
    chmod +x "$stage/crashpad_handler"
    cat > "$stage/config.ini" <<EOF
[crash]
backend=$backend
db_path=$stage/crashdb
EOF

    # 3a. 确定性崩溃,进程必须以 SIGSEGV 退出
    set +e
    (cd "$stage" && timeout 60 ./BPLC_STA_Monitor -platform offscreen --self-crash-test >/tmp/crashtest_${name}_${backend}.log 2>&1)
    local ccode=$?
    set -e
    if [ "$ccode" -ne 139 ]; then
        fail "$tag: --self-crash-test 未以 SIGSEGV 退出, exit=$ccode"
        tail -15 "/tmp/crashtest_${name}_${backend}.log" || true
        return
    fi
    pass "$tag: 进程以 SIGSEGV 崩溃(exit 139)"

    # 3b. dump 落盘且格式有效
    local dmp=""
    for _ in $(seq 1 60); do
        dmp=$(find "$stage" -name "*.dmp" 2>/dev/null | head -1)
        [ -n "$dmp" ] && break
        sleep 1
    done
    if [ -z "$dmp" ] || [ "$(head -c 4 "$dmp")" != "MDMP" ]; then
        fail "$tag: 60s 内未见有效 MDMP dump"
        return
    fi
    pass "$tag: dump 落盘且 MDMP 魔数有效 ($dmp)"

    # 3c. 符号化(用本次构建产物的调试信息)
    local symdir="$MATRIX_DIR/sym_${name}_${backend}"
    local stack="/tmp/stack_${name}_${backend}.txt"
    rm -rf "$symdir"
    if ! bash "$ROOT/scripts/symbolize.sh" "$dmp" "$app" "$symdir" >"$stack" 2>/tmp/symerr_${name}_${backend}.log; then
        fail "$tag: 符号化失败"
        tail -10 "/tmp/symerr_${name}_${backend}.log" || true
        return
    fi
    grep -q "SIGSEGV" "$stack" || { fail "$tag: 栈回溯中未见 SIGSEGV 崩溃原因"; return; }
    pass "$tag: dump 符号化完成,崩溃原因为 SIGSEGV"

    # 3d. 定位到崩溃处:崩溃线程栈出现 crash_selftest_trigger 且带 main.cpp 行号
    local crash_sec frame
    crash_sec=$(awk '/Thread [0-9]+ \(crashed\)/{f=1} f{print} f&&/^ *$/{exit}' "$stack")
    if ! echo "$crash_sec" | grep -q "crash_selftest_trigger"; then
        fail "$tag: 崩溃线程栈中找不到 crash_selftest_trigger"
        echo "$crash_sec" | head -12 || true
        return
    fi
    frame=$(echo "$crash_sec" | grep "crash_selftest_trigger" | head -1)
    echo "崩溃帧: $frame"
    if ! echo "$frame" | grep -Eq "\[[^]]+\.(cpp|c|cc|cxx) : [0-9]+"; then
        fail "$tag: 崩溃帧无源码行号(只有偏移)"
        return
    fi
    pass "$tag: 定位到崩溃处: $frame"
}

# run_combo <组合名> <qmake附加参数> <后端列表:空格分隔,空=无后端>
run_combo() {
    local name="$1" qargs="$2" backends="$3"
    local bdir="$MATRIX_DIR/build_$name"
    echo "===== 组合: $name (qmake $qargs) ====="
    rm -rf "$bdir"; mkdir -p "$bdir"

    # 1. 构建
    # shellcheck disable=SC2086
    if ! (cd "$bdir" && qmake "$ROOT/BPLC_STA_Monitor.pro" $qargs >/tmp/qmake_$name.log 2>&1 \
            && make -j"$(nproc)" >/tmp/make_$name.log 2>&1); then
        fail "$name: 构建失败(见 /tmp/make_$name.log)"
        tail -20 /tmp/make_$name.log || true
        return
    fi
    pass "$name: qmake+make 构建成功"

    local app="$bdir/BPLC_STA_Monitor"
    [ -x "$app" ] || { fail "$name: 主程序未生成"; return; }

    # 2. 启动冒烟:15 秒无启动期崩溃、无 dump(用 auto 后端,不干扰后测)
    local smokestage="$MATRIX_DIR/stage_${name}_smoke"
    rm -rf "$smokestage"; mkdir -p "$smokestage"
    cp "$app" "$smokestage/"
    cp "$CRASHPAD_ROOT/bin/crashpad_handler" "$smokestage/" 2>/dev/null || true
    chmod +x "$smokestage/crashpad_handler" 2>/dev/null || true
    printf '[crash]\nbackend=auto\ndb_path=%s/crashdb\n' "$smokestage" > "$smokestage/config.ini"
    set +e
    (cd "$smokestage" && timeout 15 ./BPLC_STA_Monitor -platform offscreen >/tmp/smoke_$name.log 2>&1)
    local scode=$?
    set -e
    if [ "$scode" -ne 124 ]; then
        fail "$name: 启动冒烟异常 exit=$scode(期望 124=被 timeout 正常结束,说明进程存活)"
        tail -15 /tmp/smoke_$name.log || true
        return
    fi
    if [ -n "$(find "$smokestage" -name "*.dmp" 2>/dev/null | head -1)" ]; then
        fail "$name: 启动期产生崩溃 dump"
        return
    fi
    pass "$name: offscreen 启动 15s 无崩溃、无 dump"

    # 3. 无后端组合:崩溃但无捕获、无 dump 即符合预期
    if [ -z "$backends" ]; then
        set +e
        (cd "$smokestage" && timeout 60 ./BPLC_STA_Monitor -platform offscreen --self-crash-test >/tmp/crashtest_${name}_none.log 2>&1)
        local ccode=$?
        set -e
        [ "$ccode" -eq 139 ] || { fail "$name: --self-crash-test 未以 SIGSEGV 退出, exit=$ccode"; return; }
        sleep 10
        if [ -z "$(find "$smokestage" -name "*.dmp" 2>/dev/null | head -1)" ]; then
            pass "$name: 无后端时崩溃无捕获、无 dump(符合预期)"
        else
            fail "$name: 无后端却产生了 dump"
        fi
        return
    fi

    # 4. 每个后端:崩溃 → dump → 符号化 → 定位
    local b
    for b in $backends; do
        test_backend "$name" "$b" "$app"
    done
}

run_combo default     ""                                            "crashpad"
run_combo sentry-only "CONFIG+=crash_no_default CONFIG+=crash_sentry" "sentry"
run_combo both        "CONFIG+=crash_sentry"                         "sentry crashpad"
run_combo none        "CONFIG+=crash_no_default"                       ""

echo "=== App 编译组合矩阵: PASS=$PASS FAIL=$FAIL ==="
[ "$FAIL" -eq 0 ]
