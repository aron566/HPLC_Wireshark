QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = nw_msdu_probe
TEMPLATE = app
R = $$PWD/../../../src
INCLUDEPATH += $$R $$R/common $$R/protocol $$R/protocol/common $$R/protocol/nw_protocol/nw_2021 $$PWD/../../../3rdparty/qslog
SOURCES += main.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
           $$R/common/i18n.cpp
SOURCES += $$PWD/../../../3rdparty/qslog/QsLog.cpp $$PWD/../../../3rdparty/qslog/QsLogDest.cpp $$PWD/../../../3rdparty/qslog/QsLogDestConsole.cpp $$PWD/../../../3rdparty/qslog/QsLogDestFile.cpp $$PWD/../../../3rdparty/qslog/QsLogDestFunctor.cpp
HEADERS += $$PWD/../../../3rdparty/qslog/QsLogDestFunctor.h $$PWD/../../../3rdparty/qslog/QsLogDest.h $$PWD/../../../3rdparty/qslog/QsLog.h
