# gw_2026.pri - 国网 GW_2026 协议解析(解析器 + 各帧型 + PB 表)
#
# 模块内文件(国网 HDC 2.0;HDC 1.0 帧经 parse() 版本自适应委托 GW_2022):
#   - gw_2026_parser.h/.cpp          总控:剥物理头/FCH/版本自适应/帧分发/过滤
#   - gw_2026_msdu_parser.h/.cpp     MSDU/MAC 层字段解析(表2 含聚合标识/发送帧序号/LinkID)
#   - gw_2026_pb_table.h            TMI → PB 块大小查表(国网定义)
#   - beacon/  sof/  ack/  coord/   各帧型解析(独立 .cpp/.h,载波按 HDC 2.0 表5-表14)

HEADERS += \
    $$PWD/gw_2026_parser.h \
    $$PWD/gw_2026_msdu_parser.h \
    $$PWD/gw_2026_pb_table.h \
    $$PWD/gw_2026_tree.h \
    $$PWD/beacon/gw_2026_beacon_parser.h \
    $$PWD/sof/gw_2026_sof_parser.h \
    $$PWD/ack/gw_2026_ack_parser.h \
    $$PWD/coord/gw_2026_coord_parser.h

SOURCES += \
    $$PWD/gw_2026_parser.cpp \
    $$PWD/gw_2026_msdu_parser.cpp \
    $$PWD/gw_2026_tree.cpp \
    $$PWD/beacon/gw_2026_beacon_parser.cpp \
    $$PWD/sof/gw_2026_sof_parser.cpp \
    $$PWD/ack/gw_2026_ack_parser.cpp \
    $$PWD/coord/gw_2026_coord_parser.cpp \
    $$PWD/gw_2026_provider.cpp

INCLUDEPATH += $$PWD \
               $$PWD/beacon \
               $$PWD/sof \
               $$PWD/ack \
               $$PWD/coord
