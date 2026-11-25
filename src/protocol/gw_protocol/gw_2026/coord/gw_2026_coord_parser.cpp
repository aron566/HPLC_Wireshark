/// @file coordparser.cpp
/// @brief 网间协调帧(COORD)解析实现(独立帧解析模块,coordparser.pri)
/// @details 可变区域字段与规范表16 对齐(相对 MPDU 起点):
///          TimeDuration(4,0,16) BandStartOffset(6,0,16)
///          NeighbourNID(8,0,16) RSV(10,0,8) NetRfChannel(11,0,8)
///          NetRfOption(12,0,2) RSV(12,2,2)。
///          公共位域工具见 fieldspec.h。
#include "gw_2026_coord_parser.h"
#include "common/fieldtools.h"
#include "crc.h"

namespace gw_2026_coordp {

void parse_fch(const quint8* p, MpduInfo& m) {
    m.coord_duration      = (quint16)get_bits_unchecked(p, 4, 0, 16);
    m.coord_shift         = (quint16)get_bits_unchecked(p, 6, 0, 16);
    m.coord_neighbour_nid = (quint32)get_bits_unchecked(p, 8, 0, 16);  // 表16 (8,0,16) 邻网号 16bit
    m.coord_rf_channel    = (quint8)get_bits_unchecked(p, 11, 0, 8);
    // byte12 bit0-3 整体存入 coord_rsv0 容器(表16:bit0-1 = NetRfOption, bit2-3 = RSV);
    // 字段树层拆 (12,0,2)/(12,2,2) 渲染(不新增 MpduInfo 字段, 遵守 bplcframe.h 零改动)
    m.coord_rsv0          = (quint8)get_bits_unchecked(p, 12, 0, 4);
}

}  // namespace gw_2026_coordp
