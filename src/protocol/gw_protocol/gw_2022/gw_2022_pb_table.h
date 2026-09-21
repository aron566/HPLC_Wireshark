/// @file gw_2022_pb_table.h
/// @brief 国网 GW_2022 协议参数:TMI → PB 块大小查表
/// @details 国网 HPLC TMI(分级拷贝基本模式)决定物理块大小,取值 0-14:
///   TMI 0-1 → 520;2-6 → 136;7-10 → 520;11-12 → 264;13-14 → 72。
///   信标/单块帧同样按 FCH TMI 查表;-1 = TMI 无效。
/// @note 南网 NW_2021 的 PB 大小定义不同(见 nw_protocol/nw_2021/nw_2021_pb_table.h),
///       故本表按协议+版本独立,不与其他协议共享。
#ifndef GW_2022_PB_TABLE_H
#define GW_2022_PB_TABLE_H

#include <QtGlobal>

/// 国网 TMI → PB 块大小(字节)。-1 = TMI 无效
inline int gw_2022_pb_size(quint8 tmi) {
    if (tmi == 0 || tmi == 1)       return 520;
    if (tmi >= 2 && tmi <= 6)       return 136;
    if (tmi >= 7 && tmi <= 10)      return 520;
    if (tmi == 11 || tmi == 12)     return 264;
    if (tmi == 13 || tmi == 14)     return 72;
    return -1;
}

#endif // GW_2022_PB_TABLE_H
