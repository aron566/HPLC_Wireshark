# nw2021_parser_plugin.pro: 南网 NW_2021 协议解析器插件(动态库,供 bplc-plugin-host 加载)
# 纯解析器插件(QtCore),复用主程序 nw_2021 解析器源码,不依赖 QtWidgets。
QT       += core
QT       -= gui
CONFIG   += c++17 plugin
CONFIG   -= app_bundle

TARGET = nw2021_parser
TEMPLATE = lib

DEFINES += QT_DEPRECATED_WARNINGS QT_PLUGIN

# 插件 API
INCLUDEPATH += ../../plugin_api
DEPENDPATH  += ../../plugin_api

# 主程序公共结构 + 协议接口 + 公共工具 + 南网 NW_2021 源码
INCLUDEPATH += ../../../common ../../../protocol ../../../protocol/common \
               ../../../protocol/nw_protocol/nw_2021
DEPENDPATH  += ../../../common ../../../protocol ../../../protocol/common \
               ../../../protocol/nw_protocol/nw_2021

HEADERS += \
    ../../plugin_api/iplugin.h \
    ../../plugin_api/iprotocolparserplugin.h

SOURCES += nw2021_parser_plugin.cpp \
    ../../../common/i18n.cpp \
    ../../../protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
    ../../../protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
    ../../../protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp

# 产物旁附带 plugin.json(方便直接当插件目录用)
QMAKE_POST_LINK += $$QMAKE_COPY $$PWD/plugin.json $$OUT_PWD $$escape_expand(\\n\\t)
