/// @file column_gen.h
/// @brief 参考解剖器的 PacketList 列能力声明与逐帧列值生成
/// @details 列集与内置 PacketList 固定列(#/Time/Delta 之后的部分)一一
///          对应,列值逐项复刻 PacketListModel::data 与 MainWindow
///          csv_row 的渲染规则,使插件模式下列表与 CSV 导出和内置模式
///          逐格一致(由 export_consistency_test 逐列校验)。
///          Orig/Source/Destination 的 MAC 后缀依赖全会话 TEI→MAC 学习
///          表:宿主模型在插件模式下拿不到 MSDU 明细字段,无法替插件
///          学习,故由解剖器自持学习表(规则与 PacketListModel::
///          update_tei_mac 一致),dissect_reset 时一并清空。
#ifndef DISSECTOR_COLUMN_GEN_H
#define DISSECTOR_COLUMN_GEN_H

#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

#include "dissect.h"
#include "iprotocolparser.h"   // ParseResult

namespace dissect_gen {

/// @brief TEI→MAC 学习表(NID → TEI → MAC48),与宿主模型同构
using TeiMacMap = QHash<quint32, QHash<quint16, quint64>>;

/// @brief 参考解剖器声明的列集(顺序即显示/导出顺序)
QVector<DissectColumn> reference_columns();

/// @brief 用本帧学习对更新学习表(须在生成本帧列值之前调用,与宿主
///        模型 append 时先 update 后渲染的顺序一致)
void update_tei_mac(TeiMacMap& map, const ParseResult& r);

/// @brief 按本帧解析结果生成全部列值(键为 reference_columns 的列 id)
QMap<QString, QString> reference_column_values(const ParseResult& r,
                                               const TeiMacMap& map);

}  // namespace dissect_gen

#endif  // DISSECTOR_COLUMN_GEN_H
