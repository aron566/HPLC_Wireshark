/// @file nw_2021_tree.h
/// @brief 南网 NW_2021 协议字段树构建器(IProtocolTreeBuilder 实现)
/// @details 南网双模 2021 报批版的 MPDU Base + 各帧型 FCH 字段树。
///          字段坐标/语义与 NW_2021_Parser 解析结果(MpduInfo)严格一致。
#ifndef NW_2021_TREE_H
#define NW_2021_TREE_H

#include "protocol_tree_builder.h"
#include "nw_2021_frametype.h"

/// 南网双模 2021 报批版字段树构建器
class NW_2021_TreeBuilder : public IProtocolTreeBuilder {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::NW_2021; }
    void build(QTreeWidgetItem* root, const PacketEntry& e) override;
};

#endif // NW_2021_TREE_H
