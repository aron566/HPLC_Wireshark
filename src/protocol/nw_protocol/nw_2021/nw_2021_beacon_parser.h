/// @file nw_2021_beacon_parser.h
/// @brief 南网 NW_2021 信标帧载荷区解析器
/// @details 输入整帧 MPDU(自 FrameType 起含 16B FCH),输出信标帧载荷区字段树:
///          固定头(信标类型/标志位/组网序列号/短网络标识) + 信标管理信息 +
///          帧载荷校验序列(BPCS,32-bit CRC32) + 保留字节 + 物理块检查序列(24-bit)。
///          字段坐标对照数据链路层报批稿表24/表29 及 Python 参考 MPDU_BEACON_LOAD。
#ifndef NW_2021_BEACON_PARSER_H
#define NW_2021_BEACON_PARSER_H

#include "bplcframe.h"

namespace NW_2021_BeaconParser {

/// @brief 解析南网信标帧载荷区
/// @param payload 整帧 MPDU(payload_for_log,自 FrameType 起含 16B FCH)
/// @param pbsize  信标物理块大小(由 FCH TMI 决定,如 136/520)
/// @return MsduInfo.tree 为字段树(相对载荷区起点,即 payload 偏移 16);
///         present=false 表示载荷不可解析
MsduInfo parse_beacon(const QByteArray& payload, int pbsize);

}  // namespace NW_2021_BeaconParser

#endif // NW_2021_BEACON_PARSER_H
