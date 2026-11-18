# NW_2026 委托走注册表探针(A1 委托语义):内置委托与裸 NW_2021 一致、影子覆盖生效
QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = nw2026_delegate_probe
TEMPLATE = app

REPO = $$PWD/../../..
R = $$REPO/src
INCLUDEPATH += $$R $$R/common $$R/protocol $$R/protocol/common \
               $$R/protocol/nw_protocol/nw_2026 $$R/protocol/nw_protocol/nw_2021 \
               $$REPO/3rdparty/qslog
SOURCES += delegate_probe.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_parser.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_msdu_parser.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_beacon_parser.cpp \
           $$R/protocol/nw_protocol/nw_2026/nw_2026_tree.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_tree.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_provider.cpp \
           $$R/protocol/protocolfactory.cpp \
           $$R/protocol/protocol_tree_builder.cpp \
           $$R/common/i18n.cpp
QSLOG = $$REPO/3rdparty/qslog
SOURCES += $$QSLOG/QsLog.cpp $$QSLOG/QsLogDest.cpp $$QSLOG/QsLogDestFile.cpp \
           $$QSLOG/QsLogDestConsole.cpp $$QSLOG/QsLogDestFunctor.cpp
HEADERS += $$QSLOG/QsLog.h $$QSLOG/QsLogDest.h $$QSLOG/QsLogDestFunctor.h \
           $$QSLOG/QsLogDestConsole.h $$QSLOG/QsLogDestFile.h
