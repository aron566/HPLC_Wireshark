/// @file topo_state.h
/// @brief 拓扑(Topo)状态模型:单 NID 网络的节点/路由累积状态 + 路由变更事件历史
/// @details 本模块独立于 app/io/ui,只依赖 common(bplcframe.h 的 TopoEvent/TeiMacPair)。
///          协议解析器在关键管理消息(关联请求/关联确认/代理变更/发现列表/离网指示)
///          解析时填充 MsduInfo::topo_event;TopoState 消费该事件,累积为节点图 + 事件表。
#ifndef TOPO_STATE_H
#define TOPO_STATE_H

#include "bplcframe.h"
#include <QHash>
#include <QVector>

/// @brief 节点入网状态(对应拓扑图图标三态)
enum class NodeStatus {
    OnlineGoing, ///< 正在入网(STA 发起关联请求,尚未完成)
    Online,      ///< 已入网(关联确认/发现列表在网)
    Offline,     ///< 离线(离网指示)
};

/// @brief 单个节点的拓扑信息
struct TopoNode {
    quint16 tei = 0;
    quint64 mac = 0;
    quint16 parent_tei = 0xFFFF; ///< 代理 TEI;1=CCO 直属;0=根(CCO 自身);0xFFFF=未知
    NodeStatus status = NodeStatus::OnlineGoing;
    qint64  last_seen_ms = 0;
    bool    is_rf = false;            ///< 接入方式:false=PLC 载波;true=HRF 无线

    /// @brief 是否在线(非离线)
    bool online() const { return status != NodeStatus::Offline; }
};

/// @brief 单个 NID 网络的拓扑状态(节点 + 路由 + 事件历史)
class TopoState {
public:
    quint32 nid = 0;
    quint64 cco_mac = 0;
    QHash<quint16, TopoNode> nodes; ///< TEI → 节点(含 CCO TEI=1)
    QHash<quint64, TopoNode> pending; ///< MAC → 正在入网节点(TEI 未分配,关联请求阶段)
    QHash<quint16, CommRateInfo> comm_rates; ///< TEI → 通讯成功率(成功率上报)
    QVector<TopoEvent> events;      ///< 路由变更事件(时间序,供底部表格)

    /// @brief 应用一个拓扑事件,增量更新节点/路由/入网状态并记录事件
    void apply(const TopoEvent& e);

    /// @brief 计算各节点层级(从 CCO=0 沿父链向下;不可达=-1)
    QHash<quint16, int> compute_levels() const;

    /// @brief 取节点 MAC(TEI 未知返回 0)
    quint64 mac_of(quint16 tei) const;
};

#endif // TOPO_STATE_H
