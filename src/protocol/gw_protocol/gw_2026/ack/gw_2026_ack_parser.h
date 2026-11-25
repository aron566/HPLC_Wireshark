/// @file ackparser.h
/// @brief ACK 帧解析模块声明(选择确认帧:FC ExtFrameType 0=选择确认,其他=保留)
/// @details 载波按表14/表15;无线按表19(无 BRU/SAT ID 标志,byte8 保留,byte9=信号强度)。
#ifndef BPLC_GW_2026_ACK_PARSER_H
#define BPLC_GW_2026_ACK_PARSER_H

#include "bplcframe.h"

namespace gw_2026_ackp {

/// @brief 解析 ACK 帧 FC 字段(相对 FC 起点;ExtFrameType 于 bit(12,0,4))
/// @param p     MPDU 数据指针(自 FrameType 起)
/// @param m     [out] 填充 ack_ext_type 及对应分支字段
/// @param is_rf 介质:true=无线(表19) / false=载波(表14/15)
void parse_fch(const quint8* p, MpduInfo& m, bool is_rf);

}  // namespace gw_2026_ackp

#endif  // BPLC_GW_2026_ACK_PARSER_H
