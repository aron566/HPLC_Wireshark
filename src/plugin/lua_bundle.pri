# lua_bundle.pri: 内嵌 Lua 5.4.6 的获取 + 构建 + 链接(主程序 app_integration
# 与 bplc-plugin-host 共用)。
#
# 行为(幂等):
#   1. 3rdparty/lua-5.4.6 源码目录缺失时自动获取:有 tarball 则直接解压,
#      无则下载(Windows 10 1803+ 自带 curl/tar,无需 bash;unix 同样 curl + tar)。
#   2. src/liblua.a 缺失时自动编译(win32-g++ 用 mingw32-make)。
#   3. 统一追加 INCLUDEPATH / LIBS。
# 注意:3rdparty/lua-5.4.6* 被 gitignore,不在版本库里,全新 checkout 必须走
# 第 1 步,否则 qmake 的 cd 会报"系统找不到指定的路径"。

_LUA_PWD = $$PWD
_LUA_THIRDPARTY = $$_LUA_PWD/../../3rdparty
LUA_DIR = $$_LUA_THIRDPARTY/lua-5.4.6
LUA_SRC = $$LUA_DIR/src
LUA_LIB = $$LUA_SRC/liblua.a
LUA_TARBALL = $$_LUA_THIRDPARTY/lua-5.4.6.tar.gz
LUA_URL = https://www.lua.org/ftp/lua-5.4.6.tar.gz

# --- 1. 源码目录缺失时获取 ---
# tarball 已进版本库(3rdparty/lua-5.4.6.tar.gz),只需解压;万一缺失才尝试下载
# (Windows 10 1803+ 自带 curl/tar,无需 bash)。
!exists($$LUA_DIR) {
    message("lua: lua-5.4.6 source missing, bootstrapping ...")
    !exists($$LUA_TARBALL): system(curl -sSL -o "$$shell_path($$LUA_TARBALL)" $$LUA_URL)
    !exists($$LUA_TARBALL): error("lua: $$LUA_TARBALL missing and download failed; manually download $$LUA_URL into 3rdparty/ and re-run qmake")
    system(tar -xzf "$$shell_path($$LUA_TARBALL)" -C "$$shell_path($$_LUA_THIRDPARTY)")
    !exists($$LUA_DIR): error("lua: failed to unpack $$LUA_TARBALL")
}

# --- 2. 静态库缺失时编译 ---
!exists($$LUA_LIB) {
    message("lua: building bundled lua-5.4.6 ...")
    win32-g++: LUA_MAKE = mingw32-make
    else: LUA_MAKE = make
    LUA_BUILD = cd "$$shell_path($$LUA_DIR)" && $$LUA_MAKE -C src liblua.a MYCFLAGS="-fPIC"
    system($$LUA_BUILD): message("lua: built OK")
    !exists($$LUA_LIB): error("lua: failed to build $$LUA_LIB")
}

# --- 3. 链接 ---
INCLUDEPATH += $$LUA_SRC
DEPENDPATH += $$LUA_SRC
LIBS += $$LUA_LIB
unix: LIBS += -ldl -lm   # Windows(MinGW) 无 libdl,仅 unix 链
