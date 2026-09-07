# beaconparser.pri - BEACON 帧载荷解析模块(51242/51243 信标载荷)
#
# 模块内文件:
#   - beaconparser.h      BEACON 载荷解析器声明
#   - beaconparser.cpp    实现(固定头/管理条目/PB Padding/CRC)
#
# 依赖:common(bplcframe.h) + protocol(fieldspec.h 公共字段工具)
# 被 protocol.pri include;扩展信标条目见 beaconparser.cpp 内注释。

HEADERS += $$PWD/beaconparser.h
SOURCES += $$PWD/beaconparser.cpp
