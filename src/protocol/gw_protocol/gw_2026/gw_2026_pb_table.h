/// @file gw_2026_pb_table.h
/// @brief 国网 GW_2026 协议参数:TMI → PB 块大小查表
/// @details 国网 HPLC TMI(分级拷贝基本模式)决定物理块大小,取值 0-14:
///   TMI 0-1 → 520;2-6 → 136;7-10 → 520;11-12 → 264;13-14 → 72。
///   信标/单块帧同样按 FCH TMI 查表;-1 = TMI 无效。
/// @note 南网 NW_2021 的 PB 大小定义不同(见 nw_protocol/nw_2021/nw_2021_pb_table.h),
///       故本表按协议+版本独立,不与其他协议共享。
#ifndef GW_2026_PB_TABLE_H
#define GW_2026_PB_TABLE_H

#include <QtGlobal>

/// 国网无线载荷 PB 大小码 → PB 块大小(字节)(HDC2.0 4-2 表18/表19:0-5 = 16/40/72/136/264/520)。-1 = 保留值
inline int gw_2026_rf_pb_size(quint8 code) {
    static const quint16 table[] = {16, 40, 72, 136, 264, 520};
    return (code <= 5) ? int(table[code]) : -1;
}

/// 国网 TMI → PB 块大小(字节)。-1 = TMI 无效
inline int gw_2026_pb_size(quint8 tmi) {
    if (tmi == 0 || tmi == 1)       return 520;
    if (tmi >= 2 && tmi <= 6)       return 136;
    if (tmi >= 7 && tmi <= 10)      return 520;
    if (tmi == 11 || tmi == 12)     return 264;
    if (tmi == 13 || tmi == 14)     return 72;
    return -1;
}

/// 国网载波分集拷贝模式 → PB 块大小(字节)。模式 0-28 有效,其余 -1。
/// 分集拷贝模式表(2026-10-28 用户提供;HDC 2.0 4-2 表7 调制参数原文指向
/// Q/GDW XXX.41 5.1.7.6 分集拷贝模式,本地提取件无该表,故此处逐行照录用户表):
///   模式: 0   1   2   3   4   5   6   7   8   9   10  11  12  13  14
///   大小: 520 264 136 72  520 264 136 72  520 264 136 72  520 264 136
///   模式: 15  16  17  18  19  20  21  22  23  24  25  26  27  28
///   大小: 72  520 264 136 72  520 264 136 72  520 264 136 72  520
/// 附注:子频段物理块数应不大于 4,且同时保证载荷符号数不超过 511。
/// HDC 2.0 无 TMI_EXT:分集拷贝模式为单字段(表7 byte10 bit2-6,5 bit),
/// 不存在"基本模式 15 → 扩展模式"的 HDC 1.0 机制,故本函数不取 tmi_ext。
inline int gw_2026_diversity_pb_size(quint8 mode) {
    static const quint16 table[] = {
        520, 264, 136, 72,  520, 264, 136, 72,  520, 264, 136, 72,  520, 264, 136,
        72,  520, 264, 136, 72,  520, 264, 136, 72,  520, 264, 136, 72,  520
    };
    return (mode <= 28) ? int(table[mode]) : -1;
}

/// 国网各频段(PL band 0-3)× 分集拷贝模式支持的最大 PB 块数量。
/// 列索引 0-14 = 模式 0-14(沿用旧表值,行为不变);模式 15-28 无旧表列,
/// 按上表附注"子频段物理块数应不大于 4"取上限 4(门仅起封顶作用)。
inline constexpr quint8 gw_2026_pb_num_table[4][15] = {
    //  mode: 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e
    { 4, 4, 4, 4, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4 },  // PL band0
    { 3, 4, 4, 2, 4, 4, 4, 1, 1, 2, 3, 4, 2, 4, 4 },  // PL band1
    { 2, 4, 4, 1, 2, 3, 4, 0, 1, 1, 2, 2, 1, 4, 4 },  // PL band2
    { 1, 2, 3, 0, 1, 1, 2, 0, 0, 0, 1, 1, 0, 4, 2 },  // PL band3
};

/// 查国网某频段 + 分集拷贝模式支持的最大 PB 块数量。返回 -1 = 配置无效。
/// HDC 2.0 无 TMI_EXT,故本函数不再取 tmi_ext(旧扩展列 16-29 已删)。
inline int gw_2026_max_pb_num(quint8 band, quint8 mode) {
    if (band >= 4) return -1;                       // 频段越界(0-3)
    if (mode <= 14) return gw_2026_pb_num_table[band][mode];
    if (mode <= 28) return 4;                       // 新模式列:附注封顶 4
    return -1;                                      // 模式 29-31 保留
}

#endif // GW_2026_PB_TABLE_H
