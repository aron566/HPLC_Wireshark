/// @file nw_2021_parser.cpp
/// @brief 南网 NW_2021 双模协议解析器实现(骨架:帧控制 + 各帧型 FCH 字段)
/// @details 字段坐标移植自 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG 的
///          MPDU_Class.py。南网与国网差异:SNID 4b、各帧型字段坐标、PB 头 4B。
///          物理头 [dlen2][ts4][phr_mcs][option][channel][isRF] 与国网一致。
#include "nw_2021_parser.h"
#include "nw_2021_pb_table.h"
#include "nw_2021_msdu_parser.h"
#include "nw_2021_beacon_parser.h"
#include "common/fieldtools.h"
#include "crc.h"
#include "bcd.h"
#include "i18n.h"
#include <QDateTime>

namespace {

int bcd2dec_here(quint8 b) {
    return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

}  // namespace

NW_2021_Parser::NW_2021_Parser() {}

// 剥物理层头(与国网一致:[dlen2][ts4][phr_mcs][option][channel][isRF])
bool NW_2021_Parser::decode_envelope(const BplcFrame& in, Result& r) {
    r.meta = in.meta;
    r.arrival_us = in.arrival_us;
    r.raw_wire   = in.raw_wire;
    const QByteArray& d = in.data;

    if (in.meta.from_raw) {
        if (d.size() < 2) { r.reject_reason = trl::L("空帧"); return false; }
        r.meta.is_rf = (quint8(d[0]) != 0);
        r.payload_for_log = d.mid(1);
        r.meta.frame_time = QDateTime::fromMSecsSinceEpoch(in.arrival_ms);
        return true;
    }

    int hdr = in.meta.has_time_tag ? 8 : 0;
    if (d.size() < 20 + hdr) {
        r.reject_reason = trl::L("帧长过短");
        return false;
    }

    if (in.meta.has_time_tag) {
        try {
            int y  = bcd2dec_here((quint8)d[0]);
            int mo = bcd2dec_here((quint8)d[1]);
            int da = bcd2dec_here((quint8)d[2]);
            int hh = bcd2dec_here((quint8)d[3]);
            int mm = bcd2dec_here((quint8)d[4]);
            int ss = bcd2dec_here((quint8)d[5]);
            int ms = bcd2dec_here((quint8)d[6]) * 100 + bcd2dec_here((quint8)d[7]);
            r.meta.frame_time = QDateTime(QDate(2000 + y, mo, da),
                                          QTime(hh, mm, ss, ms));
            r.meta.has_time_tag = true;
        } catch (...) {
            r.meta.frame_time = QDateTime::currentDateTime();
        }
    } else {
        r.meta.frame_time = QDateTime::fromMSecsSinceEpoch(in.arrival_ms);
    }

    int offset = hdr;
    quint32 ts = 0;
    ts |= (quint32)(quint8)d[offset + 2];
    ts |= (quint32)(quint8)d[offset + 3] << 8;
    ts |= (quint32)(quint8)d[offset + 4] << 16;
    ts |= (quint32)(quint8)d[offset + 5] << 24;
    r.meta.timestamp = ts;

    QByteArray payload = d.mid(offset + 6);
    if (payload.size() < 4) {
        r.reject_reason = trl::L("剥头后载荷过短(<4B)");
        return false;
    }
    r.meta.phr_mcs  = (quint8)payload[0];
    r.meta.option   = (quint8)payload[1];
    r.meta.channel  = (quint8)payload[2];
    quint8 media_id = (quint8)payload[3];
    r.meta.is_rf = (media_id != 0);

    payload.remove(0, 4);
    r.payload_for_log = payload;
    if (payload.isEmpty()) {
        r.reject_reason = trl::L("剥头后空载荷");
        return false;
    }
    return true;
}

// 南网 MPDU_BASE 帧控制(16B):FrameType + ConInd + ShortNID(4b) + VersionNum + FCCS
bool NW_2021_Parser::parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err) {
    if (body.size() < 16) { err = trl::L("MPDU_BASE 长度不足 16B"); return false; }
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    quint32 calc_crc = crc24_lsb(p, 16);
    quint32 fch_crc  = (quint32)p[13] | ((quint32)p[14] << 8) | ((quint32)p[15] << 16);
    info.fch_crc_ok = (calc_crc == fch_crc);
    if (!info.fch_crc_ok) {
        err = QString("FCH CRC24 错(calc=%1 rx=%2)")
              .arg(calc_crc, 6, 16, QChar('0')).arg(fch_crc, 6, 16, QChar('0'));
        return false;
    }

    info.frame_type = (quint8)get_bits(p, 0, 0, 3);   // 定界符类型
    info.net_type   = (quint8)get_bits(p, 0, 3, 1);   // 接入指示 ConInd
    info.net_id     = (quint32)get_bits(p, 0, 4, 4);  // 短网络标识 SNID 4b
    info.version    = (quint8)get_bits(p, 12, 4, 4);  // 标准版本号
    return true;
}

