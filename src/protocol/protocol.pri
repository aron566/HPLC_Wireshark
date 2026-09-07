# protocol.pri - 协议解析模块(总控 + 各帧解析子模块)
#
# 结构(每类帧解析独立 .pri,由本文件汇总):
#   - bplcparser.h/.cpp     总控:MPDU/FCH 解析、帧分发、多 PB 重组
#   - msduparser.h/.cpp     SOF 帧 MSDU/MAC 层字段解析(MMe/APP)
#   - beacon/beaconparser.* BEACON 帧载荷解析(beaconparser.pri)
#   - statistics.h/.cpp     帧类型统计
#
# 依赖:common(bplcframe.h, MsduState)
#
# 新增帧解析模块:建子目录 + <name>.pri,并在本文件 include 之。

include($$PWD/beacon/beaconparser.pri)

HEADERS += \
    $$PWD/bplcparser.h \
    $$PWD/msduparser.h \
    $$PWD/statistics.h

SOURCES += \
    $$PWD/bplcparser.cpp \
    $$PWD/msduparser.cpp \
    $$PWD/statistics.cpp

INCLUDEPATH += $$PWD $$PWD/beacon
