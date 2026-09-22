/// @file gw_2022_tree.h
/// @brief 国网 GW_2022 协议字段树构建器(IProtocolTreeBuilder 实现)
/// @details 国网双模标准 2022(202203)的 MPDU Base + 各帧型 FCH 字段树。
///          字段坐标/语义与 GW_2022_Parser 解析结果(MpduInfo)严格一致。
#ifndef GW_2022_TREE_H
#define GW_2022_TREE_H

#include "protocol_tree_builder.h"

/// 国网 GW_2022 定界符类型(FCH byte0 bit0-2)
enum class GW_2022_FrameType : quint8 {
    BEACON = 0,  ///< 信标帧
    SOF    = 1,  ///< SOF 帧
    ACK    = 2,  ///< 选择确认帧
    COORD  = 3,  ///< 网间协调帧
};

/// 国网 GW_2022 ACK 扩展帧类型(FCH byte12 bit0-3)
enum class GW_2022_AckExtType : quint8 {
    Normal        = 0,  ///< 常规 ACK
    Search        = 1,  ///< 网络搜索
    Sync          = 2,  ///< 同步
    SwitchChannel = 3,  ///< 无线切频
};

/// 国网双模标准 2022 字段树构建器
class GW_2022_TreeBuilder : public IProtocolTreeBuilder {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }
    void build(QTreeWidgetItem* root, const PacketEntry& e) override;
};

#endif // GW_2022_TREE_H
