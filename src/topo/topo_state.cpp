/// @file topo_state.cpp
/// @brief 拓扑状态模型实现(节点/路由累积 + 层级计算)
#include "topo_state.h"

namespace {

/// @brief 事件类型 → 节点入网状态(发起关联请求=正在入网,其余在网事件=已入网)
NodeStatus status_for(TopoEventKind k) {
    switch (k) {
        case TopoEventKind::AssocReq: return NodeStatus::OnlineGoing;
        default:                      return NodeStatus::Online;
    }
}

} // namespace

void TopoState::apply(const TopoEvent& e) {
    nid = e.nid;
    // CCO(TEI=1):根节点,父=0,始终已入网
    if (e.cco_mac) {
        cco_mac = e.cco_mac;
        TopoNode& cco = nodes[1];
        cco.tei = 1;
        cco.mac = e.cco_mac;
        cco.parent_tei = 0;
        cco.status = NodeStatus::Online;
        cco.last_seen_ms = e.epoch_ms;
        cco.is_rf = e.is_rf;
    }
    // TEI→MAC 学习对(节点出现,按事件类型定入网状态)
    for (const TeiMacPair& p : e.nodes) {
        if (!p.mac) continue;
        if (p.tei == 0) {
            // 正在入网(TEI 未分配):按 MAC 暂存 pending(关联请求阶段)
            TopoNode n;
            n.tei = 0;
            n.mac = p.mac;
            n.status = NodeStatus::OnlineGoing;
            n.last_seen_ms = e.epoch_ms;
            n.is_rf = e.is_rf;
            pending[p.mac] = n;
            continue;
        }
        TopoNode& n = nodes[p.tei];
        n.tei = p.tei;
        n.mac = p.mac;
        n.status = status_for(e.kind);
        n.last_seen_ms = e.epoch_ms;
        n.is_rf = e.is_rf;
        // 若该 MAC 之前正在入网(关联请求),现已分配到 TEI → 移除 pending
        for (auto it = pending.begin(); it != pending.end();) {
            if (it.value().mac == p.mac) it = pending.erase(it);
            else ++it;
        }
    }
    // 路由关系(子 TEI → 父/代理 TEI)
    for (const auto& r : e.routes) {
        if (!r.first) continue;
        TopoNode& n = nodes[r.first];
        n.tei = r.first;
        n.parent_tei = r.second;
        n.status = status_for(e.kind);
        n.last_seen_ms = e.epoch_ms;
        n.is_rf = e.is_rf;
    }
    // 离网节点(按 MAC 匹配,标记离线)
    for (quint64 mac : e.leaves) {
        if (!mac) continue;
        for (auto it = nodes.begin(); it != nodes.end(); ++it) {
            if (it.value().mac == mac) {
                it.value().status = NodeStatus::Offline;
                it.value().last_seen_ms = e.epoch_ms;
                break;
            }
        }
    }
    // 通讯成功率(成功率上报,按 TEI 覆盖更新)
    for (const CommRateInfo& cr : e.comm_rates) {
        if (!cr.tei) continue;
        comm_rates[cr.tei] = cr;
    }
    // 记录路由变更事件(供底部表格):仅真正"变更"入表。
    // 发现列表(周期性快照)/成功率上报(通讯质量)不入路由变更表。
    switch (e.kind) {
        case TopoEventKind::AssocReq:
        case TopoEventKind::AssocCnf:
        case TopoEventKind::ChangeProxyCnf:
        case TopoEventKind::LeaveInd:
            events.append(e);
            break;
        default:
            break;
    }
}

QHash<quint16, int> TopoState::compute_levels() const {
    QHash<quint16, int> lv;
    if (nodes.isEmpty()) return lv;
    lv[1] = 0;                 // CCO 层级 0
    // 迭代松弛:父层级已确定时子层级 = 父 + 1(容忍环路,最多节点数轮)
    for (int pass = 0; pass < nodes.size() + 1; ++pass) {
        bool changed = false;
        for (auto it = nodes.begin(); it != nodes.end(); ++it) {
            const quint16 tei = it.key();
            if (tei == 1) continue;
            const quint16 parent = it.value().parent_tei;
            if (parent == 0xFFFF || !lv.contains(parent)) continue;
            const int nl = lv[parent] + 1;
            if (!lv.contains(tei) || lv[tei] != nl) { lv[tei] = nl; changed = true; }
        }
        if (!changed) break;
    }
    return lv;
}

quint64 TopoState::mac_of(quint16 tei) const {
    auto it = nodes.constFind(tei);
    return it == nodes.constEnd() ? 0 : it->mac;
}
