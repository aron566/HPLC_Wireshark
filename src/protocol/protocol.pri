# protocol.pri - 协议解析 + 统计
#
# 模块内文件:
#   - bplcparser.h/.cpp   BPLC/HPLC+HRF 协议解析
#   - msduparser.h/.cpp   MSDU/MAC 层字段解析(MMe/APP)
#   - statistics.h/.cpp    帧类型统计
#
# 依赖:common(bplcframe.h, MsduState)

HEADERS += \
    $$PWD/bplcparser.h \
    $$PWD/msduparser.h \
    $$PWD/beaconparser.h \
    $$PWD/statistics.h

SOURCES += \
    $$PWD/bplcparser.cpp \
    $$PWD/msduparser.cpp \
    $$PWD/statistics.cpp

INCLUDEPATH += $$PWD
