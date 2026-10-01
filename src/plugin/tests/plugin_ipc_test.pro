QT += core network gui
QT -= widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = plugin_ipc_test
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common $$PWD/../../protocol $$PWD/../plugin_api

SOURCES += plugin_ipc_test.cpp \
    ../ipc/plugin_serialization.cpp

HEADERS += ../ipc/plugin_ipc.h \
    ../ipc/plugin_serialization.h
