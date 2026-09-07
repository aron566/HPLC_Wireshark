/// @file coordparser.h
/// @brief 网间协调帧(COORD)解析模块声明(51242 COORD:FCH 时隙参数)
/// @details 协调帧的 FCH 字段(TimeDuration/NextShift/NeighbourNID/
///          NetRfChannel/NetRfOption),填充 MpduInfo。
#ifndef CORDPARSER_H
#define CORDPARSER_H

#include "bplcframe.h"

namespace coordp {

/// @brief 解析 COORD 帧 FCH 字段(相对 FCH 起点)
/// @param p  MPDU 数据指针(自 FrameType 起)
/// @param m  [out] 填充 coord_* 字段
void parse_fch(const quint8* p, MpduInfo& m);

}  // namespace coordp

#endif // CORDPARSER_H
