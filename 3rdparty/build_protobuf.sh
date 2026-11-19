#!/bin/bash
# build_protobuf.sh - 构建 vendored protobuf v36.2 + abseil-cpp 20250512.1（静态）
#
# 产物统一安装到 3rdparty/install/:
#   install/absl/{include,lib}        libabsl_*.a(88 个), abslConfig.cmake
#   install/protobuf/{include,lib,bin} libprotobuf.a, libprotoc.a, libupb.a,
#                                      libutf8_validity.a, protoc(+protoc-36.2.0)
# 构建树放在 3rdparty/build/（已加入 .gitignore，不入库；构建完可删）。
#
# 子库来源（钉死 commit，不自动拉最新）:
#   3rdparty/protobuf-src  protocolbuffers/protobuf v36.2
#                          commit 2c74169b34066ceb8ddb6b882fcb3fb32d737a55，跟踪 36.x 分支
#   3rdparty/abseil-cpp   abseil/abseil-cpp 20250512.1
#                          commit 76bb24329e8bf5f39704eb10d21b9a80befa7c81
# 注意：3rdparty/protobuf/ 仍是父仓的集成目录（protobuf.pri + README.md），
# 上游源码子库挂在 3rdparty/protobuf-src/，避免与父仓跟踪文件同路径冲突。
#
# 用法: bash 3rdparty/build_protobuf.sh
#   幂等：哨兵齐全时直接跳过。qmake 期由 protobuf.pri 在缺失时同步调用。
#   环境变量（仿 build_crash_deps.sh）: CMAKE_BIN / MAKE_BIN / JOBS。
#   Windows: Git Bash + -G "MinGW Makefiles"，cmake/make 经 CMAKE_BIN/MAKE_BIN 传绝对路径。
#   绝不联网拉依赖：protobuf 侧传 -Dprotobuf_LOCAL_DEPENDENCIES_ONLY=ON，
#   找不到本地 absl 时 CMake 直接 FATAL_ERROR（见 protobuf-src/cmake/abseil-cpp.cmake）。
set -e
cd "$(dirname "$0")"

# qmake 自动构建经 CMAKE_BIN/MAKE_BIN 传入 cmake/make 绝对路径；Windows 下
# qmake 扫 PATH 得到的是反斜杠路径(C:\...)，在 bash 单引号里会当成字面文件名
# 而失效，统一转成正斜杠(MSYS bash 可直接执行 C:/... 形式)。
_to_bash_path() { local p="$1"; printf '%s' "${p//\\//}"; }
CMAKE_BIN="$(_to_bash_path "${CMAKE_BIN:-cmake}")"
MAKE_BIN="$(_to_bash_path "${MAKE_BIN:-}")"

SRC_PB="$PWD/protobuf-src"
SRC_ABSL="$PWD/abseil-cpp"
INSTALL="$PWD/install"
BUILD_DIR="$PWD/build"

# ---- 平台检测 ----
ON_WINDOWS=0
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) ON_WINDOWS=1 ;;
esac

# 原生 Windows 路径(C:/...)供 CMake(原生 Win32 构建)使用；
# Git Bash 的 /c/... 写法 CMake 不认。
to_win_path() {
    if [ "$ON_WINDOWS" = "1" ]; then
        cygpath -m "$1"
    else
        printf '%s' "$1"
    fi
}

# ---- 并行度：内存够才敢多核 ----
# protobuf 单个编译单元峰值内存大；本机可用内存仅约 1 GB 时 -j2 已是上限，
# 内存 >= 8 GB 的机器（CI runner）才用全部核心。可用 JOBS 环境变量覆盖。
default_jobs() {
    local mem_kb
    mem_kb=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)
    if [ "$mem_kb" -ge 8388608 ] 2>/dev/null; then
        nproc 2>/dev/null || echo 4
    else
        echo 2
    fi
}
JOBS="${JOBS:-$(default_jobs)}"

# ---- 源码完整性判定 ----
# 子库未初始化（全新 clone 后忘开 submodule）时直接报错指引，不让 CMake
# 报看不懂的缺文件错误。
for f in "$SRC_PB/CMakeLists.txt" "$SRC_ABSL/CMakeLists.txt"; do
    if [ ! -f "$f" ]; then
        echo "error: missing $f" >&2
        echo "error: vendored protobuf source not checked out; run:" >&2
        echo "error:   git submodule update --init --depth 1 3rdparty/protobuf-src 3rdparty/abseil-cpp" >&2
        exit 1
    fi
