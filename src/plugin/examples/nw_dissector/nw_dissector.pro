# nw_dissector.pro: 南网 NW_2021 参考解剖器插件
TARGET = nw_dissector

DEFINES += DISSECTOR_NW

include(../dissector_common/dissector_plugin.pri)

SOURCES += \
    ../../../protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
    ../../../protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
    ../../../protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp
