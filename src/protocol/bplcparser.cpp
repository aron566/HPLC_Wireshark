/// @file bplcparser.cpp
/// @brief BplcParser 实现:剥头 + CRC24 + BitDefine + SOF MSDU 重组
/// @details 移植自 BPLCMonitor/main.py + MPDU_Class.py 的字段逻辑。
#include "bplcparser.h"
#include "i18n.h"
#include "statistics.h"
#include "msduparser.h"
#include "beaconparser.h"
#include <QDateTime>
#include <QtEndian>
#include <algorithm>

// 取 N bit 位字段(等价 Python BitDefine)
quint64 BplcParser::get_bits(const quint8* data, int start_byte, int start_bit, int bit_len) {
    int bits_occupied = bit_len + start_bit;
    int bytes_floor = bits_occupied / 8;
    int bytes_occupied = bytes_floor + ((bits_occupied % 8) != 0 ? 1 : 0);

    quint64 result = 0;
    for (int i = 0; i < bytes_occupied; ++i) {
        quint8 b = data[start_byte + i];
        if (i == 0) {
            // 首字节:清零低 start_bit 位(右移再左移,与 Python 一致)
            b = (quint8)((b >> start_bit) << start_bit);
        }
        if (i == bytes_occupied - 1) {
            // 末字节:只保留低 keep 位。
            // 注意:不能写 (b<<(8-keep))>>(8-keep)——quint8 提升为 int 后
            // 左移不丢高位,右移回来是恒等变换,截断失效(bug 曾致 SOF src_tei 错误)。
            // 用掩码等价于 Python 的 %256 环绕取低位。
            int keep = bits_occupied - (bytes_occupied - 1) * 8;
            if (keep < 8) b = (quint8)(b & ((1u << keep) - 1u));
        }
        result |= (quint64(b) << (8 * i));
    }
    return result >> start_bit;
}

quint64 BplcParser::get_bits(const QByteArray& data, int start_byte, int start_bit, int bit_len) {
    return get_bits(reinterpret_cast<const quint8*>(data.constData()), start_byte, start_bit, bit_len);
}

// CRC24(从 BPLCMonitor/cal_crc24 直接移植:poly=0xC60001,init=0)
quint32 BplcParser::crc24(const quint8* data, int len) {
    const quint32 poly = 0xC60001;
    quint32 crc = 0;
    for (int i = 0; i < len - 3; ++i) {
        for (int j = 0; j < 8; ++j) {
            quint32 bit_in  = (data[i] >> j) & 0x1;
            quint32 bit_lsb = crc & 0x1;
            crc >>= 1;
            if (bit_in ^ bit_lsb) crc ^= poly;
        }
    }
    return crc;
}

// CRC32(从 BPLCMonitor/cal_crc32 直接移植:poly=0xEDB88320,init=0xFFFFFFFF,~)
quint32 BplcParser::crc32(const quint8* data, int len) {
    const quint32 poly = 0xEDB88320;
    quint32 crc = 0xFFFFFFFF;
    for (int i = 0; i < len - 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            quint32 bit_in  = (data[i] >> j) & 0x1;
            quint32 bit_lsb = crc & 0x1;
            crc >>= 1;
            if (bit_in ^ bit_lsb) crc ^= poly;
        }
    }
    return (~crc) & 0xFFFFFFFF;
}

static int bcd2dec(quint8 b) {
    return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

static int get_pb_size(quint8 tmi, quint8 tmi_ext) {
    if (tmi == 0 || tmi == 1)                          return 520;
    if (tmi >= 2 && tmi <= 6)                          return 136;
    if (tmi >= 7 && tmi <= 10)                         return 520;
    if (tmi == 11 || tmi == 12)                        return 264;
    if (tmi == 13 || tmi == 14)                        return 72;
    if (tmi_ext >= 1 && tmi_ext <= 6)                  return 520;
    if (tmi_ext >= 10 && tmi_ext <= 14)                return 136;
    return -1;
}

BplcParser::BplcParser() {}

// 剥物理层头
bool BplcParser::decode_envelope(const BplcFrame& in, Result& r) {
    r.meta = in.meta;
    const QByteArray& d = in.data;

    if (in.meta.from_raw) {
        if (d.isEmpty()) { r.reject_reason = trl::L("空帧"); return false; }
        r.payload_for_log = d;
        r.meta.is_rf = (quint8(d[0]) != 0);
        return true;
    }

    int hdr = in.meta.has_time_tag ? 8 : 0;
    if (d.size() < 20 + hdr) {
        r.reject_reason = trl::L("帧长过短");
        return false;
    }

    if (in.meta.has_time_tag) {
        try {
            int y  = bcd2dec((quint8)d[0]);
            int mo = bcd2dec((quint8)d[1]);
            int da = bcd2dec((quint8)d[2]);
            int hh = bcd2dec((quint8)d[3]);
            int mm = bcd2dec((quint8)d[4]);
            int ss = bcd2dec((quint8)d[5]);
            int ms = bcd2dec((quint8)d[6]) * 100 + bcd2dec((quint8)d[7]);
            r.meta.frame_time = QDateTime(QDate(2000 + y, mo, da),
                                          QTime(hh, mm, ss, ms));
            r.meta.has_time_tag = true;
        } catch (...) {
            r.meta.frame_time = QDateTime::currentDateTime();
        }
    } else {
        r.meta.frame_time = QDateTime::currentDateTime();
    }

    int offset = hdr;
    quint16 data_len = (quint8)d[offset] | ((quint8)d[offset + 1] << 8);
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

    // 去掉 4 字节媒介头(phr_mcs/option/channel/isRF),payload_for_log = 纯 MPDU
    // (与 main.py 语义一致:log hex 行 = [isRF][MPDU],MPDU_BASE 从 data[1] 开始)
    payload.remove(0, 4);
    r.payload_for_log = payload;
    if (payload.isEmpty()) {
        r.reject_reason = trl::L("剥头后空载荷");
        return false;
    }

    Q_UNUSED(data_len);
    return true;
}

// MPDU_BASE 控制头
bool BplcParser::parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err) {
    if (body.size() < 16) { err = trl::L("MPDU_BASE 长度不足 16B"); return false; }
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    quint32 calc_crc = crc24(p, 16);
    quint32 fch_crc  = (quint32)p[13]
                     | ((quint32)p[14] << 8)
                     | ((quint32)p[15] << 16);
    info.fch_crc_ok = (calc_crc == fch_crc);
    if (!info.fch_crc_ok) {
        err = QString("FCH CRC24 错(calc=%1 rx=%2)")
              .arg(calc_crc, 6, 16, QChar('0')).arg(fch_crc, 6, 16, QChar('0'));
        return false;
    }

    info.frame_type = (quint8)get_bits(p, 0, 0, 3);
    info.net_type   = (quint8)get_bits(p, 0, 3, 5);
    info.net_id     = (quint32)get_bits(p, 1, 0, 24);
    info.version    = (quint8)get_bits(p, 12, 4, 4);
    return true;
}

