/// @file ackparser.cpp
/// @brief ACK 帧解析实现(独立帧解析模块,ackparser.pri)
/// @details 移植自 BPLCMonitor/MPDU_Class.py 的 MPDU_ACK 逻辑:
///          FCH ExtFrameType(bit 12,0,4) 按表25/表36:0=选择确认,其他=保留。
///          公共位域工具见 fieldspec.h。
#include "gw_2022_ack_parser.h"
#include "gw_2022_tree.h"
#include "common/fieldtools.h"
#include "crc.h"

namespace gw_2022_ackp {

void parse_fch(const quint8* p, MpduInfo& m) {
    m.ack_ext_type = (quint8)get_bits(p, 12, 0, 4);
    // 表25/表36:0=选择确认帧,其他=保留;未知扩展帧类型默认按选择确认帧解析(表23,载波):
    // RxRes(4,0,4) RxStatus(4,4,4) SrcTEI(5,0,12) DstTEI(6,4,12)
    // RxPBNum(8,0,3) RSV0(8,3,5) ChQ(9,0,8) Load(10,0,8) RSV1(11,0,8)
    m.ack_rx_res      = (quint8)get_bits(p, 4, 0, 4);
    m.ack_rx_status   = (quint8)get_bits(p, 4, 4, 4);
    m.src_tei         = (quint16)get_bits(p, 5, 0, 12);
    m.dst_tei         = (quint16)get_bits(p, 6, 4, 12);
    m.ack_rx_pb_num   = (quint8)get_bits(p, 8, 0, 3);
    m.ack_rsv0        = (quint8)get_bits(p, 8, 3, 5);
    m.ack_channel_quality = (quint8)get_bits(p, 9, 0, 8);
    m.ack_sta_load    = (quint8)get_bits(p, 10, 0, 8);
    m.ack_rsv1        = (quint8)get_bits(p, 11, 0, 8);
}

}  // namespace gw_2022_ackp
