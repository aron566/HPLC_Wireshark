/// @file coordparser.cpp
/// @brief 网间协调帧(COORD)解析实现(独立帧解析模块,coordparser.pri)
/// @details 可变区域字段与规范表26 对齐(相对 MPDU 起点):
///          TimeDuration(4,0,16) NextTimeSlotShift(6,0,16)
///          NeighbourNID(8,0,24) NetRfChannel(11,0,8) RSV0(12,0,4)。
///          公共位域工具见 fieldspec.h。
#include "coordparser.h"
#include "fieldspec.h"

namespace coordp {

void parse_fch(const quint8* p, MpduInfo& m) {
    m.coord_duration      = (quint16)get_bits(p, 4, 0, 16);
    m.coord_shift         = (quint16)get_bits(p, 6, 0, 16);
    m.coord_neighbour_nid = (quint32)get_bits(p, 8, 0, 24);
    m.coord_rf_channel    = (quint8)get_bits(p, 11, 0, 8);
    m.coord_rsv0          = (quint8)get_bits(p, 12, 0, 4);
}

}  // namespace coordp
