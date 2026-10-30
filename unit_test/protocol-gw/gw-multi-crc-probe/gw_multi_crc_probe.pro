QT += core gui widgets
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = gw_multi_crc_probe
TEMPLATE = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/common $$REPO/src/protocol \
               $$REPO/src/protocol/common \
               $$REPO/src/protocol/gw_protocol/gw_2026 \
               $$REPO/src/protocol/gw_protocol/gw_2026/sof \
               $$REPO/3rdparty/qslog
DEFINES += QsLog_STATIC

HEADERS += $$REPO/3rdparty/qslog/QsLog.h \
    $$REPO/3rdparty/qslog/QsLogDest.h \
    $$REPO/3rdparty/qslog/QsLogDestFile.h \
    $$REPO/3rdparty/qslog/QsLogDestFunctor.h \
    $$REPO/3rdparty/qslog/QsLogDestConsole.h

SOURCES += gw_multi_crc_probe.cpp \
    $$REPO/src/protocol/gw_protocol/gw_2026/sof/gw_2026_sof_parser.cpp \
    $$REPO/src/common/i18n.cpp \
    $$REPO/3rdparty/qslog/QsLog.cpp \
    $$REPO/3rdparty/qslog/QsLogDest.cpp \
    $$REPO/3rdparty/qslog/QsLogDestConsole.cpp \
    $$REPO/3rdparty/qslog/QsLogDestFile.cpp \
    $$REPO/3rdparty/qslog/QsLogDestFunctor.cpp
