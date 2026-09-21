# common.pri - 协议公共工具(位域/CRC/BCD/字段树/协议变体)
#
# 模块内文件(协议无关,国网/南网共用,永远不变):
#   - protocolvariant.h  协议变体枚举(GW_2022 / NW_2021)
#   - fieldtools.h       位域取值/设置 + hex/MAC 格式化
#   - crc.h              CRC32 / CRC24
#   - bcd.h              BCD 编解码
#   - fieldspec.h        字段树构建工具(FieldSpec/add_fields/group/annotate_unit)

HEADERS += \
    $$PWD/protocolvariant.h \
    $$PWD/fieldtools.h \
    $$PWD/crc.h \
    $$PWD/bcd.h \
    $$PWD/fieldspec.h

INCLUDEPATH += $$PWD
