#!/bin/bash
# run_crash_tests_win.sh - Windows(Git Bash + MinGW) crashpad 后端崩溃捕获实测
#
# 前置: 3rdparty/build_crash_deps.sh crashpad   (Windows 上自动用 MinGW 构建)
# 流程:
#   - 编译 crash_test_crashpad.exe(-g -O0,带 DWARF)
#   - 拷贝 crashpad_handler.exe 到同目录(后端按 exe 同目录查找)
#   - 运行,故意触发空指针写(Windows 上是 Access Violation)
#   - 等待 db/reports/*.dmp 生成,校验结构
#   - 用 scripts/symbolize_win.py 做 addr2line 符号化,断言能定位到 do_crash
set -e
cd "$(dirname "$0")/../../.."
ROOT="$PWD"

CRASHPAD_ROOT="$ROOT/3rdparty/install/crashpad"
BUILD_DIR="$ROOT/src/crash/tests/build_win"

if [ ! -f "$CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h" ]; then
    echo "SKIP: crashpad 未构建,请先跑 3rdparty/build_crash_deps.sh crashpad"
    exit 0
fi

command -v g++ >/dev/null 2>&1 || { echo "error: 找不到 g++(MinGW)"; exit 1; }
command -v addr2line >/dev/null 2>&1 || { echo "error: 找不到 addr2line"; exit 1; }

mkdir -p "$BUILD_DIR"

echo "=== compiling crash_test_crashpad.exe ==="
libs=""
for a in "$CRASHPAD_ROOT/lib"/libcrashpad_*.a; do
    [ -f "$a" ] || continue
    name=$(basename "$a" .a); name=${name#lib}
    libs="$libs -l$name"
done
for a in "$CRASHPAD_ROOT/lib"/libmini_chromium*.a; do
    [ -f "$a" ] || continue
    name=$(basename "$a" .a); name=${name#lib}
    libs="$libs -l$name"
done

g++ -std=c++17 -g -O0 -DCRASH_HAVE_CRASHPAD \
    -I"$ROOT/src/crash" \
    -I"$CRASHPAD_ROOT/include" \
    -I"$CRASHPAD_ROOT/include/crashpad" \
    -I"$CRASHPAD_ROOT/include/crashpad/mini_chromium" \
    "$ROOT/src/crash/crash_handler.cpp" \
    "$ROOT/src/crash/crash_util.cpp" \
    "$ROOT/src/crash/backend_stub.cpp" \
    "$ROOT/src/crash/backend_sentry.cpp" \
    "$ROOT/src/crash/backend_crashpad.cpp" \
    "$ROOT/src/crash/tests/crash_test.cpp" \
    -L"$CRASHPAD_ROOT/lib" $libs \
    -lwinhttp -ldbghelp -lversion -lws2_32 \
    -o "$BUILD_DIR/crash_test_crashpad.exe"
echo "built: $BUILD_DIR/crash_test_crashpad.exe"

# handler 二进制放测试程序同目录(后端按 exe 同目录查找)
cp "$CRASHPAD_ROOT/bin/crashpad_handler.exe" "$BUILD_DIR/"
echo "handler: $BUILD_DIR/crashpad_handler.exe"

DB="$BUILD_DIR/db_crashpad"
rm -rf "$DB"
echo "=== running crash test (expect Access Violation) ==="
set +e
(cd "$BUILD_DIR" && ./crash_test_crashpad.exe crashpad "$DB")
code=$?
set -e
echo "[test] exit code: $code (crashed as expected)"

echo "=== waiting for .dmp ==="
DMP=""
for i in $(seq 1 15); do
    DMP=$(find "$DB" -name "*.dmp" 2>/dev/null | head -1)
    if [ -n "$DMP" ]; then break; fi
    sleep 1
done
if [ -z "$DMP" ]; then
    echo "[test] FAIL: no .dmp found in $DB"
    find "$DB" -type f 2>/dev/null | head -10
    exit 1
fi
echo "[test] PASS: dump generated: $DMP"
ls -la "$DMP"

echo "=== 结构校验 ==="
python3 "$ROOT/3rdparty/check_minidump.py" "$DMP"

echo "=== 符号化(addr2line) ==="
python3 "$ROOT/scripts/symbolize_win.py" "$DMP" "$BUILD_DIR/crash_test_crashpad.exe" | tee "$BUILD_DIR/symbolize.log"
grep -q "do_crash" "$BUILD_DIR/symbolize.log" || {
    echo "[test] FAIL: 符号化结果未定位到 do_crash"
    exit 1
}
echo "[test] PASS: 符号化定位到 do_crash"

echo "ALL DONE (windows)"
