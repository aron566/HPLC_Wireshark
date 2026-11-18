# 分层注册表探针(A1 内置层/覆盖层 + A-7 内置只读守卫 + A4 影子告警):
# 只链接 protocolfactory.cpp,用测试替身提供者模拟四内置协议与插件协议
QT += core gui widgets
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = protocol_registry_probe
TEMPLATE = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/common $$REPO/src/protocol $$REPO/src/protocol/common

SOURCES += registry_probe.cpp \
    $$REPO/src/protocol/protocolfactory.cpp
HEADERS += $$REPO/src/protocol/protocolfactory.h \
    $$REPO/src/protocol/iprotocolprovider.h
