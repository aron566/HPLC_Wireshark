# app_integration.pri: 主界面内置插件引擎(进程内 JS/Lua/Native 后端 + 功能面板)
# 与 PluginManager(多进程 IPC)互补:本模块让主界面直接调用插件、
# 展示各插件功能界面(拓扑/回放/诊断/报表),后端跑在独立工作线程,
# GUI 线程只做展示,不阻塞界面。

QT += qml

INCLUDEPATH += $$PWD/../plugin_api $$PWD/../host $$PWD \
               $$PWD/../../common $$PWD/../../protocol
DEPENDPATH  += $$PWD/../plugin_api $$PWD/../host $$PWD \
               $$PWD/../../common $$PWD/../../protocol

# Lua 5.4 (3rdparty 内嵌,缺失时自动获取源码并构建;见 lua_bundle.pri)
include(../lua_bundle.pri)

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
