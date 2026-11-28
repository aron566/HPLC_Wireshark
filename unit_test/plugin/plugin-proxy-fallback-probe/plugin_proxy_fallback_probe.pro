QT += core gui widgets network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = plugin_proxy_fallback_probe
TEMPLATE = app
REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/app $$REPO/src/plugin $$REPO/src/common $$REPO/src/protocol $$REPO/src/protocol/common $$REPO/3rdparty/qslog
include($$REPO/3rdparty/qslog/QsLog.pri)
include($$REPO/src/plugin/plugin.pri)
include($$REPO/src/protocol/protocol.pri)
SOURCES += plugin_proxy_fallback_probe.cpp \
    $$REPO/src/common/i18n.cpp
HEADERS += $$REPO/src/common/i18n.h
