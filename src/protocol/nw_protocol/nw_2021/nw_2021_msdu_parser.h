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

/// @brief 确认/否认帧业务标识(表9,报文端口 0x11)
enum class NW_2021_AckNackBid : quint8 {
    Confirm = 0x00,  ///< 确认
    Deny    = 0x01,  ///< 否认
};

/// @brief 数据转发帧业务标识(表9)
enum class NW_2021_DataForwardBid : quint8 {
    ToDevice = 0x00,  ///< 数据透传至设备(端口 0x11)
    ToModule = 0x01,  ///< 数据透传至模块(端口 0x13)
};

/// @brief 命令帧业务标识(表9)
enum class NW_2021_CommandBid : quint8 {
    QuerySearchResult    = 0x00,  ///< 查询终端搜索结果(0x11)
    IssueSearchList      = 0x01,  ///< 下发搜索终端列表(0x11)
    FileTransfer         = 0x02,  ///< 文件传输(0x11)
    EnableDisableNodeEv  = 0x03,  ///< 允许/禁止从节点事件(0x11)
    NodeRestart          = 0x04,  ///< 从节点重启(0x11)
    NodeInfoQuery        = 0x05,  ///< 从节点信息查询(0x11)
    IssueCommAddrMap     = 0x06,  ///< 下发通信地址映射表列表(0x11)
    QueryNodeRunStatus   = 0x07,  ///< 查询从节点运行状态信息(0x13)
    QueryNodeChannelInfo = 0x08,  ///< 查询从节点信道信息(0x13)
    PhaseIdent           = 0x10,  ///< 台区户变关系/相位识别(0x11)
    TestFrame            = 0xF0,  ///< 测试帧(0x11)
};

/// @brief 主动上报帧业务标识(表9)
enum class NW_2021_EventReportBid : quint8 {
    MeterEvent  = 0x00,  ///< 电表事件主动上报(0x11)
    PowerOnOff  = 0x01,  ///< 停上电事件上报(0x13)
    DeviceEvent = 0x02,  ///< 设备事件主动上报(0x11)/通信模块事件上报(0x13)
};

/// @brief 抄控器协议帧业务标识(表9,报文端口 0x11)
enum class NW_2021_ReaderFrameBid : quint8 {
    CcoProtocol   = 0x00,  ///< 抄控器-CCO 协议
    SerialForward = 0x01,  ///< 数据透传串口转发
};

/// @brief 广播命令帧业务标识(表9)
enum class NW_2021_BroadcastCmdBid : quint8 {
    NodeRestart          = 0x04,  ///< 从节点重启(0x11)
    NodeInfoQuery        = 0x05,  ///< 从节点信息查询(0x11)
    QueryNodeRunStatus   = 0x07,  ///< 查询从节点运行状态信息(0x13)
    QueryNodeChannelInfo = 0x08,  ///< 查询从节点信道信息(0x13)
};

/// @brief 南网 MSDU 解析器(纯静态,无状态)
class NW_2021_MsduParser {
public:
    /// @brief 解析完整 MAC 帧(MAC 帧头起,含尾 CRC32)
    /// @param body  重组后的完整 MAC 帧(MSDU_BASE 起)
    static MsduInfo parse(const QByteArray& body);
};

#endif // NW_2021_MSDU_PARSER_H
