# NW_2021 枚举独立头编译检查(A5):只含 nw_2021_frametype.h,QT -= gui widgets
# (若新头依赖 QtWidgets,编译即失败)
QT += core
QT -= gui widgets
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = nw2021_frametype_probe
TEMPLATE = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src/protocol/nw_protocol/nw_2021

SOURCES += frametype_probe.cpp
HEADERS += $$REPO/src/protocol/nw_protocol/nw_2021/nw_2021_frametype.h
