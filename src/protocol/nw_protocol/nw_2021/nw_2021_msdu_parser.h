/// @file nw_2021_msdu_parser.h
/// @brief 南网 NW_2021 MSDU/MAC 层解析器(头解析,管理消息后续补)
/// @details 输入 SOF 重组出的 MAC 帧(MAC 帧头 + MSDU + CRC32),解析:
///          - MAC 帧头(MSDU_BASE 32B 长/12B 短):TEI/SNID/重启次数/广播方向/目的 MAC
///          - MSDU 帧头(MSDU_LONGHEAD 18B / SHORTHEAD 2B):MAC 48b + VLAN + MSDU 类型
///          字段坐标移植自 MSDU_Class.py(MSDU_BASE/MSDU_LONGHEAD/MSDU_SHORTHEAD)。
#ifndef NW_2021_MSDU_PARSER_H
#define NW_2021_MSDU_PARSER_H

#include "bplcframe.h"

/// @brief 南网 NW_2021 应用层帧类型域(业务报文头控制域 bit0-3,表4)
enum class NW_2021_PacketType : quint8 {
    AckNack       = 0x0,  ///< 确认/否认
    DataForward   = 0x1,  ///< 数据转发帧
    Command       = 0x2,  ///< 命令帧
    EventReport   = 0x3,  ///< 主动上报帧
    ReaderFrame   = 0x4,  ///< 抄控器相关协议
    BroadcastCmd  = 0x5,  ///< 广播命令帧
    DataSubscribe = 0x6,  ///< 数据订阅路由帧
    Test          = 0xE,  ///< 厂家调试帧
    FactoryFrame  = 0xF,  ///< 厂测帧
};

/// @brief 南网 MSDU 解析器(纯静态,无状态)
class NW_2021_MsduParser {
public:
    /// @brief 解析完整 MAC 帧(MAC 帧头起,含尾 CRC32)
    /// @param body  重组后的完整 MAC 帧(MSDU_BASE 起)
    static MsduInfo parse(const QByteArray& body);
};

#endif // NW_2021_MSDU_PARSER_H
