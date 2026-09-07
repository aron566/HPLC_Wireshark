/// @file ackparser.cpp
/// @brief ACK 帧解析实现(独立帧解析模块,ackparser.pri)
/// @details 移植自 BPLCMonitor/MPDU_Class.py 的 MPDU_ACK 逻辑:
///          FCH ExtFrameType(bit 12,0,4) 分派:
///          0 常规(收包结果+信道质量+STA 负载)/1 搜索/2 同步/3 切频。
///          公共位域工具见 fieldspec.h。
#include "ackparser.h"
#include "fieldspec.h"

namespace ackp {

void parse_fch(const quint8* p, MpduInfo& m) {
    m.ack_ext_type = (quint8)get_bits(p, 12, 0, 4);
    if (m.ack_ext_type == 0) {
        // 常规 ACK(用户/协议定义,物理字节序):
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
    } else if (m.ack_ext_type == 1) {
        // 搜索:DstAddr(4,0,48) SearchStei(10,0,12) SearchFreq(11,4,4)
        m.ack_dst_addr    = get_bits(p, 4, 0, 48);
        m.ack_search_tei  = (quint16)get_bits(p, 10, 0, 12);
        m.ack_search_freq = (quint8)get_bits(p, 11, 4, 4);
    } else if (m.ack_ext_type == 2) {
        // 同步:Timestamp(4,0,32) SyncStei(8,0,12)
        m.ack_sync_timestamp = (quint32)get_bits(p, 4, 0, 32);
        m.ack_sync_tei       = (quint16)get_bits(p, 8, 0, 12);
    } else if (m.ack_ext_type == 3) {
        // 切频:DstAddr(4,0,48) HrfChanel(10,0,8) HrfOption(11,0,8)
        m.ack_dst_addr    = get_bits(p, 4, 0, 48);
        m.ack_rx_pb_num   = (quint8)get_bits(p, 10, 0, 8);  // HrfChanel
        m.ack_sta_load    = (quint8)get_bits(p, 11, 0, 8);  // HrfOption
    }
}

}  // namespace ackp
