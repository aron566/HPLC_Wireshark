# dissector_plugin.pri: 参考解剖器插件公共构建片段(gw/nw 两打包共用)
# 用法:各打包 .pro 设 TARGET 与变体 DEFINES/解析器源码后 include 本文件。
QT       += core gui widgets
CONFIG   += c++17 plugin
CONFIG   -= app_bundle
TEMPLATE  = lib

DEFINES += QT_DEPRECATED_WARNINGS QT_PLUGIN

# 插件 API + 主程序公共结构/协议头
INCLUDEPATH += ../../plugin_api \
               ../../../common \
               ../../../protocol \
               ../../../protocol/common \
               ../../../protocol/gw_protocol/gw_2022 \
               ../../../protocol/gw_protocol/gw_2022/beacon \
               ../../../protocol/gw_protocol/gw_2022/sof \
               ../../../protocol/gw_protocol/gw_2022/ack \
               ../../../protocol/gw_protocol/gw_2022/coord \
               ../../../protocol/nw_protocol/nw_2021 \
               $$PWD
DEPENDPATH  += ../../plugin_api ../../../common ../../../protocol

# QsLog(内置解析器源码依赖其日志宏)
include(../../../../3rdparty/qslog/QsLog.pri)

HEADERS += \
    ../../plugin_api/iplugin.h \
    ../../plugin_api/iprotocolparserplugin.h \
    ../../plugin_api/dissect.h \
    ../../plugin_api/idissectorplugin.h \
    ../dissector_common/field_gen.h

SOURCES += \
    ../dissector_common/dissector_plugin.cpp \
    ../dissector_common/field_gen.cpp \
    ../../../common/i18n.cpp \
    ../../../protocol/statistics.cpp

# 产物旁附带 plugin.json(方便直接当插件目录用)
QMAKE_POST_LINK += $$QMAKE_COPY $$PWD/plugin.json $$OUT_PWD $$escape_expand(\\n\\t)
