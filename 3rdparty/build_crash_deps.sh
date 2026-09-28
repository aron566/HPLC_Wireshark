#!/bin/bash
# build_crash_deps.sh - 构建崩溃转储第三方依赖(sentry-native / crashpad)
#
# 产物统一安装到 3rdparty/install/:
#   install/sentry/{include,lib,bin}    libsentry.a(+crashpad_client...), sentry.h, crashpad_handler
#   install/crashpad/{include,lib,bin}  libcrashpad_client.a 等, crashpad 头文件, crashpad_handler
#
# 用法:
#   ./build_crash_deps.sh [sentry|crashpad|all]   (默认 all)
#
# 注意: sentry-native 的 crashpad 后端与独立 crashpad 模块共用同一份源码
# (3rdparty/sentry-native/external/crashpad),但分别独立构建,互不耦合。
set -e
cd "$(dirname "$0")"

JOBS=$(nproc 2>/dev/null || echo 4)
SRC_SENTRY="$PWD/sentry-native"
SRC_CRASHPAD="$PWD/sentry-native/external/crashpad"
INSTALL="$PWD/install"

WHAT="${1:-all}"

build_sentry() {
    echo "=== building sentry-native (backend=crashpad, static) ==="
    # SENTRY_TRANSPORT 可被环境变量覆盖(如无 curl 开发头时用 none 先验证捕获链路)
    local transport="${SENTRY_TRANSPORT:-curl}"
    cmake -S "$SRC_SENTRY" -B "$SRC_SENTRY/build" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_SHARED_LIBS=OFF \
        -DSENTRY_BACKEND=crashpad \
        -DSENTRY_TRANSPORT="$transport" \
        -DSENTRY_BUILD_TESTS=OFF \
        -DSENTRY_BUILD_EXAMPLES=OFF
    cmake --build "$SRC_SENTRY/build" --parallel "$JOBS"
    cmake --install "$SRC_SENTRY/build" --prefix "$INSTALL/sentry"
    echo "sentry -> $INSTALL/sentry"
}

build_crashpad() {
    echo "=== building crashpad standalone (client + handler) ==="
    cmake -S "$SRC_CRASHPAD" -B "$SRC_CRASHPAD/build" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCRASHPAD_ENABLE_INSTALL=ON \
        -DCRASHPAD_ENABLE_INSTALL_DEV=ON
    cmake --build "$SRC_CRASHPAD/build" --parallel "$JOBS"
    cmake --install "$SRC_CRASHPAD/build" --prefix "$INSTALL/crashpad"
    echo "crashpad -> $INSTALL/crashpad"
}

case "$WHAT" in
    sentry)   build_sentry ;;
    crashpad) build_crashpad ;;
    all)      build_sentry; build_crashpad ;;
    *) echo "usage: $0 [sentry|crashpad|all]"; exit 1 ;;
esac
echo "DONE"
