# bplc-plugin-host.pro: 插件宿主进程(独立可执行文件,多 runtime)
QT       += core network qml
QT       -= gui
CONFIG   += c++17 console
CONFIG   -= app_bundle

TARGET = bplc-plugin-host
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

# 共享:插件 API + IPC 序列化
INCLUDEPATH += ../plugin_api ../ipc
DEPENDPATH  += ../plugin_api ../ipc

# 主程序公共结构头文件(bplcframe.h / iprotocolparser.h)
INCLUDEPATH += ../../common ../../protocol ../../protocol/common
DEPENDPATH  += ../../common ../../protocol ../../protocol/common

# Lua 5.4 (3rdparty 内嵌,缺失时自动构建)
LUA_SRC = $$PWD/../../../3rdparty/lua-5.4.6/src
LUA_LIB = $$LUA_SRC/liblua.a
!exists($$LUA_LIB) {
    message("lua: building bundled lua-5.4.6 ...")
    LUA_BUILD = cd $$PWD/../../../3rdparty/lua-5.4.6 && make -C src liblua.a MYCFLAGS="-fPIC"
    system($$LUA_BUILD): message("lua: built OK")
    !exists($$LUA_LIB): error("lua: failed to build $$LUA_LIB, run 3rdparty/build_lua.sh manually")
}
INCLUDEPATH += $$LUA_SRC
LIBS += $$LUA_LIB -ldl -lm

HEADERS += \
    plugin_host.h \
    plugin_backend.h \
    native_backend.h \
    js_backend.h \
    lua_backend.h \
    ../plugin_api/iplugin.h \
    ../plugin_api/iprotocolparserplugin.h \
    ../plugin_api/plugin_manifest.h \
    ../ipc/plugin_ipc.h \
    ../ipc/plugin_serialization.h

SOURCES += \
    main.cpp \
    plugin_host.cpp \
    plugin_backend.cpp \
    native_backend.cpp \
    js_backend.cpp \
    lua_backend.cpp \
    ../ipc/plugin_serialization.cpp
