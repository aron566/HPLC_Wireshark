QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = nw2026_ul_probe
TEMPLATE = app
R = $$PWD/../../../src
INCLUDEPATH += $$R $$R/common $$R/protocol $$R/protocol/common $$R/protocol/nw_protocol/nw_2026 $$R/protocol/nw_protocol/nw_2021 $$R/../3rdparty/qslog
SOURCES += main.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_parser.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_msdu_parser.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_beacon_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
           $$R/common/i18n.cpp
QSLOG = $$PWD/../../../3rdparty/qslog
SOURCES += $$QSLOG/QsLog.cpp $$QSLOG/QsLogDest.cpp $$QSLOG/QsLogDestFile.cpp $$QSLOG/QsLogDestConsole.cpp $$QSLOG/QsLogDestFunctor.cpp
HEADERS += $$QSLOG/QsLog.h $$QSLOG/QsLogDest.h $$QSLOG/QsLogDestFunctor.h $$QSLOG/QsLogDestConsole.h $$QSLOG/QsLogDestFile.h
