# gw_dissector.pro: 国网 GW_2022 参考解剖器插件
TARGET = gw_dissector

include(../dissector_common/dissector_plugin.pri)

SOURCES += \
    ../../../protocol/gw_protocol/gw_2022/gw_2022_parser.cpp \
    ../../../protocol/gw_protocol/gw_2022/gw_2022_msdu_parser.cpp \
    ../../../protocol/gw_protocol/gw_2022/beacon/gw_2022_beacon_parser.cpp \
    ../../../protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp \
    ../../../protocol/gw_protocol/gw_2022/ack/gw_2022_ack_parser.cpp \
    ../../../protocol/gw_protocol/gw_2022/coord/gw_2022_coord_parser.cpp
