# topo.pri - 拓扑模块(独立窗口:拓扑图 + 路由变更表 + TEI→MAC 表)
#
# 与 app/io/ui 解耦,只依赖 common(bplcframe.h 的 TopoEvent/TeiMacPair)。
# 协议解析器在关键管理消息填充 MsduInfo::topo_event;MainWindow 将其喂给
# QHash<quint32, TopoState> 累积,TopoWindow 按 NID 读取状态绘制。
#
# 模块内文件:
#   - topo_state.h/.cpp     单 NID 拓扑状态(节点/路由/事件历史)
#   - topo_window.h/.cpp    独立窗口 + 层次拓扑图 + 两张表

HEADERS += \
    $$PWD/topo_state.h \
    $$PWD/topo_window.h

SOURCES += \
    $$PWD/topo_state.cpp \
    $$PWD/topo_window.cpp

INCLUDEPATH += $$PWD
