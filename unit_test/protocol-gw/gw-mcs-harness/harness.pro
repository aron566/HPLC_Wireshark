QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = gw_mcs_harness
TEMPLATE = app
R = $$PWD/../../../src
INCLUDEPATH += $$R $$R/common $$R/protocol $$R/protocol/common $$R/protocol/gw_protocol/gw_2022
SOURCES += main.cpp \
           $$R/protocol/gw_protocol/gw_2022/gw_2022_tree.cpp \
           $$R/protocol/protocol_tree_builder.cpp \
           $$R/common/i18n.cpp
