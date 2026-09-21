/// @file nw_2021_msdu_parser.cpp
/// @brief 南网 NW_2021 MSDU/MAC 层解析实现(头解析,管理消息后续补)
/// @details 移植自 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG/MSDU_Class.py
///          的 MSDU_Process 流程:
///           1. 判帧类型:Version(bit1-2)=2 单跳(MSDU_BASE_S 4B);=1 标准(MSDU_BASE)
///           2. MAC 帧头(MSDU_BASE 32B 长/12B 短):TEI/SNID/重启次数/广播方向
///           3. MSDU 帧头(长 18B / 短 2B,由 MACHeadFlag 决定):MAC 48b + VLAN + 类型
#include "nw_2021_msdu_parser.h"
#include "common/fieldtools.h"
#include "common/fieldspec.h"

MsduInfo NW_2021_MsduParser::parse(const QByteArray& body) {
    MsduInfo out;
    if (body.size() < 4) return out;
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    // 帧类型:Version 字段(bit1-2)。2=单跳帧(MSDU_BASE_S 4B),1=标准帧(MSDU_BASE)
    const quint8 version = (quint8)get_bits(p, 0, 1, 2);

    if (version == 2) {
        // 单跳 MAC 帧头 MSDU_BASE_S(4B):MACHeadFlag(0,0,1) Version RSV0 MSDU_Type(1,0,8) MSDULen(2,0,16)
        out.simple_head = true;
        out.msdu_type   = (quint16)get_bits(p, 1, 0, 8);
        const quint16 msdu_len = (quint16)get_bits(p, 2, 0, 16);
        out.total_len = 4 + msdu_len + 4;   // 头 + 体 + CRC32
        out.present = true;
        return out;
    }

    // 标准 MAC 帧头 MSDU_BASE:MACHeadFlag(0,0,1) 决定长(32B)/短(12B)
    const quint8 mac_head_flag = (quint8)get_bits(p, 0, 0, 1);
    const quint16 msdu_len     = (quint16)get_bits(p, 2, 0, 16);

    out.msdu_dst_tei        = (int)get_bits(p, 4, 0, 12);
    out.msdu_src_tei        = (int)get_bits(p, 5, 4, 12);
    // ShortNID(7,0,4) 已在 MpduInfo.net_id 承载,此处不重复
    out.restart_count       = (quint8)get_bits(p, 7, 4, 4);
    out.broadcast_direction = (quint8)get_bits(p, 8, 4, 4);
    out.msdu_send_type      = (int)get_bits(p, 9, 0, 3);
    out.msdu_seq            = (quint16)get_bits(p, 10, 0, 16);

    const int mac_hdr_len = (mac_head_flag == 0) ? 32 : 12;
    if (body.size() < mac_hdr_len + msdu_len) return out;
    const QByteArray msdu_body = body.mid(mac_hdr_len, msdu_len);
    out.total_len = mac_hdr_len + msdu_len + 4;   // 头 + 体 + CRC32

    // MSDU 帧头(长 18B / 短 2B,由 MACHeadFlag 决定)
    if (mac_head_flag == 0) {
        // MSDU_LONGHEAD(18B):原始目的/源 MAC 48b + VLAN 32b + MSDU 类型 16b
        if (msdu_body.size() >= 18) {
            const quint8* q = reinterpret_cast<const quint8*>(msdu_body.constData());
            out.msdu_dst_mac = get_bits(q, 0, 0, 48);
            out.msdu_src_mac = get_bits(q, 6, 0, 48);
            out.vlan_tag     = (quint32)get_bits(q, 12, 0, 32);
            out.msdu_type    = (quint16)get_bits(q, 16, 0, 16);
            // VLAN 0x8100 = 长帧头管理消息(MMe);否则抄表业务(APP)
            if (out.vlan_tag == 0x8100 && msdu_body.size() >= 20) {
                const quint16 mm_type = (quint16)get_bits(q, 18, 0, 16);
                out.summary = QStringLiteral("MMe 0x%1").arg(mm_type, 4, 16, QChar('0'));
            } else {
                out.summary = QStringLiteral("APP 0x%1").arg(out.msdu_type, 4, 16, QChar('0'));
            }
        }
    } else {
        // MSDU_SHORTHEAD(2B):VLAN 8b + MSDU 类型 8b
        if (msdu_body.size() >= 2) {
            const quint8* q = reinterpret_cast<const quint8*>(msdu_body.constData());
            out.vlan_tag  = (quint32)get_bits(q, 0, 0, 8);
            out.msdu_type = (quint16)get_bits(q, 1, 0, 8);
            out.summary   = QStringLiteral("APP 0x%1").arg(out.msdu_type, 2, 16, QChar('0'));
        }
    }
    out.present = true;
    return out;
}
