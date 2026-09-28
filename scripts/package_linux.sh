#!/usr/bin/env bash
# Linux 打包脚本:构建 → 收集 Qt 运行时 → tar.gz 安装包
# 用法: bash scripts/package_linux.sh [版本号,默认 1.0.1]
#
# 产物: dist/BPLC_STA_Monitor_v<VER>_linux_x86_64.tar.gz
# 解压即用,含 crashpad_handler(崩溃转储必需,放程序同目录)。
#
# 依赖: qmake(Qt6) / make(需联网构建 crashpad 依赖)
set -e
cd "$(dirname "$0")/.."
VER="${1:-1.0.1}"
ARCH="x86_64"

echo "== 0/4 检查 crashpad 依赖"
if [ ! -f 3rdparty/install/crashpad/bin/crashpad_handler ]; then
    echo "未找到 3rdparty/install/crashpad,先构建:"
    echo "  bash 3rdparty/build_crash_deps.sh crashpad"
    bash 3rdparty/build_crash_deps.sh crashpad
fi

echo "== 1/4 qmake + 构建(默认 crashpad 后端)"
rm -rf build_linux
mkdir -p build_linux
cd build_linux
qmake ../BPLC_STA_Monitor.pro
make -j"$(nproc)"
cd ..

echo "== 2/4 ldd 收集运行时库"
rm -rf AppDir
mkdir -p AppDir/usr/bin AppDir/usr/lib
cp build_linux/BPLC_STA_Monitor AppDir/usr/bin/
# crashpad_handler 必须与主程序同目录(后端按 exe 目录查找)
cp 3rdparty/install/crashpad/bin/crashpad_handler AppDir/usr/bin/
chmod +x AppDir/usr/bin/crashpad_handler
# 默认配置文件模板(用户首次运行可复制为 config.ini)
cp config.ini AppDir/usr/bin/config.ini.example 2>/dev/null || true
# 附 Wireshark 解析插件
mkdir -p AppDir/usr/share/bplc_sta_monitor/wireshark_support_plugins
cp -r wireshark_support_plugins/. AppDir/usr/share/bplc_sta_monitor/wireshark_support_plugins/
rm -f AppDir/usr/share/bplc_sta_monitor/wireshark_support_plugins/*.c
# ldd 收集所有 .so 依赖(含 Qt6)
ldd AppDir/usr/bin/BPLC_STA_Monitor | grep -o '/[^ ]*\.so[^ ]*' | sort -u | while read -r lib; do
    cp -L "$lib" AppDir/usr/lib/ 2>/dev/null || true
done
# Qt 插件(xcb 平台插件等)
QT_PLUGINS="$(qmake -query QT_INSTALL_PLUGINS 2>/dev/null)"
if [ -n "$QT_PLUGINS" ] && [ -d "$QT_PLUGINS/platforms" ]; then
    mkdir -p AppDir/usr/lib/qt6/plugins
    cp -r "$QT_PLUGINS/platforms" AppDir/usr/lib/qt6/plugins/
    cp -r "$QT_PLUGINS/xcbglintegrations" AppDir/usr/lib/qt6/plugins/ 2>/dev/null || true
fi
echo "收集到 $(ls AppDir/usr/lib/*.so* 2>/dev/null | wc -l) 个库"

echo "== 3/4 启动脚本"
cat > AppDir/usr/bin/run.sh <<'EOF'
#!/usr/bin/env bash
# 解压即用启动器:设置库路径后启动主程序
HERE="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$HERE/../lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$HERE/../lib/qt6/plugins"
exec "$HERE/BPLC_STA_Monitor" "$@"
EOF
chmod +x AppDir/usr/bin/run.sh

echo "== 4/4 打包 tar.gz"
mkdir -p dist
PKG="dist/BPLC_STA_Monitor_v${VER}_linux_${ARCH}.tar.gz"
tar -czf "$PKG" -C AppDir .
ls -la "$PKG"
echo "DONE: $PKG"
