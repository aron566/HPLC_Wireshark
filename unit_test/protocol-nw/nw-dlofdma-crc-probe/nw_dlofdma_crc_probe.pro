QT += core gui widgets
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = nw_dlofdma_crc_probe
TEMPLATE = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/common $$REPO/src/protocol \
               $$REPO/src/protocol/common \
               $$REPO/src/protocol/nw_protocol/nw_2026 \
               $$REPO/src/protocol/nw_protocol/nw_2021 \
               $$REPO/3rdparty/qslog
DEFINES += QsLog_STATIC

HEADERS += $$REPO/3rdparty/qslog/QsLog.h \
    $$REPO/3rdparty/qslog/QsLogDest.h \
    $$REPO/3rdparty/qslog/QsLogDestFile.h \
    $$REPO/3rdparty/qslog/QsLogDestFunctor.h \
    $$REPO/3rdparty/qslog/QsLogDestConsole.h

SOURCES += nw_dlofdma_crc_probe.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2026/nw_2026_parser.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2026/nw_2026_msdu_parser.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2026/nw_2026_beacon_parser.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2026/nw_2026_tree.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp \
    $$REPO/src/protocol/nw_protocol/nw_2021/nw_2021_tree.cpp \
    $$REPO/src/protocol/protocol_tree_builder.cpp \
    $$REPO/src/common/i18n.cpp \
    $$REPO/3rdparty/qslog/QsLog.cpp \
    $$REPO/3rdparty/qslog/QsLogDest.cpp \
    $$REPO/3rdparty/qslog/QsLogDestConsole.cpp \
    $$REPO/3rdparty/qslog/QsLogDestFile.cpp \
    $$REPO/3rdparty/qslog/QsLogDestFunctor.cpp
