QT += core gui widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = nw_mcs_harness
TEMPLATE = app
R = $$PWD/../../../src
INCLUDEPATH += $$R $$R/common $$R/protocol $$R/protocol/common $$R/protocol/nw_protocol/nw_2021
SOURCES += $$PWD/../../../unit_test/protocol-nw/nw-mcs-harness/main.cpp \
           $$R/protocol/nw_protocol/nw_2021/nw_2021_tree.cpp \
           $$R/protocol/protocol_tree_builder.cpp \
           $$R/common/i18n.cpp
