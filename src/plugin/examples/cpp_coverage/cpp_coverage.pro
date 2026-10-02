# cpp_coverage.pro: C++ 信号覆盖插件(动态库,供主程序 QPluginLoader 加载)
QT       += core gui
CONFIG   += c++17 plugin
CONFIG   -= app_bundle

TARGET = cpp_coverage
TEMPLATE = lib

DEFINES += QT_DEPRECATED_WARNINGS QT_PLUGIN

# 插件 API
INCLUDEPATH += ../../plugin_api
DEPENDPATH  += ../../plugin_api

# 主程序公共结构头文件
INCLUDEPATH += ../../../common ../../../protocol ../../../protocol/common
DEPENDPATH  += ../../../common ../../../protocol ../../../protocol/common

HEADERS += \
    ../../plugin_api/iplugin.h \
    ../../plugin_api/iprotocolparserplugin.h \
    ../../plugin_api/igraphicsplugin.h

SOURCES += coverage_plugin.cpp

# 产物旁附带 plugin.json(方便直接当插件目录用)
QMAKE_POST_LINK += $$QMAKE_COPY $$PWD/plugin.json $$OUT_PWD $$escape_expand(\\n\\t)
