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
    bool    is_rf = false;            ///< 首次观察到该 TEI 时帧的物理介质(false=载波 true=RF);
                                      ///  仅首次写入并锁定,不表示入网链路 —— 入网链路看 link_type;
                                      ///  双模标记看 rf_capable
    bool    is_rf_known = false;      ///< is_rf 是否已判定(方案②:仅首次/未知时写入,防事件帧外高频重写)
    int     rssi_dbm = 0;             ///< 无线信号强度(dBm 估计,档位下限;RF 发现列表信道信息)
    bool    rssi_known = false;       ///< rssi_dbm 是否有效(RF 帧携带过信号强度)
    qint16  proxy_up_rate = -1;       ///< 代理上行接收率%(表217/表126 站点属性;-1=无)
    qint16  proxy_down_rate = -1;     ///< 代理下行接收率%(表217/表126)
    qint16  min_link_rate = -1;       ///< 链路最小接收率%(表217/表126)
    quint8  rf_hop = 0;               ///< RF 跳数(表217/表126 byte8 高4bit)
    quint8  rf_role = 0;              ///< 角色(表217/表126 byte7 高4bit;0未知/1站点/2代理站点/4CCO)
    bool    proxy_rate_known = false; ///< 站点属性速率是否有效
    quint8  link_type = 0xFF;         ///< 入网链路类型(表79/表160:0=载波链路 1=无线链路 0xFF=未定)
    bool    link_type_known = false;  ///< 入网链路是否已确定(权威帧赋值后锁定,不被后续帧翻转)
    bool    rf_capable = false;       ///< 双模标记:该 TEI 作为发送方发过 RF 帧(置位后不清除)
    qint32  proxy_comm_rate = -1;      ///< 与代理站点通信成功率(表77 byte16-19,32bit;0-100%;-1=无)
    qint32  proxy_down_comm_rate = -1; ///< 与代理站点下行通信成功率(表77 byte20-23,32bit)
    qint16  min_comm_rate = -1;        ///< 最小通信成功率(表77 byte36,8bit)
    bool    comm_rate_known = false;   ///< 来自 0x0055 载波发现列表(独立于 RFDL proxy_* 与 0x005E comm_rates)

    /// @brief 是否在线(非离线)
    bool online() const { return status != NodeStatus::Offline; }
};

/// @brief 单个 NID 网络的拓扑状态(节点 + 路由 + 事件历史)
class TopoState {
public:
    quint32 nid = 0;
    quint64 cco_mac = 0;
    qint64 first_seen_ms = 0;     ///< 首次识别到该 NID 的帧时间戳(epoch ms;0=未知)
    qint64 first_seen_frame = -1; ///< 首次识别到该 NID 的帧序号(-1=未知)
    QHash<quint16, TopoNode> nodes; ///< TEI → 节点(含 CCO TEI=1)
    QHash<quint64, TopoNode> pending; ///< MAC → 正在入网节点(TEI 未分配,关联请求阶段)
    QHash<quint64, quint16> mac_to_tei; ///< MAC → TEI 快速索引(离线标记按 MAC 查找;与 nodes 双向一致)
    QHash<quint16, CommRateInfo> comm_rates; ///< TEI → 通讯成功率(成功率上报)
    QHash<quint16, QVector<quint16>> neighbors; ///< TEI → 邻居 TEI 列表(发现列表位图,按帧更新)
    QHash<quint16, QHash<quint16, int>> neighbor_counts; ///< STA TEI → (邻居 TEI → 发现列表接收数,累计)
    QHash<quint16, QHash<quint16, quint8>> neighbor_rates; ///< 上报方 STA → (邻居 → 接收率%·方向=邻居→上报方,即上行)
    QHash<quint16, QHash<quint16, qint16>> neighbor_snrs;  ///< 上报方 STA → (邻居 → SNR dB)
    QVector<TopoEvent> events;      ///< 路由变更事件(时间序,供底部表格)

    /// @brief 应用一个拓扑事件,增量更新节点/路由/入网状态并记录事件
        /// @brief 逐帧接入方式兜底(方案②):仅对"已存在且接入方式未确定"的节点写入
    /// @note  不建占位节点(避免数据帧/广播帧在拓扑里凭空生成 TEI 节点);不覆盖已确定值(防双模抖动)
    /// @return 是否实际发生写入(UI 刷新节流用)
    bool note_frame_medium(quint16 tei, bool is_rf) {
        if (!tei || tei == 0xFFFF) return false;
        auto it = nodes.find(tei);
        if (it == nodes.end()) return false;   // 节点不存在 → 不建(等事件帧创建)
        if (it->is_rf_known) return false;     // 已确定 → 不覆盖(双模设备会来回跳)
        it->is_rf = is_rf;
        it->is_rf_known = true;
        return true;
    }
    /// @brief 双模标记:该 TEI 作为发送方发过 RF 帧则置 rf_capable(置位后永不清除)
    /// @note  判据取"发过"而非"收发过"(收到 RF 帧不能证明该节点有 RF 发射能力)
    /// @return 是否实际发生写入(UI 刷新节流用)
    bool note_rf_capable(quint16 src_tei, bool is_rf) {
        if (!src_tei || src_tei == 0xFFFF || !is_rf) return false;
        auto it = nodes.find(src_tei);
        if (it == nodes.end() || it->rf_capable) return false;   // 已置位不清除
        it->rf_capable = true;
        return true;
    }
void apply(const TopoEvent& e);

    /// @brief 应用 RFDL(单跳帧)邻居信道信息:接收率/SNR 按上报方 STA 键控,rssi 更新节点属性
    /// @note 单跳帧 topo_event.kind=Other(不触发 apply()),其信道信息单独走此路径;
    ///       上报方 TEI 取自帧 FCH SrcTEI(单跳无中继,源=发送方)
    void apply_channel_info(quint16 reporter_tei, const TopoEvent& e);

    /// @brief 计算各节点层级(从 CCO=0 沿父链向下;不可达=-1)
    QHash<quint16, int> compute_levels() const;

    /// @brief 取节点 MAC(TEI 未知返回 0)
    quint64 mac_of(quint16 tei) const;

private:
    /// @brief 维护 mac_to_tei 双向一致(处理"同一 MAC 先后对应不同 TEI"和"同一 TEI 先后对应不同 MAC")
    void index_mac(quint16 tei, quint64 mac);
    /// @brief 按 MAC 移除已存在的拓扑节点(设备重上电再次发起关联时先清旧状态)
    /// @details CCO(TEI=1)永不移除;同时清理该 TEI 的成功率残留与 MAC 索引。
    /// @return 是否移除了节点
    bool remove_node_by_mac(quint64 mac);

    int m_last_restart_count = -1; ///< 上次发现列表的发送方重启次数(-1=尚无基线;CCO 重启检测用)
    QHash<quint64, int> m_sta_restart_count; ///< STA MAC → 上次重启次数(-1=尚无基线;STA 重启检测用,仅关联请求更新)
};

#endif // TOPO_STATE_H
