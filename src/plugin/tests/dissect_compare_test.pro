# dissect_compare_test.pro: 解剖器 vs 内置解析器同帧一致性对比
QT       += core gui widgets serialport network
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = dissect_compare_test
TEMPLATE  = app

INCLUDEPATH += ../../common ../../protocol ../../protocol/common \
               ../../protocol/gw_protocol/gw_2022 \
               ../../protocol/gw_protocol/gw_2022/beacon \
               ../../protocol/gw_protocol/gw_2022/sof \
               ../../protocol/gw_protocol/gw_2022/ack \
               ../../protocol/gw_protocol/gw_2022/coord \
               ../../protocol/nw_protocol/nw_2021 \
               ../../ui ../../io ../plugin_api

include(../../../3rdparty/qslog/QsLog.pri)

HEADERS += \
    ../../ui/protocoltree.h \
    ../../io/serialreader.h \
    ../../protocol/statistics.h

SOURCES += \
    dissect_compare_test.cpp \
    ../../common/i18n.cpp \
    ../../io/serialreader.cpp \
    ../../ui/protocoltree.cpp \
    ../../protocol/statistics.cpp \
    ../../protocol/protocolfactory.cpp \
    ../../protocol/protocol_tree_builder.cpp \
    ../../protocol/gw_protocol/gw_2022/gw_2022_parser.cpp \
    ../../protocol/gw_protocol/gw_2022/gw_2022_msdu_parser.cpp \
    ../../protocol/gw_protocol/gw_2022/gw_2022_tree.cpp \
    ../../protocol/gw_protocol/gw_2022/beacon/gw_2022_beacon_parser.cpp \
    ../../protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp \
    ../../protocol/gw_protocol/gw_2022/ack/gw_2022_ack_parser.cpp \
    ../../protocol/gw_protocol/gw_2022/coord/gw_2022_coord_parser.cpp \
    ../../protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
    ../../protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
    ../../protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp \
    ../../protocol/nw_protocol/nw_2021/nw_2021_tree.cpp
