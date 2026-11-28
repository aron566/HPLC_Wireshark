QT += core gui widgets network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = plugin_parse_timeout_unregister_probe
TEMPLATE = app
REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/app $$REPO/src/plugin $$REPO/src/common $$REPO/src/protocol $$REPO/src/protocol/common
include($$REPO/src/plugin/plugin.pri)
SOURCES += plugin_parse_timeout_unregister_probe.cpp \
    $$REPO/src/protocol/protocolfactory.cpp
HEADERS += $$REPO/src/protocol/protocolfactory.h
