/// @file beaconparser.h
/// @brief BEACON 帧载荷区解析器(移植自 BPLCMonitor/MPDU_Class.py)
/// @details 输入 MPDU(payload_for_log,自 FrameType 起),输出载荷区字段树:
///          载荷固定头(BeaconType/标志位/NetSN/CCO MAC/周期计数/RF 参数)
///          + BeaconMgrInfo 管理条目(STA Cap / Route Param / TSA 等)。
///          字段树复用 MsduFieldNode/MsduInfo(定义在 bplcframe.h)。
#ifndef BEACONPARSER_H
#define BEACONPARSER_H

#include "bplcframe.h"

/// @brief BEACON 载荷解析器(纯静态,无状态)
class BeaconParser {
public:
    /// @brief 解析 BEACON 帧载荷区
    /// @param payload_for_log 整帧 MPDU(payload_for_log,自 FrameType 起含 16B FCH)
    /// @return MsduInfo.tree 为字段树;present=false 表示载荷不可解析
    static MsduInfo parse_beacon(const QByteArray& payload_for_log);
};

#endif // BEACONPARSER_H
