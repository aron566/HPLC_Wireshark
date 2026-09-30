# plugin.pri: 插件系统(主程序侧:IPC 序列化 + PluginManager + 代理)
# bplc-plugin-host 与示例插件各有独立 .pro,不在此

QT += core network

INCLUDEPATH += $$PWD/plugin_api $$PWD/ipc $$PWD/manager
DEPENDPATH  += $$PWD/plugin_api $$PWD/ipc $$PWD/manager

HEADERS += \
    $$PWD/plugin_api/iplugin.h \
    $$PWD/plugin_api/iprotocolparserplugin.h \
    $$PWD/plugin_api/igraphicsplugin.h \
    $$PWD/plugin_api/plugin_manifest.h \
    $$PWD/ipc/plugin_ipc.h \
    $$PWD/ipc/plugin_serialization.h \
    $$PWD/manager/plugin_manager.h \
    $$PWD/manager/plugin_parser_proxy.h \
    $$PWD/manager/plugin_graphics_view.h

SOURCES += \
    $$PWD/ipc/plugin_serialization.cpp \
    $$PWD/manager/plugin_manager.cpp \
    $$PWD/manager/plugin_parser_proxy.cpp \
    $$PWD/manager/plugin_graphics_view.cpp
