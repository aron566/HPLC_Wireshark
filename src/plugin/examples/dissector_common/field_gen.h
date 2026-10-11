/// @file field_gen.h
/// @brief 参考解剖器的通用字段树生成器（数据版字段树构建器）
/// @details 与主程序 gw_2022_tree.cpp / nw_2021_tree.cpp 的构建逻辑逐项
///          对齐（同名、同值、同字节区间），只是产物为 DissectField 数据
///          而非 QTreeWidgetItem；一致性由 dissect_compare_test 同帧对比
///          逐行校验。坐标系与 MsduRawMap 映射规则与 protocol_tree_builder
///          的 tree_msdu_raw_ranges 保持一致（单字段取首片段为主区间）。
#ifndef DISSECTOR_FIELD_GEN_H
#define DISSECTOR_FIELD_GEN_H

#include <QByteArray>
#include <QVector>

#include "dissect.h"
#include "iprotocolparser.h"   // ParseResult

namespace dissect_gen {

/// @brief 国网 GW_2022 字段树（顶层：MPDU Base + 帧型段 + MSDU 段）
QVector<DissectField> gw_fields(const QByteArray& raw, const ParseResult& r);

/// @brief 南网 NW_2021 字段树（同上结构）
QVector<DissectField> nw_fields(const QByteArray& raw, const ParseResult& r);

}  // namespace dissect_gen

#endif  // DISSECTOR_FIELD_GEN_H
