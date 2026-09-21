/// @file nw_2021_pb_table.h
/// @brief 南网 NW_2021 协议参数:PB 块大小查表(载波 TMI / 无线载荷 PB 大小)
/// @details 南网双模 2021 报批版,与国网 GW_2022 的 PB 定义不同:
///   - 载波路径:TMI(载波映射表索引)→ PB size,取值 0-14;TMI 13-14 转查 TMI_EXT
///   - 无线路径:载荷 PB 大小(PBLen,表130)→ PB size,0-5 → 16/40/72/136/264/520
/// @note 参考 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG/MPDU_Class.py
///       (MPDU_BASE::get_pbsize / get_RFpbsize)。与国网独立,勿共享。
#ifndef NW_2021_PB_TABLE_H
#define NW_2021_PB_TABLE_H

#include <QtGlobal>

/// 南网载波路径 TMI → PB 块大小(字节)。-1 = TMI/TMI_EXT 无效
inline int nw_2021_pb_size(quint8 tmi, quint8 tmi_ext) {
    if (tmi == 0 || tmi == 1)                          return 520;
    if (tmi >= 2 && tmi <= 6)                          return 136;
    if (tmi >= 7 && tmi <= 10)                         return 520;
    if (tmi == 11 || tmi == 12)                        return 264;
    // TMI 13-14:转查扩展 TMI_EXT
    if (tmi_ext >= 1 && tmi_ext <= 6)                  return 520;
    if (tmi_ext >= 10 && tmi_ext <= 14)                return 136;
    return -1;
}

/// 南网无线路径「载荷 PB 大小」(表130)→ PB 块大小(字节)。-1 = 无效
inline int nw_2021_rf_pb_size(quint8 pblen) {
    switch (pblen) {
        case 0: return 16;
        case 1: return 40;
        case 2: return 72;
        case 3: return 136;
        case 4: return 264;
        case 5: return 520;
        default: return -1;
    }
}

#endif // NW_2021_PB_TABLE_H
