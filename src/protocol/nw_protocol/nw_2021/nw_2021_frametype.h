/// @file nw_2021_frametype.h
/// @brief 南网 NW_2021 帧类型枚举(独立头文件,不含 QtWidgets 依赖)
/// @details 从 nw_2021_tree.h 拆出:解析器(纯 QtCore)与字段树构建器
///          (QtWidgets)共用这些枚举,避免解析器为取枚举被迫链接 QtWidgets。
#ifndef NW_2021_FRAMETYPE_H
#define NW_2021_FRAMETYPE_H

#include <QtGlobal>

/// 南网 NW_2021 定界符类型(FCH byte0 bit0-2,表11)
enum class NW_2021_FrameType : quint8 {
    BEACON = 0,  ///< 信标帧
    SOF    = 1,  ///< SOF 帧
    ACK    = 2,  ///< 选择确认帧
    COORD  = 3,  ///< 网间协调帧
};

/// 南网 NW_2021 ACK 扩展帧类型(FCH byte12 bit0-3)
enum class NW_2021_AckExtType : quint8 {
    Normal        = 0,  ///< 常规 ACK
    Search        = 1,  ///< 网络搜索
    Sync          = 2,  ///< 同步
    SwitchChannel = 3,  ///< 无线切频
};

#endif // NW_2021_FRAMETYPE_H
