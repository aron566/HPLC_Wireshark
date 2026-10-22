/// @file nw_2026_pb_table.h
/// @brief 南网 NW_2026 协议参数:PB 块大小查表(载波 TMI / SPLC TMI编号 / 无线载荷 PB 大小)
/// @details 通感一体化 SPLC 数据链路层(试行)物理层表13 ROBO 模式 + 数据链路层表23:
///   - BPLC 版本(标准版本号=1)载波路径:TMI(载波映射表索引)→ PB size,取值 0-14;
///     TMI 13-14 转查 TMI_EXT
///   - SPLC 版本(标准版本号=2)载波路径:TMI编号(表23 byte6 bit1-5)→ PB size,
///     Bitloading标识=0 用 nw_2026_splc_pb_size,=1 用 nw_2026_splc_bitload_pb_size
///   - 无线路径:载荷 PB 大小(PBLen,表130)→ PB size,0-5 → 16/40/72/136/264/520
/// @note 参考 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG/MPDU_Class.py
///       (MPDU_BASE::get_pbsize / get_RFpbsize / MPDU_SOF_SPLC::get_pbsize_splc)。
///       与国网独立,勿共享。
#ifndef NW_2026_PB_TABLE_H
#define NW_2026_PB_TABLE_H

#include <QtGlobal>

/// 南网 BPLC 版载波路径 TMI → PB 块大小(字节)。-1 = TMI/TMI_EXT 无效
inline int nw_2026_pb_size(quint8 tmi, quint8 tmi_ext) {
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
inline int nw_2026_rf_pb_size(quint8 pblen) {
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

/// 南网 SPLC 版(标准版本号=2)载波 TMI编号 → PB 块大小(字节,表23/物理层表13):
/// 0-8→520 / 9-15→264 / 16-20→136 / 21-25→72 / 26-27→16。Bitloading标识=0 时使用。
inline int nw_2026_splc_pb_size(quint8 tmi_idx) {
    if (tmi_idx <= 8)  return 520;
    if (tmi_idx <= 15) return 264;
    if (tmi_idx <= 20) return 136;
    if (tmi_idx <= 25) return 72;
    if (tmi_idx <= 27) return 16;
    return -1;
}

/// 南网 SPLC 版 Bitloading标识=1 时 TMI编号 → 物理块大小(表23:0→136/1→520/2→72/3→264)。
inline int nw_2026_splc_bitload_pb_size(quint8 idx) {
    switch (idx) {
        case 0: return 136;
        case 1: return 520;
        case 2: return 72;
        case 3: return 264;
        default: return -1;
    }
}

#endif // NW_2026_PB_TABLE_H
