QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = nw_frame_probe
TEMPLATE = app
R = $$PWD/../../../src
INCLUDEPATH += $$R $$R/common $$R/protocol $$R/protocol/common $$R/protocol/nw_protocol/nw_2021
SOURCES += main.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_beacon_parser.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_msdu_parser.cpp \
           $$R/common/i18n.cpp
