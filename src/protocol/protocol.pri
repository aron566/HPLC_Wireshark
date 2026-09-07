# protocol.pri - 协议解析模块(总控 + 各帧解析子模块)
#
# 结构(每类帧解析独立 .pri,由本文件汇总):
#   - bplcparser.h/.cpp     总控:物理头剥取、FCH 公共头校验、帧分发、过滤
#   - msduparser.h/.cpp     MSDU/MAC 层字段解析(SOF 重组完整后)
#   - beacon/beaconparser.* BEACON 帧载荷解析(beaconparser.pri)
#   - sof/sofparser.*       SOF 帧解析:FCH 字段 + 多 PB/MSDU 重组(sof.pri)
#   - ack/ackparser.*       ACK 帧解析(ackparser.pri)
#   - coord/coordparser.*   网间协调帧(COORD)解析(coord.pri)
#   - statistics.h/.cpp     帧类型统计
#
# 依赖:common(bplcframe.h, MsduState, fieldspec.h 公共字段工具)
#
# 新增帧解析模块:建子目录 + <name>.pri,并在本文件 include 之。

include($$PWD/beacon/beaconparser.pri)
include($$PWD/sof/sof.pri)
include($$PWD/ack/ack.pri)
include($$PWD/coord/coord.pri)

HEADERS += \
    $$PWD/bplcparser.h \
    $$PWD/msduparser.h \
    $$PWD/statistics.h

SOURCES += \
    $$PWD/bplcparser.cpp \
    $$PWD/msduparser.cpp \
    $$PWD/statistics.cpp

INCLUDEPATH += $$PWD \
               $$PWD/beacon \
               $$PWD/sof \
               $$PWD/ack \
               $$PWD/coord
