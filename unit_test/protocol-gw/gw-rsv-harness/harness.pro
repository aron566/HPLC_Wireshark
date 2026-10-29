QT += core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = gw_rsv_harness
TEMPLATE = app
INCLUDEPATH += $$PWD/../../../src \
               $$PWD/../../../src/common \
               $$PWD/../../../src/protocol \
               $$PWD/../../../src/protocol/common \
               $$PWD/../../../src/protocol/gw_protocol/gw_2022 \
               $$PWD/../../../src/protocol/gw_protocol/gw_2022/sof
SOURCES += main.cpp \
           $$PWD/../../../src/protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp \
           $$PWD/../../../src/common/i18n.cpp
