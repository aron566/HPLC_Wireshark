# gw_2022.pri - 国网 GW_2022 协议解析(解析器 + 各帧型 + PB 表)
#
# 模块内文件(国网双模标准 2022,与南网 NW_2021 完全独立):
#   - gw_2022_parser.h/.cpp          总控:剥物理头/FCH/帧分发/过滤
#   - gw_2022_msdu_parser.h/.cpp     MSDU/MAC 层字段解析
#   - gw_2022_pb_table.h            TMI → PB 块大小查表(国网定义)
#   - beacon/  sof/  ack/  coord/   各帧型解析(独立 .cpp/.h)

HEADERS += \
    $$PWD/gw_2022_parser.h \
    $$PWD/gw_2022_msdu_parser.h \
    $$PWD/gw_2022_pb_table.h \
    $$PWD/gw_2022_tree.h \
    $$PWD/beacon/gw_2022_beacon_parser.h \
    $$PWD/sof/gw_2022_sof_parser.h \
    $$PWD/ack/gw_2022_ack_parser.h \
    $$PWD/coord/gw_2022_coord_parser.h

SOURCES += \
    $$PWD/gw_2022_parser.cpp \
    $$PWD/gw_2022_msdu_parser.cpp \
    $$PWD/gw_2022_tree.cpp \
    $$PWD/beacon/gw_2022_beacon_parser.cpp \
    $$PWD/sof/gw_2022_sof_parser.cpp \
    $$PWD/ack/gw_2022_ack_parser.cpp \
    $$PWD/coord/gw_2022_coord_parser.cpp

INCLUDEPATH += $$PWD \
               $$PWD/beacon \
               $$PWD/sof \
               $$PWD/ack \
               $$PWD/coord
