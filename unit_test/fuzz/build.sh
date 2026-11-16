#!/bin/bash
# libFuzzer 目标构建 + 短跑:帧解析入口(get_bits / PB 组装 / add_fields)。
# 用法: ./build.sh [norun] [fuzzer-args...]
#   默认构建后短跑 30 秒(-max_total_time=30 corpus/);
#   传 norun 只构建;其余参数透传给 fuzzer 二进制。
# 工具链:需要带 -fsanitize=fuzzer 的 clang(本机 g++ 不支持该选项)。
#   未装时按 README.md 装 clang(或用 CC/CXX 指向自带 libFuzzer 的 clang),
#   本脚本先做特性探测,不满足就明确报错退出,不静默跳过。
set -e
cd "$(dirname "$0")"
REPO="$(cd ../.. && pwd)"

CXX_BIN="${CXX:-clang++}"
command -v "$CXX_BIN" >/dev/null 2>&1 || {
    echo "未找到 clang++(需带 libFuzzer 的 clang;g++ 不支持 -fsanitize=fuzzer)" >&2
    echo "安装: sudo apt-get install -y clang (Ubuntu noble 自带 clang 18)" >&2
    exit 1
}

# 特性探测:-fsanitize=fuzzer 是否可用(编译+链接一个最小目标)
TMPD="$(mktemp -d "${TMPDIR:-/tmp}/fuzz-probe.XXXXXX")"
trap 'rm -rf "$TMPD"' EXIT
cat >"$TMPD/probe.cpp" <<'EOF'
#include <cstdint>
#include <cstddef>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) { return 0; }
EOF
"$CXX_BIN" -fsanitize=fuzzer,address -std=c++17 "$TMPD/probe.cpp" -o "$TMPD/probe" 2>/dev/null || {
    echo "clang++ 不支持 -fsanitize=fuzzer(缺 libFuzzer 运行时)" >&2
    echo "Ubuntu: sudo apt-get install -y libclang-rt-18-dev" >&2
    exit 1
}

# Qt 路径:与各探针 build.sh 同一套探测(QT_ROOT 优先)
QT="${QT_ROOT:-}"
if [ -z "$QT" ]; then
    for cand in "$HOME/Qt/6.11.3/6.11.3/gcc_64" "$HOME/Qt/6.11.3/gcc_64" "$HOME/qt/6.11.2/gcc_64" "/usr/lib/qt6" "/usr"; do
        if [ -d "$cand/include/QtCore" ]; then QT="$cand"; break; fi
    done
fi
[ -n "$QT" ] || { echo "未找到 Qt,请用 QT_ROOT= 指定" >&2; exit 1; }

mkdir -p build
"$CXX_BIN" -std=c++17 -O1 -g -fno-omit-frame-pointer \
    -fsanitize=fuzzer,address \
    -fPIC \
    -I"$REPO/src" -I"$REPO/src/common" -I"$REPO/src/protocol/common" \
    -I"$QT/include" -I"$QT/include/QtCore" \
    fuzz_frame_parse.cpp "$REPO/src/common/i18n.cpp" \
    -L"$QT/lib" -lQt6Core -Wl,-rpath,"$QT/lib" \
    -o build/fuzz_frame_parse
echo "built build/fuzz_frame_parse"

if [ "${1:-}" = "norun" ]; then exit 0; fi
if [ "$#" -eq 0 ]; then
    # 默认短跑 30 秒(任务要求:几十秒一轮)。libFuzzer 会把新覆盖的输入写回
    # 语料目录,为保持入库语料干净,默认跑在 build/ 下的临时拷贝上;
    # 有价值的新输入人工挑出来再放进 corpus/。
    rm -rf build/corpus-run && mkdir -p build/corpus-run
    cp corpus/* build/corpus-run/
    exec ./build/fuzz_frame_parse -max_total_time=30 build/corpus-run/
else
    exec ./build/fuzz_frame_parse "$@"
fi
