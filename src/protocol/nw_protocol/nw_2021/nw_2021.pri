# nw_2021.pri - 南网 NW_2021 协议解析(骨架:帧控制 + 各帧型 FCH 字段)
#
# 南网双模 2021 报批版。与国网 GW_2022 完全独立:
#   - nw_2021_parser.h/.cpp          总控(SNID 4b / 载荷 PB 大小查表)
#   - nw_2021_pb_table.h            载波 TMI / 无线载荷 PB 大小 → PB size
#   - (待补) nw_2021_msdu_parser     MSDU 头(MAC 48b + VLAN 标签)
#   - (待补) beacon/ sof/ ack/ coord 各帧型详细字段
#
# 参考实现:D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG(MPDU_Class.py / MSDU_Class.py)

HEADERS += \
    $$PWD/nw_2021_parser.h \
    $$PWD/nw_2021_msdu_parser.h \
    $$PWD/nw_2021_pb_table.h

SOURCES += \
    $$PWD/nw_2021_parser.cpp \
    $$PWD/nw_2021_msdu_parser.cpp

INCLUDEPATH += $$PWD
