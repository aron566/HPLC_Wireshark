# protocol.pri - 协议解析模块(公共工具 + 各协议实现)
#
# 结构(每个协议一个目录,独立 .pri,由本文件汇总):
#   - common/               协议公共工具(位域/CRC/BCD/字段树/协议变体)
#   - gw_protocol/gw_2022/  国网双模标准 2022(gw_2022.pri)
#   - nw_protocol/nw_2021/  南网双模 2021 报批版(nw_2021.pri)
#   - statistics.h/.cpp     帧类型统计(协议无关)
#
# 新增协议版本:建 <proto>_protocol/<proto>_<yyyy>/ 目录 + <proto>_<yyyy>.pri,
# 并在本文件 include 之。协议升级(如南网 2026)= 新增 nw_2026/ 目录,老版本零改动。

include($$PWD/common/common.pri)
include($$PWD/gw_protocol/gw_2022/gw_2022.pri)
include($$PWD/nw_protocol/nw_2021/nw_2021.pri)

HEADERS += \
    $$PWD/iprotocolparser.h \
    $$PWD/protocolfactory.h \
    $$PWD/statistics.h

SOURCES += \
    $$PWD/protocolfactory.cpp \
    $$PWD/statistics.cpp

INCLUDEPATH += $$PWD \
               $$PWD/common
