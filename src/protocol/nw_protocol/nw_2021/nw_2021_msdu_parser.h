/// @file nw_2021_msdu_parser.h
/// @brief 南网 NW_2021 MSDU/MAC 层解析器(头解析,管理消息后续补)
/// @details 输入 SOF 重组出的 MAC 帧(MAC 帧头 + MSDU + CRC32),解析:
///          - MAC 帧头(MSDU_BASE 32B 长/12B 短):TEI/SNID/重启次数/广播方向/目的 MAC
///          - MSDU 帧头(MSDU_LONGHEAD 18B / SHORTHEAD 2B):MAC 48b + VLAN + MSDU 类型
///          字段坐标移植自 MSDU_Class.py(MSDU_BASE/MSDU_LONGHEAD/MSDU_SHORTHEAD)。
#ifndef NW_2021_MSDU_PARSER_H
#define NW_2021_MSDU_PARSER_H

#include "bplcframe.h"

/// @brief 南网 MSDU 解析器(纯静态,无状态)
class NW_2021_MsduParser {
public:
    /// @brief 解析完整 MAC 帧(MAC 帧头起,含尾 CRC32)
    /// @param body  重组后的完整 MAC 帧(MSDU_BASE 起)
    static MsduInfo parse(const QByteArray& body);
};

#endif // NW_2021_MSDU_PARSER_H
