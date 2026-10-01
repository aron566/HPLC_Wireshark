# app_integration.pri: 主界面内置插件引擎(进程内 JS/Lua/Native 后端 + 功能面板)
# 与 PluginManager(多进程 IPC)互补:本模块让主界面直接调用插件、
# 展示各插件功能界面(拓扑/回放/诊断/报表),后端跑在独立工作线程,
# GUI 线程只做展示,不阻塞界面。

QT += qml

INCLUDEPATH += $$PWD/../plugin_api $$PWD/../host $$PWD \
               $$PWD/../../common $$PWD/../../protocol
DEPENDPATH  += $$PWD/../plugin_api $$PWD/../host $$PWD \
               $$PWD/../../common $$PWD/../../protocol

# Lua 5.4 (3rdparty 内嵌,缺失时自动构建;与 bplc-plugin-host.pro 同策略)
LUA_SRC = $$PWD/../../../3rdparty/lua-5.4.6/src
LUA_LIB = $$LUA_SRC/liblua.a
!exists($$LUA_LIB) {
    message("app_integration: building bundled lua-5.4.6 ...")
    win32-g++: LUA_MAKE = mingw32-make
    else: LUA_MAKE = make
    LUA_BUILD = cd $$PWD/../../../3rdparty/lua-5.4.6 && $$LUA_MAKE -C src liblua.a MYCFLAGS="-fPIC"
    system($$LUA_BUILD): message("app_integration: lua built OK")
    !exists($$LUA_LIB): error("app_integration: failed to build $$LUA_LIB")
}
INCLUDEPATH += $$LUA_SRC
LIBS += $$LUA_LIB
unix: LIBS += -ldl -lm   # Windows(MinGW) 无 libdl,仅 unix 链

HEADERS += \
    $$PWD/local_plugin_engine.h \
    $$PWD/plugin_panels.h \
    $$PWD/../host/plugin_backend.h \
    $$PWD/../host/js_backend.h \
    $$PWD/../host/lua_backend.h \
    $$PWD/../host/native_backend.h \
    $$PWD/../host/script_painter.h

SOURCES += \
    $$PWD/local_plugin_engine.cpp \
    $$PWD/plugin_panels.cpp \
    $$PWD/../host/plugin_backend.cpp \
    $$PWD/../host/js_backend.cpp \
    $$PWD/../host/lua_backend.cpp \
    $$PWD/../host/native_backend.cpp \
    $$PWD/../host/script_painter.cpp
