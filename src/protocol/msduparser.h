/// @file msduparser.h
/// @brief MSDU/MAC 层完整字段解析器(移植自 BPLCMonitor/MSDU_Class.py)
/// @details 输入 SOF 重组出的完整 MSDU 载荷(含 MSDU_BASE 头 + 体 + CRC32),
///          输出结构化的 MsduInfo 字段树(节点定义在 bplcframe.h)。
///          覆盖回放数据实际出现的类型:
///            MSDUType 0(网管):AssocReq/AssocCnf/ChangeProxyReq/
///                              ChangeProxyBitMapCnf/HeartBeatCheck/
///                              DiscoverNodeList/SuccessRateReport
///            MSDUType 48(应用):APP_BASE + PacketID 0x0008 EventPacket
#ifndef MSDUPARSER_H
#define MSDUPARSER_H

#include "bplcframe.h"

/// @brief MSDU 解析器(纯静态,无状态)
class MsduParser {
public:
    /// @brief 解析完整 MSDU
    /// @param body  重组后的完整 MSDU(MSDU_BASE 起,含尾 CRC32)
    static MsduInfo parse(const QByteArray& body);
};

#endif // MSDUPARSER_H
