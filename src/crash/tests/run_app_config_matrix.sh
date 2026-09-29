#!/bin/bash
# run_app_config_matrix.sh - 正式 App 编译组合矩阵(Linux CI)
#
# 验证 BPLC_STA_Monitor.pro 的 qmake CONFIG 组合:
#   default      无附加 CONFIG(.pro 默认 CONFIG+=crash_crashpad)
#   sentry-only  CONFIG-=crash_crashpad CONFIG+=crash_sentry
#   both         CONFIG+=crash_sentry
#   none         CONFIG-=crash_crashpad
#
# 每种组合断言三件事:
#   1. qmake + make 构建成功
#   2. offscreen 启动 15 秒无启动期崩溃、无 dump
#   3. --self-crash-test: 含后端的组合 exit 139 且 db 下出现有效 MDMP dump;
#      none 组合 exit 139 但无 dump(后端未编译,优雅无捕获)
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

# run_combo <组合名> <qmake附加参数> <崩溃测试后端> <期望dump:yes|no>
run_combo() {
    local name="$1" qargs="$2" tbackend="$3" exp_dump="$4"
    local bdir="$MATRIX_DIR/build_$name" stage="$MATRIX_DIR/stage_$name"
    echo "===== 组合: $name (qmake $qargs) ====="
    rm -rf "$bdir" "$stage"; mkdir -p "$bdir" "$stage"

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

    # 2. 布署:主程序 + 对应 handler + config.ini(db 用绝对路径)
    cp "$app" "$stage/"
    case "$tbackend" in
        sentry) cp "$SENTRY_ROOT/bin/crashpad_handler" "$stage/" ;;
        crashpad) cp "$CRASHPAD_ROOT/bin/crashpad_handler" "$stage/" ;;
    esac
    chmod +x "$stage/crashpad_handler" 2>/dev/null || true
    cat > "$stage/config.ini" <<EOF
[crash]
backend=$tbackend
db_path=$stage/crashdb
EOF

    # 3. 启动冒烟:15 秒无启动期崩溃、无 dump
    set +e
    (cd "$stage" && timeout 15 ./BPLC_STA_Monitor -platform offscreen >/tmp/smoke_$name.log 2>&1)
    local scode=$?
    set -e
    if [ "$scode" -ne 124 ]; then
        fail "$name: 启动冒烟异常 exit=$scode(期望 124=被 timeout 正常结束,说明进程存活)"
        tail -15 /tmp/smoke_$name.log || true
        return
    fi
    if [ -n "$(find "$stage" -name "*.dmp" 2>/dev/null | head -1)" ]; then
        fail "$name: 启动期产生崩溃 dump"
        return
    fi
    pass "$name: offscreen 启动 15s 无崩溃、无 dump"

    # 4. 崩溃捕获:确定性崩溃,断言 exit 139 与 dump 有无
    set +e
    (cd "$stage" && timeout 60 ./BPLC_STA_Monitor -platform offscreen --self-crash-test >/tmp/crashtest_$name.log 2>&1)
    local ccode=$?
    set -e
    if [ "$ccode" -ne 139 ]; then
        fail "$name: --self-crash-test 未以 SIGSEGV 退出, exit=$ccode"
        tail -15 /tmp/crashtest_$name.log || true
        return
    fi
    local dmp=""
    for _ in $(seq 1 60); do
        dmp=$(find "$stage" -name "*.dmp" 2>/dev/null | head -1)
        [ -n "$dmp" ] && break
        sleep 1
    done
    if [ "$exp_dump" = "yes" ]; then
        if [ -n "$dmp" ] && [ "$(head -c 4 "$dmp")" = "MDMP" ]; then
            pass "$name: 崩溃被捕获,dump 落盘 ($dmp)"
        else
            fail "$name: 60s 内未见有效 MDMP dump"
            return
        fi
    else
        if [ -z "$dmp" ]; then
            pass "$name: 无后端时崩溃无捕获、无 dump(符合预期)"
        else
            fail "$name: 无后端却产生了 dump: $dmp"
            return
        fi
    fi
}

run_combo default     ""                                            crashpad yes
run_combo sentry-only "CONFIG-=crash_crashpad CONFIG+=crash_sentry" sentry   yes
run_combo both        "CONFIG+=crash_sentry"                        sentry   yes
run_combo none        "CONFIG-=crash_crashpad"                       auto     no

echo "=== App 编译组合矩阵: PASS=$PASS FAIL=$FAIL ==="
[ "$FAIL" -eq 0 ]
