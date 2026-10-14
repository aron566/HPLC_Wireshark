/// @file color_gen.h
/// @brief 参考解剖器的报文着色配置声明与逐帧规则选择
/// @details 默认清单逐项复刻主界面当前着色配置(PacketListModel 的
///          ForegroundRole 优先级与配色):管理消息 MMe 按类型逐一列项
///          (暖色系哈希公式)、应用层 BID 按值域逐一列项(冷色系哈希
///          公式,确认/否认另有语义色)、其余按帧类型基色。逐帧选择
///          逻辑与宿主内置优先级同序:MMe > BID > 帧类型。
#ifndef DISSECTOR_COLOR_GEN_H
#define DISSECTOR_COLOR_GEN_H

#include <QString>
#include <QVector>

#include "dissect.h"
#include "iprotocolparser.h"   // ParseResult

namespace dissect_gen {

/// @brief 参考解剖器声明的着色配置(顺序无语义,按 id 查表)
QVector<DissectColorRule> reference_color_rules();

/// @brief 按本帧解析结果选择命中的着色规则 id(与宿主内置着色
///        优先级逐项对应;返回的 id 必在 reference_color_rules 内,
///        协议未定义取值域外的 MMe 类型除外——宿主回退内置逻辑)
QString reference_color_id(const ParseResult& r);

}  // namespace dissect_gen

#endif  // DISSECTOR_COLOR_GEN_H