// SOF 解析 + PB 重组
void BplcParser::parse_sof_and_assemble(const QByteArray& body, MpduInfo& info,
                                        MsduState& msdu, QByteArray& complete_msdu_body,
                                        QString& err) {
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    info.src_tei     = (quint16)get_bits(p, 4, 0, 12);
    info.dst_tei     = (quint16)get_bits(p, 5, 4, 12);
    info.link_id     = (quint8) get_bits(p, 7, 0, 8);
    info.frame_len   = (quint16)get_bits(p, 8, 0, 12);
    info.pb_num      = (quint8) get_bits(p, 9, 4, 4);
    info.symbol_num  = (quint16)get_bits(p, 10, 0, 9);
    info.bc_flag     = (bool)   get_bits(p, 11, 1, 1);
    info.re_send_flag= (bool)   get_bits(p, 11, 2, 1);
    info.encryp_flag = (bool)   get_bits(p, 11, 3, 1);
    info.tmi         = (quint8) get_bits(p, 11, 4, 4);
    info.tmi_ext     = (quint8) get_bits(p, 12, 0, 4);
    info.pb_size     = get_pb_size(info.tmi, info.tmi_ext);

    if (info.pb_size <= 0 || info.pb_num == 0 || info.pb_num > 4) {
        err = QString("PB 配置非法(tmi=%1 ext=%2 num=%3)")
              .arg(info.tmi).arg(info.tmi_ext).arg(info.pb_num);
        return;
    }

    int block_offset = 16;
    bool all_pb_ok = true;
    for (int i = 0; i < info.pb_num; ++i) {
        int block_start = block_offset + i * info.pb_size;
        if (block_start + info.pb_size > body.size()) {
            err = trl::L("PB 块超出帧长");
            msdu = MsduState{};
            return;
        }
        const quint8* blk = reinterpret_cast<const quint8*>(body.constData()) + block_start;
        quint32 calc = crc24(blk, info.pb_size);
        quint32 rx   = (quint32)blk[info.pb_size - 3]
                     | ((quint32)blk[info.pb_size - 2] << 8)
                     | ((quint32)blk[info.pb_size - 1] << 16);
        if (calc != rx) {
            all_pb_ok = false;
            // 失败块同样记录,供 UI 逐块展示(头仍可读,CRC 状态 FAIL)
            info.pb_heads.append(blk[0]);
            info.pb_crc_oks.append(false);
            continue;
        }
        quint8 pb_head = blk[0];
        bool   is_start = (pb_head & 0x40) != 0;
        bool   is_end   = (pb_head & 0x80) != 0;
        quint8 seq     = pb_head & 0x3F;
        // 保存首块(起始块)的 PB 头原始字节供 UI 显示;多块时后块不覆盖
        if (i == 0) info.pb_head = pb_head;
        info.pb_heads.append(pb_head);       // 按块序保存(UI 逐块展示)
        info.pb_crc_oks.append(true);        // 各块 CRC24 结果(此处已通过)

        int body_len = info.pb_size - 4;
        QByteArray pb_body(reinterpret_cast<const char*>(blk + 1), body_len);

        if (is_start && msdu.received_count == 0) {
            msdu.expected_len = body_len;
            msdu.buffer.resize(info.pb_num * body_len);
            std::fill(msdu.buffer.begin(), msdu.buffer.end(), 0);
            msdu.received_count = 1;
            std::copy(pb_body.begin(), pb_body.end(), msdu.buffer.begin());
        } else if (msdu.received_count > 0 && seq == (quint8)msdu.received_count) {
            int dst = msdu.received_count * msdu.expected_len;
            if (dst + pb_body.size() > msdu.buffer.size()) {
                msdu.buffer.resize(dst + pb_body.size());
            }
            std::copy(pb_body.begin(), pb_body.end(), msdu.buffer.begin() + dst);
            msdu.received_count++;
        } else {
            msdu = MsduState{};
            return;
        }

        if (is_end) {
            complete_msdu_body = msdu.buffer.left(msdu.received_count * msdu.expected_len);
            msdu = MsduState{};
            info.pb_crc_ok = all_pb_ok;
            return;
        }
    }
    info.pb_crc_ok = all_pb_ok;
}

