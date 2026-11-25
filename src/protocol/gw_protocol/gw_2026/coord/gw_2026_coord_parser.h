/// @file coordparser.h
/// @brief 网间协调帧(COORD)解析模块声明(V6:COORD 时隙参数)
/// @brief 协调帧的 FC 字段(TimeDuration/BandStartOffset/NeighbourNID(16b)/
///          NetRfChannel/NetRfOption+RSV,表16),填充 MpduInfo。
///          byte12 bit0-3 整体存 coord_rsv0(表16:bit0-1=NetRfOption,bit2-3=RSV),
///          字段树层拆 (12,0,2)/(12,2,2) 渲染。
#ifndef BPLC_GW_2026_COORD_PARSER_H
#define BPLC_GW_2026_COORD_PARSER_H

#include "bplcframe.h"

namespace gw_2026_coordp {

/// @brief 解析 COORD 帧 FC 字段(相对 FC 起点)
/// @param p  MPDU 数据指针(自 FrameType 起)
/// @param m  [out] 填充 coord_* 字段
void parse_fch(const quint8* p, MpduInfo& m);

}  // namespace gw_2026_coordp

#endif  // BPLC_GW_2026_COORD_PARSER_H
