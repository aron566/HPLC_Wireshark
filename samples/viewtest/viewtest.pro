QT       += core gui widgets
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = viewtest
TEMPLATE  = app

INCLUDEPATH += $$PWD/../../src \
               $$PWD/../../src/common \
               $$PWD/../../src/ui

# viewtest 依赖 ui + common + protocol(解析真实回放文件)
include($$PWD/../../src/common/common.pri)
include($$PWD/../../src/protocol/protocol.pri)

SOURCES += $$PWD/viewtest.cpp \
           $$PWD/../../src/ui/protocoltree.cpp \
           $$PWD/../../src/ui/hexview.cpp \
           $$PWD/../../src/ui/packetlistmodel.cpp
HEADERS += $$PWD/../../src/ui/protocoltree.h \
           $$PWD/../../src/ui/hexview.h \
           $$PWD/../../src/ui/packetlistmodel.h
