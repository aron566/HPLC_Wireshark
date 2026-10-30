/// @file nw_2021_pb_table.h
/// @brief 南网 NW_2021 协议参数:PB 块大小查表(载波 TMI / 无线载荷 PB 大小)
/// @details 南网双模 2021 报批版,与国网 GW_2022 的 PB 定义不同:
///   - 载波路径:TMI(载波映射表索引)→ PB size,取值 0-14;TMI 13-14 转查 TMI_EXT
///   - 无线路径:载荷 PB 大小(PBLen,表130)→ PB size,0-5 → 16/40/72/136/264/520
/// @note PB 大小映射见协议表(载波 TMI 映射表 / 无线 PBLen 表130)。与国网独立,勿共享。
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

/// 南网 BPLC 各频段(PL band 0-2)× 载波 TMI/TMI_EXT 支持的最大 PB 块数量。
/// 列索引 0-12 = TMI 0-12(主列);列 13 = TMI 13 直用(全 0=非法);列 14-27 =
/// TMI_EXT 1-14(TMI=0xD/13 触发扩展,表121 j)节:载荷用扩展载波映射表索引译码)。
/// 0 = 该组合不支持(拒收)。
/// @note BPLC 版载波 SOF(表121)帧内无频段字段,band 为网络级配置,
///       帧级校验无法取得 → 本表暂仅作参考记录,未接入解析门限。
inline constexpr quint8 nw_2021_pb_num_table[3][28] = {
    //      0  1  2  3  4  5  6  7  8  9 10 11 12 13 | 1  2  3  4  5  6  7  8  9 10 11 12 13 14 (TMI_EXT)
    { 1, 4, 0, 4, 4, 4, 4, 1, 1, 1, 4, 4, 4, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4 },  // PL band0
    { 1, 4, 0, 2, 4, 4, 4, 1, 1, 1, 3, 4, 2, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4 },  // PL band1
    { 2, 4, 4, 1, 2, 3, 4, 0, 1, 1, 2, 2, 1, 0, 4, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4 },  // PL band2
};

/// 查南网 BPLC 某频段 + TMI/TMI_EXT 支持的最大 PB 块数量。返回 -1 = 配置无效。
/// 扩展触发口径与 nw_2021_pb_size 一致对齐协议(表121 j)节):TMI=0xD/13 时
/// 载荷按 TMI_EXT 译码;TMI>=14 主表无定义,直接无效。
inline int nw_2021_max_pb_num(quint8 band, quint8 tmi, quint8 tmi_ext) {
    if (band >= 3) return -1;                        // 频段越界(0-2)
    int col;
    if (tmi <= 12) {
        col = tmi;                                   // TMI 主列(0-12)
    } else if (tmi == 13 && tmi_ext >= 1 && tmi_ext <= 14) {
        col = 14 + (tmi_ext - 1);                    // TMI=13 触发扩展 → TMI_EXT 列
    } else {
        return -1;                                   // TMI 14+(无主列/未定义扩展)或 TMI=13 无有效 TMI_EXT
    }
    return nw_2021_pb_num_table[band][col];
}

/// 帧级可用的保守上限:BPLC 载波 SOF(表121)帧内无频段字段,无法按 band 精确查,
/// 取三频段逐列最大值 —— 只拦"任何频段都不允许"的组合(如 TMI=2 直用、
/// TMI=13 直用、TMI_EXT=7-9),精确按频段校验需网络级频段配置(暂无)。
/// 返回 -1 = 组合本身无效(TMI 越界/13 无有效 TMI_EXT)。
inline int nw_2021_pb_num_upper_bound(quint8 tmi, quint8 tmi_ext) {
    int col;
    if (tmi <= 12) {
        col = tmi;
    } else if (tmi == 13 && tmi_ext >= 1 && tmi_ext <= 14) {
        col = 14 + (tmi_ext - 1);
    } else {
        return -1;
    }
    int m = nw_2021_pb_num_table[0][col];
    for (int b = 1; b < 3; ++b)
        m = (nw_2021_pb_num_table[b][col] > m) ? nw_2021_pb_num_table[b][col] : m;
    return m;
}

#endif // NW_2021_PB_TABLE_H
