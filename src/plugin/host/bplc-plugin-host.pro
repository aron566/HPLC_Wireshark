# bplc-plugin-host.pro: 插件宿主进程(独立可执行文件,多 runtime)
QT       += core network qml gui
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

# Lua 5.4 (3rdparty 内嵌,缺失时自动获取源码并构建;见 lua_bundle.pri)
include(../lua_bundle.pri)

HEADERS += \
    plugin_host.h \
    plugin_backend.h \
    native_backend.h \
    js_backend.h \
    lua_backend.h \
    script_painter.h \
    ../plugin_api/iplugin.h \
    ../plugin_api/iprotocolparserplugin.h \
    ../plugin_api/igraphicsplugin.h \
    ../plugin_api/plugin_manifest.h \
    ../ipc/plugin_ipc.h \
    ../ipc/plugin_serialization.h

# 节点图标(与主程序 TopoWindow 同源),供 ScriptPainter::draw_icon
RESOURCES += plugin_icons.qrc

SOURCES += \
    main.cpp \
    plugin_host.cpp \
    plugin_backend.cpp \
    native_backend.cpp \
    js_backend.cpp \
    lua_backend.cpp \
    script_painter.cpp \
    ../ipc/plugin_serialization.cpp