done

# ---- 哨兵：absl 的 CMake 配置 + protobuf 的头文件/静态库/protoc ----
absl_ready() {
    [ -f "$INSTALL/absl/lib/cmake/absl/abslConfig.cmake" ]
}
protobuf_ready() {
    [ -f "$INSTALL/protobuf/include/google/protobuf/port_def.inc" ] \
        && [ -f "$INSTALL/protobuf/lib/libprotobuf.a" ] \
        && [ -x "$INSTALL/protobuf/bin/protoc" ]
}

if absl_ready && protobuf_ready; then
    echo "=== vendored protobuf already built, skipping ==="
    exit 0
fi

command -v "$CMAKE_BIN" >/dev/null 2>&1 || {
    echo "error: cmake not found ($CMAKE_BIN); install CMake or set CMAKE_BIN" >&2
    exit 1
}

# ---- 1. absl（先编，protobuf 靠 CMAKE_PREFIX_PATH 找到它）----
if ! absl_ready; then
    echo "=== building abseil-cpp 20250512.1 (static) ==="
    gen_args=()
    if [ "$ON_WINDOWS" = "1" ]; then
        gen_args+=(-G "MinGW Makefiles")
    fi
    if [ -n "$MAKE_BIN" ]; then
        gen_args+=(-DCMAKE_MAKE_PROGRAM="$MAKE_BIN")
    fi
    "$CMAKE_BIN" -S "$(to_win_path "$SRC_ABSL")" -B "$(to_win_path "$BUILD_DIR/absl")" \
        "${gen_args[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_INSTALL_PREFIX="$(to_win_path "$INSTALL/absl")" \
        -DABSL_BUILD_TESTING=OFF \
        -DABSL_ENABLE_INSTALL=ON
    "$CMAKE_BIN" --build "$(to_win_path "$BUILD_DIR/absl")" --parallel "$JOBS"
    "$CMAKE_BIN" --install "$(to_win_path "$BUILD_DIR/absl")"
    echo "absl -> $INSTALL/absl"
fi

# ---- 2. protobuf（libprotobuf + libprotoc + protoc；不上测试/样例/一致性测试）----
if ! protobuf_ready; then
    echo "=== building protobuf v36.2 (static, libprotobuf + protoc) ==="
    gen_args=()
    if [ "$ON_WINDOWS" = "1" ]; then
        gen_args+=(-G "MinGW Makefiles")
    fi
    if [ -n "$MAKE_BIN" ]; then
        gen_args+=(-DCMAKE_MAKE_PROGRAM="$MAKE_BIN")
    fi
    "$CMAKE_BIN" -S "$(to_win_path "$SRC_PB")" -B "$(to_win_path "$BUILD_DIR/protobuf")" \
        "${gen_args[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_PREFIX_PATH="$(to_win_path "$INSTALL/absl")" \
        -Dprotobuf_LOCAL_DEPENDENCIES_ONLY=ON \
        -Dprotobuf_BUILD_TESTS=OFF \
        -Dprotobuf_BUILD_CONFORMANCE=OFF \
        -Dprotobuf_BUILD_EXAMPLES=OFF \
        -Dprotobuf_WITH_ZLIB=OFF \
        -Dprotobuf_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX="$(to_win_path "$INSTALL/protobuf")"
    "$CMAKE_BIN" --build "$(to_win_path "$BUILD_DIR/protobuf")" --parallel "$JOBS"
    "$CMAKE_BIN" --install "$(to_win_path "$BUILD_DIR/protobuf")"
    echo "protobuf -> $INSTALL/protobuf"
fi

# ---- 哨兵复验：四个文件缺一即失败（仿 crash.pri 的“有牙齿”做法）----
for f in "$INSTALL/absl/lib/cmake/absl/abslConfig.cmake" \
         "$INSTALL/protobuf/include/google/protobuf/port_def.inc" \
         "$INSTALL/protobuf/lib/libprotobuf.a" \
         "$INSTALL/protobuf/bin/protoc"; do
    [ -e "$f" ] || { echo "error: missing $f after build" >&2; exit 1; }
done
echo "=== vendored protobuf ready: $INSTALL/{protobuf,absl} ==="
