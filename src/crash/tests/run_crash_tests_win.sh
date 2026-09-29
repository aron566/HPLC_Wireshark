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

# Windows 控制台默认代码页是 cp1252,python 脚本打印中文会 UnicodeEncodeError;
# 强制 UTF-8 输出(GitHub Actions 日志 viewer 认 UTF-8)。
export PYTHONUTF8=1

CRASHPAD_ROOT="$ROOT/3rdparty/install/crashpad"
BUILD_DIR="$ROOT/src/crash/tests/build_win"

if [ ! -f "$CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h" ]; then
    echo "SKIP: crashpad 未构建,请先跑 3rdparty/build_crash_deps.sh crashpad"
    exit 0
fi

command -v g++ >/dev/null 2>&1 || { echo "error: 找不到 g++(MinGW)"; exit 1; }
command -v addr2line >/dev/null 2>&1 || { echo "error: 找不到 addr2line"; exit 1; }

# MinGW 的 bin 目录进 PATH:测试 exe 与它 spawn 的 crashpad_handler.exe 都是原生
# Windows 程序,启动时靠 Windows PATH 找 libstdc++-6.dll 等,Git Bash 的 PATH
# 转换不一定覆盖,显式加上最稳。
MINGW_BIN=$(dirname "$(command -v g++)")
export PATH="$MINGW_BIN:$PATH"
echo "mingw bin: $MINGW_BIN"

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
# 注意:crash_test_crashpad.exe 是原生 Windows 程序(MinGW 编译,不带 MSYS 路径转换),
# 传给它的 db 路径必须是 Windows 格式;直接传 /d/a/... 会被当成当前盘下的 \d\a\...,
# dump 会写到错误位置。
DB_WIN=$(cygpath -m "$DB")
echo "db(win path): $DB_WIN"
echo "=== running crash test (expect Access Violation) ==="
set +e
(cd "$BUILD_DIR" && ./crash_test_crashpad.exe crashpad "$DB_WIN") > "$BUILD_DIR/crash_run.log" 2>&1
code=$?
set -e
cat "$BUILD_DIR/crash_run.log"
echo "[test] exit code: $code"
if ! grep -q "crashing now" "$BUILD_DIR/crash_run.log"; then
    echo "[test] FAIL: 测试程序未能到达崩溃点(可能缺 MinGW DLL 或 install 失败)"
    exit 1
fi
echo "[test] 测试程序已崩溃,等待 handler 写 dump"

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

# addr2line/objdump/nm 都是原生 Windows 程序,路径必须转 Windows 格式
EXE_WIN=$(cygpath -m "$BUILD_DIR/crash_test_crashpad.exe")
DMP_WIN=$(cygpath -m "$DMP")
echo "=== DWARF 诊断 ==="
set +e
echo "--- section 列表(看有没有 .debug_info) ---"
objdump -h "$EXE_WIN" | grep -iE "debug" || echo "!! 没有 .debug_* section,exe 缺 DWARF"
echo "--- nm 查 do_crash 符号地址 ---"
SYM_ADDR=$(nm -n "$EXE_WIN" 2>/dev/null | grep -w "do_crash" | awk '{print $1}' | head -1)
if [ -n "$SYM_ADDR" ]; then
    echo "do_crash 在 nm 中的地址: 0x$SYM_ADDR"
    echo "--- 直接用该地址试 addr2line ---"
    addr2line -e "$EXE_WIN" -f -C "0x$SYM_ADDR"
else
    echo "!! nm 找不到 do_crash 符号"
fi
set -e

echo "=== 符号化(addr2line) ==="
# (EXE_WIN/DMP_WIN 已在 DWARF 诊断段定义)
python3 "$ROOT/scripts/symbolize_win.py" "$DMP_WIN" "$EXE_WIN" | tee "$BUILD_DIR/symbolize.log"
grep -q "do_crash" "$BUILD_DIR/symbolize.log" || {
    echo "[test] FAIL: 符号化结果未定位到 do_crash"
    exit 1
}
echo "[test] PASS: 符号化定位到 do_crash"

echo "ALL DONE (windows)"
