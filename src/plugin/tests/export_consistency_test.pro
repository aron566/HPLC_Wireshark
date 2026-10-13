# export_consistency_test.pro: 导出层一致性实测(内置 vs 插件解剖 → 导出字节比对)
# 运行布局:二进制与 bplc-plugin-host 同目录,plugins/ 在其下(同 dissect_ipc_test)。
QT       += core gui widgets serialport network
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = export_consistency_test
TEMPLATE  = app

INCLUDEPATH += ../../common ../../protocol ../../protocol/common \
               ../../protocol/gw_protocol/gw_2022 \
               ../../protocol/gw_protocol/gw_2022/beacon \
               ../../protocol/gw_protocol/gw_2022/sof \
               ../../protocol/gw_protocol/gw_2022/ack \
               ../../protocol/gw_protocol/gw_2022/coord \
               ../../protocol/nw_protocol/nw_2021 \
               ../../io

include(../plugin.pri)
include(../../../3rdparty/qslog/QsLog.pri)

HEADERS += \
    ../../io/serialreader.h \
    ../../io/playbackwriter.h \
    ../../common/packetentry_serialize.h \
    ../../protocol/statistics.h

SOURCES += \
    export_consistency_test.cpp \
    ../../common/i18n.cpp \
    ../../io/serialreader.cpp \
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