NW_2021_Parser::Result NW_2021_Parser::parse(const BplcFrame& in, MsduState& msdu, const Filter& f) {
    Result r;
    Q_UNUSED(msdu);
    if (!decode_envelope(in, r)) {
        r.accept = false;
        return r;
    }

    if (!r.meta.is_rf && !f.link_hplc) { r.reject_reason = trl::L("HPLC 链路被过滤"); r.accept = false; return r; }
    if ( r.meta.is_rf && !f.link_hrf ) { r.reject_reason = trl::L("HRF 链路被过滤");  r.accept = false; return r; }

    QString err;
    if (!parse_mpdu_base(r.payload_for_log, r.mpdu, err)) {
        r.reject_reason = err;
        r.accept = false;
        return r;
    }

    if (f.nid_filter && !f.nid_list.contains(r.mpdu.net_id)) {
        r.reject_reason = QString("SNID 不在白名单: 0x%1").arg(r.mpdu.net_id, 1, 16);
        r.accept = false;
        return r;
    }

    switch (r.mpdu.frame_type) {
        case 0: if (!f.allow_beacon) { r.reject_reason = trl::L("BEACON 被过滤"); r.accept = false; return r; } break;
        case 1: if (!f.allow_sof)    { r.reject_reason = trl::L("SOF 被过滤");    r.accept = false; return r; } break;
        case 2: if (!f.allow_ack)    { r.reject_reason = trl::L("ACK 被过滤");    r.accept = false; return r; } break;
        case 3: if (!f.allow_coord)  { r.reject_reason = trl::L("COORD 被过滤");  r.accept = false; return r; } break;
        default: break;
    }

    const quint8* p = reinterpret_cast<const quint8*>(r.payload_for_log.constData());

    if (r.mpdu.frame_type == 1) {
        // SOF:源/目的 TEI(载波与无线同坐标)+ PB 大小(载波 TMI / 无线 PBLen)
        r.mpdu.src_tei = (quint16)get_bits(p, 1, 0, 12);
        r.mpdu.dst_tei = (quint16)get_bits(p, 2, 4, 12);
        r.mpdu.link_id = (quint8) get_bits(p, 4, 0, 8);
        if (r.meta.is_rf) {
            r.mpdu.frame_len = (quint16)get_bits(p, 5, 0, 12);
            quint8 pblen = (quint8)get_bits(p, 6, 4, 4);
            r.mpdu.tmi     = pblen;                       // 载荷 PB 大小(复用 tmi 承载)
            r.mpdu.pb_size = (quint16)nw_2021_rf_pb_size(pblen);
            r.mpdu.pb_num  = 1;                           // 无线信道仅 1 个物理块
        } else {
            r.mpdu.pb_num    = (quint8) get_bits(p, 7, 0, 4);
            r.mpdu.tmi       = (quint8) get_bits(p, 7, 4, 4);
            r.mpdu.frame_len = (quint16)get_bits(p, 8, 0, 12);
            r.mpdu.tmi_ext   = (quint8) get_bits(p, 12, 0, 4);
            r.mpdu.pb_size   = (quint16)nw_2021_pb_size(r.mpdu.tmi, r.mpdu.tmi_ext);
        }
        if (f.tei_filter && !f.tei_list.contains(r.mpdu.src_tei)
                        && !f.tei_list.contains(r.mpdu.dst_tei)) {
            r.reject_reason = trl::L("TEI 不在白名单");
            r.accept = false;
            return r;
        }
        // PB 块重组 + CRC24 校验(南网:块 = PB头4B + 块体(pb_size-8) + 保留1B + CRC24 3B)
        if (r.mpdu.pb_size > 8 && r.mpdu.pb_num > 0) {
            const int body_len = r.mpdu.pb_size - 8;
            QByteArray msdu_body;
            bool all_pb_ok = true;
            for (int i = 0; i < r.mpdu.pb_num; ++i) {
                const int block_start = 16 + i * r.mpdu.pb_size;
                if (block_start + r.mpdu.pb_size > r.payload_for_log.size()) break;
                // PB CRC24:覆盖 PB 头 + 块体 + 保留字节(块内前 pb_size-3 字节,表7)
                const quint8* blk = reinterpret_cast<const quint8*>(
                    r.payload_for_log.constData()) + block_start;
                const quint32 calc = crc24_lsb(blk, r.mpdu.pb_size);
                const quint32 rx = (quint32)blk[r.mpdu.pb_size - 3]
                                 | ((quint32)blk[r.mpdu.pb_size - 2] << 8)
                                 | ((quint32)blk[r.mpdu.pb_size - 1] << 16);
                const bool ok = (calc == rx);
                all_pb_ok = all_pb_ok && ok;
                r.mpdu.pb_crc_oks.append(ok);
                msdu_body += r.payload_for_log.mid(block_start + 4, body_len);
            }
            r.mpdu.pb_crc_ok = all_pb_ok;
            if (!msdu_body.isEmpty()) {
                r.msdu_body = msdu_body;
                r.msdu = NW_2021_MsduParser::parse(msdu_body);
                r.msdu_raw_base = (r.mpdu.pb_num == 1) ? (16 + 4) : -1;
            }
        }
    } else if (r.mpdu.frame_type == 0) {
        // BEACON:信标时间戳/周期计数/源 TEI(载波与无线同坐标)
        r.mpdu.beacon_timestamp  = (quint32)get_bits(p, 1, 0, 32);
        r.mpdu.beacon_period_cnt = (quint32)get_bits(p, 5, 0, 32);
        r.mpdu.src_tei           = (quint16)get_bits(p, 9, 0, 12);
        if (r.meta.is_rf) {
            r.mpdu.tmi     = (quint8)get_bits(p, 11, 0, 4);   // 载荷 PB 大小
            r.mpdu.pb_size = (quint16)nw_2021_rf_pb_size(r.mpdu.tmi);
        } else {
            r.mpdu.tmi       = (quint8)get_bits(p, 10, 4, 4); // 载波映射表索引
            r.mpdu.symbol_num = (quint16)get_bits(p, 11, 0, 9);
            r.mpdu.beacon_line = (quint8)get_bits(p, 12, 2, 2);
            r.mpdu.pb_size   = (quint16)nw_2021_pb_size(r.mpdu.tmi, 0);
        }
        // 信标帧载荷区(固定头 + 管理信息 + BPCS CRC32 + 保留字节 + PB CRC24)
        if (r.mpdu.pb_size > 0)
            r.beacon = NW_2021_BeaconParser::parse_beacon(r.payload_for_log, r.mpdu.pb_size);
    } else if (r.mpdu.frame_type == 2) {
        // ACK(南网 MPDU_ACK_FCH 坐标,ExtType 0-3/10-12)
        r.mpdu.ack_ext_type = (quint8)get_bits(p, 12, 0, 4);
        switch (r.mpdu.ack_ext_type) {
            case 0:  // 常规 ACK
                r.mpdu.ack_rx_res    = (quint8) get_bits(p, 1, 0, 4);
                r.mpdu.ack_rx_status = (quint8) get_bits(p, 1, 4, 4);
                r.mpdu.dst_tei       = (quint16)get_bits(p, 2, 0, 12);
                r.mpdu.ack_rx_pb_num = (quint8) get_bits(p, 3, 4, 4);
                break;
            case 1:  // 网络搜索帧
                r.mpdu.ack_dst_addr   = get_bits(p, 1, 0, 48);
                r.mpdu.ack_search_tei = (quint16)get_bits(p, 7, 0, 12);
                break;
            case 2:  // 同步帧
                r.mpdu.ack_sync_timestamp = (quint32)get_bits(p, 1, 0, 32);
                r.mpdu.ack_sync_tei       = (quint16)get_bits(p, 5, 0, 12);
                break;
            case 3:  // 无线切频帧
                r.mpdu.ack_dst_addr        = get_bits(p, 1, 0, 48);
                r.mpdu.ack_channel_quality = (quint8)get_bits(p, 7, 0, 8);
                r.mpdu.ack_sta_load        = (quint8)get_bits(p, 8, 0, 8);
                break;
            default:  // 10 时隙预约 / 11 测距响应 / 12 测距请求(南网扩展,暂缓)
                break;
        }
    } else if (r.mpdu.frame_type == 3) {
        // COORD(南网 MPDU_COORD_FCH 坐标)
        r.mpdu.coord_neighbour_nid     = (quint32)get_bits(p, 1, 0, 16);  // 邻居 NID 位图 16b
        r.mpdu.coord_rf_channel        = (quint8) get_bits(p, 3, 0, 8);   // 信道
        r.mpdu.coord_duration          = (quint16)get_bits(p, 5, 2, 14);  // 时长(×40ms)
        r.mpdu.coord_band_end_flag     = (quint8) get_bits(p, 7, 1, 1);
        r.mpdu.coord_option            = (quint8) get_bits(p, 7, 2, 2);
        r.mpdu.coord_band_end_offset   = (quint16)get_bits(p, 8, 0, 16);  // ×4ms
        r.mpdu.coord_band_start_offset = (quint16)get_bits(p, 10, 0, 16); // ×4ms
    }

    r.accept = true;
    return r;
}
