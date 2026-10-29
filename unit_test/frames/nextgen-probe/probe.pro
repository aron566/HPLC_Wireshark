QT += core network gui widgets
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = nextgen_probe
INCLUDEPATH += $$PWD/../../../src $$PWD/../../../src/common $$PWD/../../../src/protocol $$PWD/../../../src/protocol/common $$PWD/../../../src/protocol/nw_protocol/nw_2021 $$PWD/../../../src/protocol/nw_protocol/nw_2026
SOURCES += main.cpp
SOURCES += $$PWD/../../../src/protocol/nw_protocol/nw_2021/nw_2021_parser.cpp $$PWD/../../../src/protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp $$PWD/../../../src/protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp
SOURCES += $$PWD/../../../src/protocol/nw_protocol/nw_2026/nw_2026_parser.cpp $$PWD/../../../src/protocol/nw_protocol/nw_2026/nw_2026_msdu_parser.cpp $$PWD/../../../src/protocol/nw_protocol/nw_2026/nw_2026_beacon_parser.cpp
SOURCES += $$PWD/../../../src/common/i18n.cpp

INCLUDEPATH += $$PWD/../../../src/protocol/gw_protocol/gw_2022 $$PWD/../../../src/protocol/gw_protocol/gw_2022/beacon $$PWD/../../../src/protocol/gw_protocol/gw_2022/sof $$PWD/../../../src/protocol/gw_protocol/gw_2022/ack $$PWD/../../../src/protocol/gw_protocol/gw_2022/coord $$PWD/../../../src/protocol/gw_protocol/gw_2026 $$PWD/../../../src/protocol/gw_protocol/gw_2026/beacon $$PWD/../../../src/protocol/gw_protocol/gw_2026/sof $$PWD/../../../src/protocol/gw_protocol/gw_2026/ack $$PWD/../../../src/protocol/gw_protocol/gw_2026/coord
SOURCES += $$PWD/../../../src/protocol/gw_protocol/gw_2022/gw_2022_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2022/gw_2022_msdu_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2022/beacon/gw_2022_beacon_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2022/ack/gw_2022_ack_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2022/coord/gw_2022_coord_parser.cpp
SOURCES += $$PWD/../../../src/protocol/gw_protocol/gw_2026/gw_2026_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2026/gw_2026_msdu_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2026/beacon/gw_2026_beacon_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2026/sof/gw_2026_sof_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2026/ack/gw_2026_ack_parser.cpp $$PWD/../../../src/protocol/gw_protocol/gw_2026/coord/gw_2026_coord_parser.cpp
INCLUDEPATH += $$PWD/../../../3rdparty/qslog
SOURCES += $$PWD/../../../3rdparty/qslog/QsLog.cpp $$PWD/../../../3rdparty/qslog/QsLogDest.cpp $$PWD/../../../3rdparty/qslog/QsLogDestConsole.cpp $$PWD/../../../3rdparty/qslog/QsLogDestFile.cpp $$PWD/../../../3rdparty/qslog/QsLogDestFunctor.cpp
HEADERS += $$PWD/../../../3rdparty/qslog/QsLog.h $$PWD/../../../3rdparty/qslog/QsLogDest.h $$PWD/../../../3rdparty/qslog/QsLogDestFunctor.h $$PWD/../../../3rdparty/qslog/QsLogDestConsole.h $$PWD/../../../3rdparty/qslog/QsLogDestFile.h
