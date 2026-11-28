/// @file nw_2021_frametype.h
/// @brief 南网 NW_2021 定界符类型 / ACK 扩展帧类型(纯 QtCore 枚举,与字段树解耦)
/// @details 原定义在 nw_2021_tree.h(带 QTreeWidgetItem),解析器为取两个枚举被迫
///          链接 QtWidgets;现拆到独立头,nw_2021_parser.cpp 只含本头。
///          取值与规约一致:FrameType 表11(BEACON/SOF/ACK/COORD=0/1/2/3);
///          AckExtType 表21 仅定义 0/1/2(Normal/Search/Sync),其余保留——
///          南网无 SwitchChannel(main 9d3b9e0 已按规约删除,此处绝不带回)。
#ifndef BPLC_NW_2021_FRAMETYPE_H
#define BPLC_NW_2021_FRAMETYPE_H

#include <QtGlobal>

/// 南网 NW_2021 定界符类型(FCH byte0 bit0-2,表11)
enum class NW_2021_FrameType : quint8 {
    BEACON = 0,  ///< 信标帧
    SOF    = 1,  ///< SOF 帧
    ACK    = 2,  ///< 选择确认帧
    COORD  = 3,  ///< 网间协调帧
};

/// 南网 NW_2021 ACK 扩展帧类型(FCH byte12 bit0-3,表21)
/// @note 表21 仅定义 0/1/2,其余为保留;南网无国网 GW_2022 的"无线切频"扩展类型。
enum class NW_2021_AckExtType : quint8 {
    Normal = 0,  ///< 选择确认帧
    Search = 1,  ///< 网络搜索帧(抄控器)
    Sync   = 2,  ///< 同步帧(抄控器)
};

#endif  // BPLC_NW_2021_FRAMETYPE_H
