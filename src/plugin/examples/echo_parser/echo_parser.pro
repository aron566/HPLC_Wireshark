# echo_parser.pro: 示例插件(动态库,供 bplc-plugin-host 加载)
QT       += core
QT       -= gui
CONFIG   += c++17 plugin
CONFIG   -= app_bundle

TARGET = echo_parser
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
    ../../plugin_api/iprotocolparserplugin.h

SOURCES += echo_parser.cpp

# 产物旁附带 plugin.json(方便直接当插件目录用)
QMAKE_POST_LINK += $$QMAKE_COPY $$PWD/plugin.json $$OUT_PWD $$escape_expand(\\n\\t)
