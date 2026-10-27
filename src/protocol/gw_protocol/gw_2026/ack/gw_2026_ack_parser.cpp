/// @file ackparser.cpp
/// @brief ACK 帧解析实现(独立帧解析模块,ackparser.pri)
/// @details 移植自 BPLCMonitor/MPDU_Class.py 的 MPDU_ACK 逻辑:
///          FCH ExtFrameType(bit 12,0,4) 按表14/表15:0=选择确认,其他=保留。
///          公共位域工具见 fieldspec.h。
#include "gw_2026_ack_parser.h"
#include "gw_2026_tree.h"
#include "common/fieldtools.h"
#include "crc.h"

namespace gw_2026_ackp {

void parse_fch(const quint8* p, MpduInfo& m) {
    m.ack_ext_type = (quint8)get_bits(p, 12, 0, 4);
    // 表14/表15:0=选择确认帧,其他=保留;未知扩展帧类型默认按选择确认帧解析。
    m.ack_bru_snr_flag   = (quint8)get_bits(p, 4, 0, 1);
    m.ack_sat_id_flag    = (quint8)get_bits(p, 4, 1, 1);
    if (m.ack_bru_snr_flag) {
        // 表15(选择确认帧 反馈SNR版):BRU标志(4,0)/SATID(4,1)/RxPBNum(4,2-3)/RxStatus(4,4-7)
        //   SrcTEI(5,0,12) DstTEI(6,4,12) BRU0-7 SNR(8-11,8×4b) ExtType(12,0-3)
        // 表14 的 接收结果/RremainSTANum/ChQ/Load 字段在表15 中不存在,置 0 防误渲染
        m.ack_rx_res         = 0;
        m.ack_rx_pb_num      = (quint8)get_bits(p, 4, 2, 2);
        m.ack_rx_status      = (quint8)get_bits(p, 4, 4, 4);
        m.src_tei            = (quint16)get_bits(p, 5, 0, 12);
        m.dst_tei            = (quint16)get_bits(p, 6, 4, 12);
        m.ack_remain_sta_num = 0;
        m.ack_rsv0           = 0;
        m.ack_channel_quality = 0;
        m.ack_sta_load       = 0;
        m.ack_rsv1           = 0;
        return;
    }
    // 表14(载波 不反馈SNR版)可变区域 68bit 连续位流(起点 byte4 bit0):
    //   BRU(4,0,1) SATID(4,1,1) RxRes(4,2,1) RxPBNum(4,3,2)
    //   RxStatus=(4,5,3)|(5,0,1)<<3 跨字节
    //   SrcTEI(5,1,12) DstTEI(6,5,12) RemainSTANum(8,1,2) RSV0(8,3,5)
    //   ChQ(9,1,8) Load(10,1,8) RSV1(11,1,8) ExtType(12,0,4)
    m.ack_rx_res         = (quint8)get_bits(p, 4, 2, 1);
    m.ack_rx_pb_num      = (quint8)get_bits(p, 4, 3, 2);
    m.ack_rx_status      = (quint8)((get_bits(p, 4, 5, 3))
                                 | (get_bits(p, 5, 0, 1) << 3));
    m.src_tei            = (quint16)get_bits(p, 5, 1, 12);
    m.dst_tei            = (quint16)get_bits(p, 6, 5, 12);
    m.ack_remain_sta_num = (quint8)get_bits(p, 8, 1, 2);
    m.ack_rsv0           = (quint8)get_bits(p, 8, 3, 5);
    m.ack_channel_quality = (quint8)get_bits(p, 9, 1, 8);
    m.ack_sta_load       = (quint8)get_bits(p, 10, 1, 8);
    m.ack_rsv1           = (quint8)get_bits(p, 11, 1, 8);
}

}  // namespace gw_2026_ackp
