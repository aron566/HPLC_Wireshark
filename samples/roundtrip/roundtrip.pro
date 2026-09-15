QT       += core
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = roundtrip
TEMPLATE  = app

INCLUDEPATH += $$PWD/../../src/common \
               $$PWD/../../src/io

SOURCES += $$PWD/roundtrip.cpp
