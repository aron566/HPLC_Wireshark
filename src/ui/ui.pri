# ui.pri - 自定义 Qt 控件(Wireshark 风格的三栏组件)
#
# 模块内文件:
#   - packetlistmodel.h/.cpp    PacketList 的 QAbstractTableModel
#   - hexview.h/.cpp            hex+ASCII 视图
#   - protocoltree.h/.cpp       协议树(分层 MPDU 字段)
#
# 依赖:common(bplcframe.h, PacketEntry)

HEADERS += \
    $$PWD/packetlistmodel.h \
    $$PWD/hexview.h \
    $$PWD/protocoltree.h

SOURCES += \
    $$PWD/packetlistmodel.cpp \
    $$PWD/hexview.cpp \
    $$PWD/protocoltree.cpp

INCLUDEPATH += $$PWD