// 主入口
BplcParser::Result BplcParser::parse(const BplcFrame& in, MsduState& msdu, const Filter& f) {
    Result r;
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
        r.reject_reason = QString("NetID 不在白名单: 0x%1")
                         .arg(r.mpdu.net_id, 6, 16, QChar('0'));
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

    if (r.mpdu.frame_type == 1) {
        parse_sof_and_assemble(r.payload_for_log, r.mpdu, msdu, r.msdu_body, err);
        if (!err.isEmpty()) { r.reject_reason = err; r.accept = false; return r; }
        if (f.tei_filter && !f.tei_list.contains(r.mpdu.src_tei)
                        && !f.tei_list.contains(r.mpdu.dst_tei)) {
            r.reject_reason = trl::L("TEI 不在白名单");
            r.accept = false;
            return r;
        }
        // MSDU 重组完整:解析 MAC 层字段
        if (!r.msdu_body.isEmpty()) {
            r.msdu = MsduParser::parse(r.msdu_body);
            // MSDU body 起点在 payload_for_log 中的偏移:FCH 16B + 1B pb_head = 17。
            // 仅当单块(整条 MSDU 连续位于本帧 17..)时才能把字段映射到 raw 高亮;
            // 多块重组时各 pb_body 在 raw 中被 pb_head/CRC 隔断,不连续,置 -1。
            r.msdu_raw_base = (r.mpdu.pb_num == 1) ? 17 : -1;
        }
    } else if (r.mpdu.frame_type == 0) {
        // BEACON:TimeStamp(4,0,32) SourceTEI(8,0,12) TMI(9,4,4)
        //        SymbolNum(10,0,9) LineNum(11,1,2) RSV(11,3,9)
        const quint8* p = reinterpret_cast<const quint8*>(r.payload_for_log.constData());
        r.mpdu.beacon_timestamp = (quint32)get_bits(p, 4, 0, 32);
        r.mpdu.src_tei          = (quint16)get_bits(p, 8, 0, 12);
        r.mpdu.tmi              = (quint8) get_bits(p, 9, 4, 4);
        r.mpdu.symbol_num       = (quint16)get_bits(p, 10, 0, 9);
        r.mpdu.beacon_line      = (quint8) get_bits(p, 11, 1, 2);
        // 载荷区(Beacon Load):BeaconType/NetSN/CCO MAC/周期计数/管理条目
        r.beacon = BeaconParser::parse_beacon(r.payload_for_log);
        if (r.beacon.present) {
            // gb 内字节偏移 16(FCH)为载荷区起点,摘要字段直接从载荷区取值
            const QByteArray& gb = r.payload_for_log;
            const quint8* gp = reinterpret_cast<const quint8*>(gb.constData());
            r.mpdu.beacon_type       = (quint8)get_bits(gp, 16 + 0, 0, 3);
            r.mpdu.beacon_netsn      = (quint8)get_bits(gp, 16 + 1, 0, 8);
            r.mpdu.beacon_cco_mac    = get_bits(gp, 16 + 2, 0, 48);
            r.mpdu.beacon_period_cnt = (quint32)get_bits(gp, 16 + 8, 0, 32);
            // 精简信标(51243 表56)无字节12 信道编号;管理区(条目数)位于
            // 字节12(精简)或 20(标准)
            const bool lite = ((quint8)gb.at(16) & 0x10) != 0;
            r.mpdu.beacon_rf_channel = lite ? 0
                                            : (quint8)get_bits(gp, 16 + 12, 0, 8);
            r.mpdu.beacon_rf_option  = 0;
            r.mpdu.beacon_item_num   = (quint8)gb.at(16 + (lite ? 12 : 20));
        }
    } else if (r.mpdu.frame_type == 2) {
        // ACK FCH:ExtFrameType(12,0,4),然后按类型分支
        const quint8* p = reinterpret_cast<const quint8*>(r.payload_for_log.constData());
        r.mpdu.ack_ext_type = (quint8)get_bits(p, 12, 0, 4);
        if (r.mpdu.ack_ext_type == 0) {
            // 常规 ACK(用户/协议定义,物理字节序):
            // RxRes(4,0,4) RxStatus(4,4,4) SrcTEI(5,0,12) DstTEI(6,4,12)
            // RxPBNum(8,0,3) RSV0(8,3,5) ChQ(9,0,8) Load(10,0,8) RSV1(11,0,8)
            r.mpdu.ack_rx_res      = (quint8)get_bits(p, 4, 0, 4);
            r.mpdu.ack_rx_status   = (quint8)get_bits(p, 4, 4, 4);
            r.mpdu.src_tei         = (quint16)get_bits(p, 5, 0, 12);
            r.mpdu.dst_tei         = (quint16)get_bits(p, 6, 4, 12);
            r.mpdu.ack_rx_pb_num   = (quint8)get_bits(p, 8, 0, 3);
            r.mpdu.ack_rsv0        = (quint8)get_bits(p, 8, 3, 5);
            r.mpdu.ack_channel_quality = (quint8)get_bits(p, 9, 0, 8);
            r.mpdu.ack_sta_load    = (quint8)get_bits(p, 10, 0, 8);
            r.mpdu.ack_rsv1        = (quint8)get_bits(p, 11, 0, 8);
        } else if (r.mpdu.ack_ext_type == 1) {
            // 搜索:DstAddr(4,0,48) SearchStei(10,0,12) SearchFreq(11,4,4)
            r.mpdu.ack_dst_addr    = get_bits(p, 4, 0, 48);
            r.mpdu.ack_search_tei  = (quint16)get_bits(p, 10, 0, 12);
            r.mpdu.ack_search_freq = (quint8)get_bits(p, 11, 4, 4);
        } else if (r.mpdu.ack_ext_type == 2) {
            // 同步:Timestamp(4,0,32) SyncStei(8,0,12)
            r.mpdu.ack_sync_timestamp = (quint32)get_bits(p, 4, 0, 32);
            r.mpdu.ack_sync_tei       = (quint16)get_bits(p, 8, 0, 12);
        } else if (r.mpdu.ack_ext_type == 3) {
            // 切频:DstAddr(4,0,48) HrfChanel(10,0,8) HrfOption(11,0,8)
            r.mpdu.ack_dst_addr    = get_bits(p, 4, 0, 48);
            r.mpdu.ack_rx_pb_num   = (quint8)get_bits(p, 10, 0, 8);  // HrfChanel
            r.mpdu.ack_sta_load    = (quint8)get_bits(p, 11, 0, 8);  // HrfOption
        }
    } else if (r.mpdu.frame_type == 3) {
        // COORD:TimeDuration(4,0,16) NextShift(6,0,16) NeighbourNID(8,0,24)
        //       NetRfChannel(11,0,8) NetRfOption(12,0,2) RSV(12,2,2)
        const quint8* p = reinterpret_cast<const quint8*>(r.payload_for_log.constData());
        r.mpdu.coord_duration     = (quint16)get_bits(p, 4, 0, 16);
        r.mpdu.coord_shift        = (quint16)get_bits(p, 6, 0, 16);
        r.mpdu.coord_neighbour_nid = (quint32)get_bits(p, 8, 0, 24);
        r.mpdu.coord_rf_channel   = (quint8)get_bits(p, 11, 0, 8);
        r.mpdu.coord_rf_option    = (quint8)get_bits(p, 12, 0, 2);
    }

    r.accept = true;
    return r;
}

namespace {
// 解析拒绝/错误文案的中→英词典(显示于 Info 列/错误提示)
struct I18nRegParser {
    I18nRegParser() {
        trl::register_en("空帧", "empty frame");
        trl::register_en("帧长过短", "frame too short");
        trl::register_en("剥头后载荷过短(<4B)", "payload too short after header (<4B)");
        trl::register_en("剥头后空载荷", "empty payload after header");
        trl::register_en("MPDU_BASE 长度不足 16B", "MPDU_BASE shorter than 16B");
        trl::register_en("PB 块超出帧长", "PB block exceeds frame length");
        trl::register_en("HPLC 链路被过滤", "HPLC link filtered");
        trl::register_en("HRF 链路被过滤", "HRF link filtered");
        trl::register_en("BEACON 被过滤", "BEACON filtered");
        trl::register_en("SOF 被过滤", "SOF filtered");
        trl::register_en("ACK 被过滤", "ACK filtered");
        trl::register_en("COORD 被过滤", "COORD filtered");
        trl::register_en("TEI 不在白名单", "TEI not in whitelist");
    }
};
const I18nRegParser g_i18n_reg_parser;
}  // namespace
