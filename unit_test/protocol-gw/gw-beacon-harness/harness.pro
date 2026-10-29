QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = gw_beacon_harness
TEMPLATE = app
R = $$PWD/../../../src
INCLUDEPATH += $$PWD/../../../3rdparty/qslog $$R $$R/common $$R/protocol $$R/protocol/common $$R/protocol/gw_protocol/gw_2022 $$R/protocol/gw_protocol/gw_2022/sof $$R/protocol/gw_protocol/gw_2022/beacon $$R/protocol/gw_protocol/gw_2022/ack $$R/protocol/gw_protocol/gw_2022/coord
SOURCES += main.cpp \
           $$R/protocol/gw_protocol/gw_2022/gw_2022_parser.cpp \
           $$R/protocol/gw_protocol/gw_2022/gw_2022_tree.cpp \
           $$R/protocol/gw_protocol/gw_2022/gw_2022_msdu_parser.cpp \
           $$R/protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp \
           $$R/protocol/gw_protocol/gw_2022/beacon/gw_2022_beacon_parser.cpp \
           $$R/protocol/protocol_tree_builder.cpp \
           $$R/common/i18n.cpp
SOURCES += $$PWD/../../../3rdparty/qslog/QsLog.cpp $$PWD/../../../3rdparty/qslog/QsLogDest.cpp $$PWD/../../../3rdparty/qslog/QsLogDestConsole.cpp $$PWD/../../../3rdparty/qslog/QsLogDestFile.cpp $$PWD/../../../3rdparty/qslog/QsLogDestFunctor.cpp
SOURCES += $$R/protocol/gw_protocol/gw_2022/ack/gw_2022_ack_parser.cpp $$R/protocol/gw_protocol/gw_2022/coord/gw_2022_coord_parser.cpp
HEADERS += $$PWD/../../../3rdparty/qslog/QsLogDestFunctor.h $$PWD/../../../3rdparty/qslog/QsLogDest.h $$PWD/../../../3rdparty/qslog/QsLog.h
