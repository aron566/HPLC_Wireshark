# Reader 信封必填探针(A-6):3C 回放 bin 与裸 hex 两处构造点必填
# epoch_ms/timestamp/phr_mcs/option/channel/is_rf,与 GW_2022 解析器
# decode_envelope 的复核值恒等(同字节同公式)。只链接 reader 与 GW_2022
# 解析链,不碰插件 IPC/Envelope。
QT       += core gui widgets serialport
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = reader_envelope_probe
TEMPLATE  = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src/common $$REPO/src/io $$REPO/src/protocol $$REPO/src/protocol/common \
               $$REPO/src/protocol/gw_protocol/gw_2022 \
               $$REPO/src/protocol/gw_protocol/gw_2022/sof \
               $$REPO/src/protocol/gw_protocol/gw_2022/beacon \
               $$REPO/src/protocol/gw_protocol/gw_2022/ack \
               $$REPO/src/protocol/gw_protocol/gw_2022/coord \
               $$REPO/3rdparty/qslog

include($$REPO/3rdparty/qslog/QsLog.pri)
include($$REPO/src/common/common.pri)

SOURCES += reader_envelope_probe.cpp \
           $$REPO/src/io/serialreader.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/gw_2022_parser.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/gw_2022_tree.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/gw_2022_msdu_parser.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/beacon/gw_2022_beacon_parser.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/ack/gw_2022_ack_parser.cpp \
           $$REPO/src/protocol/gw_protocol/gw_2022/coord/gw_2022_coord_parser.cpp \
           $$REPO/src/protocol/protocol_tree_builder.cpp
HEADERS += $$REPO/src/io/serialreader.h
