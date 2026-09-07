/// @file ackparser.h
/// @brief ACK 帧解析模块声明(51242 ACK:FCH ExtFrameType 0-3 分支)
/// @details 常规/搜索/同步/切频四类 ACK 的 FCH 字段解析,填充 MpduInfo。
#ifndef ACKPARSER_H
#define ACKPARSER_H

#include "bplcframe.h"

namespace ackp {

/// @brief 解析 ACK 帧 FCH 字段(相对 FCH 起点;ExtFrameType 于 bit(12,0,4))
/// @param p  MPDU 数据指针(自 FrameType 起)
/// @param m  [out] 填充 ack_ext_type 及对应分支字段
void parse_fch(const quint8* p, MpduInfo& m);

}  // namespace ackp

#endif // ACKPARSER_H
