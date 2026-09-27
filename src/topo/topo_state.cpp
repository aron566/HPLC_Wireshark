/// @file topo_state.cpp
/// @brief 拓扑状态模型实现(节点/路由累积 + 层级计算)
#include "topo_state.h"
#include "i18n.h"

namespace {

/// @brief TopoState 内部生成的事件说明英文注册(与各 parser 的 register_en 解耦,本模块自维护)
struct I18nRegTopoState { I18nRegTopoState() {
    trl::register_en("CCO重启: 重启次数 %1→%2,拓扑已清空", "CCO reboot: restart count %1→%2, topology cleared");
} } i18n_reg_topo_state;

/// @brief 事件类型 → 节点入网状态(发起关联请求=正在入网,其余在网事件=已入网)
NodeStatus status_for(TopoEventKind k) {
    switch (k) {
        case TopoEventKind::AssocReq: return NodeStatus::OnlineGoing;
        default:                      return NodeStatus::Online;
    }
}

} // namespace

void TopoState::index_mac(quint16 tei, quint64 mac) {
    if (!tei || !mac) return;
    // 情况1:该 MAC 之前映射到别的 TEI(TEI 复用/STA 重入网换 TEI)
    //       → 把旧 TEI 节点的 MAC 清零,避免之后按 MAC 查找时 stale 命中
    auto it = mac_to_tei.find(mac);
    if (it != mac_to_tei.end() && it.value() != tei) {
        const quint16 old_tei = it.value();
        auto nit = nodes.find(old_tei);
        if (nit != nodes.end() && nit.value().mac == mac)
            nit.value().mac = 0;
    }
    // 情况2:该 TEI 之前是别的 MAC(同一 TEI 换 MAC)
    //       → 若旧 MAC 仍指向本 TEI 则删除其映射,避免旧 MAC 离线时误伤本 TEI
    auto nit = nodes.find(tei);
    if (nit != nodes.end()) {
        const quint64 old_mac = nit.value().mac;
        if (old_mac && old_mac != mac) {
            auto oit = mac_to_tei.find(old_mac);
            if (oit != mac_to_tei.end() && oit.value() == tei)
                mac_to_tei.erase(oit);
        }
    }
    mac_to_tei[mac] = tei;
}

bool TopoState::remove_node_by_mac(quint64 mac) {
    if (!mac) return false;
    const auto iit = mac_to_tei.find(mac);
    if (iit == mac_to_tei.end()) return false;
    const quint16 old_tei = iit.value();
    if (old_tei == 1) return false;  // CCO 永不移除
    const auto nit = nodes.find(old_tei);
    if (nit == nodes.end() || nit.value().mac != mac) {
        mac_to_tei.erase(iit);  // 坏索引:删映射(与离线路径一致的回退策略)
        return false;
    }
    nodes.erase(nit);
    mac_to_tei.erase(iit);
    comm_rates.remove(old_tei);  // 旧 TEI 的成功率残留一并清理
    return true;
}

void TopoState::apply(const TopoEvent& e) {
    nid = e.nid;
    // CCO 重启检测:发现列表携带的发送方重启次数若发生变化,说明 CCO 已重启,
    // 旧 TEI 分配不再准确 → 先清空当前拓扑与 TEI-MAC 映射,再应用本帧的新状态。
    // (重启次数为 4-bit 计数,任何变化即视为重启,天然处理 15→0 回绕)
    if (e.kind == TopoEventKind::DiscoverList && e.restart_count >= 0) {
        if (m_last_restart_count >= 0 && e.restart_count != m_last_restart_count) {
            nodes.clear();
            pending.clear();
            mac_to_tei.clear();
            comm_rates.clear();
            cco_mac = 0;
            // 该发现列表带动了 TOPO 变化:记入路由变更表,标记为 CCO 重启事件
            TopoEvent re = e;
            re.kind = TopoEventKind::CcoRestart;
            re.desc = trl::L("CCO重启: 重启次数 %1→%2,拓扑已清空")
                          .arg(m_last_restart_count).arg(e.restart_count);
            events.append(re);
        }
        m_last_restart_count = e.restart_count;
    }
    // CCO(TEI=1):根节点,父=0,始终已入网
    if (e.cco_mac) {
        cco_mac = e.cco_mac;
        index_mac(1, e.cco_mac);
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
            // 正在入网(TEI 未分配):按 MAC 暂存 pending(关联请求阶段)。
            // 该 MAC 若已在拓扑中(设备重上电再次发起关联):先移除旧节点,
            // 再绘制当前"入网中"状态,避免同一 MAC 以"在线"+"入网中"
            // 两种状态同时出现在拓扑图中。
            remove_node_by_mac(p.mac);
            TopoNode n;
            n.tei = 0;
            n.mac = p.mac;
            n.status = NodeStatus::OnlineGoing;
            n.last_seen_ms = e.epoch_ms;
            n.is_rf = e.is_rf;
            pending[p.mac] = n;
            continue;
        }
        index_mac(p.tei, p.mac);
        TopoNode& n = nodes[p.tei];
        n.tei = p.tei;
        n.mac = p.mac;
        n.status = status_for(e.kind);
        n.last_seen_ms = e.epoch_ms;
        n.is_rf = e.is_rf;
        // 若该 MAC 之前正在入网(关联请求),现已分配到 TEI → 移除 pending。
        // pending 的 key 恒等于 TopoNode.mac(写入处 pending[p.mac]=n),等价于哈希直接删除。
        pending.remove(p.mac);
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
    // 离网节点(按 MAC 匹配,标记离线):先走 MAC→TEI 索引 O(1),索引不一致时回退线性扫描
    for (quint64 mac : e.leaves) {
        if (!mac) continue;
        bool done = false;
        auto iit = mac_to_tei.find(mac);
        if (iit != mac_to_tei.end()) {
            auto nit = nodes.find(iit.value());
            if (nit != nodes.end() && nit.value().mac == mac) {
                nit.value().status = NodeStatus::Offline;
                nit.value().last_seen_ms = e.epoch_ms;
                done = true;
            } else {
                // 索引与节点不一致(不应发生):删除坏映射,回退线性扫描保证不错杀/不漏杀
                mac_to_tei.erase(iit);
            }
        }
        if (!done) {
            for (auto it = nodes.begin(); it != nodes.end(); ++it) {
                if (it.value().mac == mac) {
                    it.value().status = NodeStatus::Offline;
                    it.value().last_seen_ms = e.epoch_ms;
                    break;
                }
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
        case TopoEventKind::AssocGatherInd:
        case TopoEventKind::AssocInd:
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
