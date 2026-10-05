/// @file bplcparser.cpp
/// @brief GW_2022_Parser 实现(总控):物理头剥取 + FCH 公共头 + 帧分发
/// @details 各帧型字段/载荷解析委托给独立模块:
///          BEACON -> GW_2022_BeaconParser(beacon/);SOF -> gw_2022_sof::assemble(sof/);
///          ACK -> gw_2022_ackp::parse_fch(ack/);COORD -> gw_2022_coordp::parse_fch(coord/)。
///          位域/CRC 公共工具见 fieldspec.h。
#include "gw_2022_parser.h"
#include "gw_2022_pb_table.h"
#include "gw_2022_tree.h"
#include "i18n.h"
#include "statistics.h"
#include "gw_2022_msdu_parser.h"
#include "gw_2022_beacon_parser.h"
#include "gw_2022_sof_parser.h"
#include "gw_2022_ack_parser.h"
#include "gw_2022_coord_parser.h"
#include "common/fieldtools.h"
#include "crc.h"
#include "QsLog.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QtEndian>

namespace {

int bcd2dec(quint8 b) {
    return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

}  // namespace

GW_2022_Parser::GW_2022_Parser() {}

// 剥物理层头
bool GW_2022_Parser::decode_envelope(const BplcFrame& in, Result& r) {
    r.meta = in.meta;
    r.arrival_us = in.arrival_us;   // 实时串口帧起始 0x3C 高精度接收时刻
    r.raw_wire   = in.raw_wire;     // 原始串口帧原样(调试复制)
    const QByteArray& d = in.data;

    if (in.meta.from_raw) {
        // 裸 hex 文本(每行一帧):行首字节 = isRF,其后为纯 MPDU
        if (d.size() < 2) { r.reject_reason = trl::L("空帧"); return false; }
        r.meta.is_rf = (quint8(d[0]) != 0);
        r.payload_for_log = d.mid(1);
        // 文本头给出首帧时间(无则回退本地):frame_time 从 arrival 恢复,
        // 使 Time/Delta/再导出以文件头时间为基准
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
            r.meta.epoch_ms = r.meta.frame_time.toMSecsSinceEpoch();
        } catch (...) {
            r.meta.frame_time = QDateTime::currentDateTime();
            r.meta.epoch_ms = r.meta.frame_time.toMSecsSinceEpoch();
        }
    } else {
        // 无时间标签:串口/回放帧 arrival=now(等价本地时间);
        // 裸数据回放帧 arrival=ts 还原的捕获时刻 → 以此恢复 frame_time
        r.meta.frame_time = QDateTime::fromMSecsSinceEpoch(in.arrival_ms);
        r.meta.epoch_ms = in.arrival_ms;   // O(1):arrival_ms 即 epoch ms
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
    payload.remove(0, 4);
    r.payload_for_log = payload;
    if (payload.isEmpty()) {
        r.reject_reason = trl::L("剥头后空载荷");
        return false;
    }

    Q_UNUSED(data_len);
    return true;
}

// MPDU_BASE 公共头(FCH 前 16B:类型/网络/版本/FCH CRC24)
bool GW_2022_Parser::parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err) {
    if (body.size() < 16) { err = trl::L("MPDU_BASE 长度不足 16B"); return false; }
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    quint32 calc_crc = crc24_lsb(p, 16);
    // FCH CRC24 字段小端存储(字节 6c 13 2a → 0x2a136c)
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

// 主入口:帧分发
GW_2022_Parser::Result GW_2022_Parser::parse(const BplcFrame& in, MsduState& msdu, const Filter& f) {
    Result r;
    QElapsedTimer pt;
    pt.start();
    if (!decode_envelope(in, r)) {
        r.accept = false;
        return r;
    }
    const qint64 us_env = pt.nsecsElapsed() / 1000;

    if (!r.meta.is_rf && !f.link_hplc) { r.reject_reason = trl::L("HPLC 链路被过滤"); r.accept = false; return r; }
    if ( r.meta.is_rf && !f.link_hrf ) { r.reject_reason = trl::L("HRF 链路被过滤");  r.accept = false; return r; }

    QString err;
    if (!parse_mpdu_base(r.payload_for_log, r.mpdu, err)) {
        r.reject_reason = err;
        r.accept = false;
        return r;
    }
    const qint64 us_mpdu = pt.nsecsElapsed() / 1000 - us_env;

    if (f.nid_filter && !f.nid_list.contains(r.mpdu.net_id)) {
        r.reject_reason = QString("NetID 不在白名单: 0x%1")
                         .arg(r.mpdu.net_id, 6, 16, QChar('0'));
        r.accept = false;
        return r;
    }

    const GW_2022_FrameType ft = static_cast<GW_2022_FrameType>(r.mpdu.frame_type);
    switch (ft) {
        case GW_2022_FrameType::BEACON: if (!f.allow_beacon) { r.reject_reason = trl::L("BEACON 被过滤"); r.accept = false; return r; } break;
        case GW_2022_FrameType::SOF:    if (!f.allow_sof)    { r.reject_reason = trl::L("SOF 被过滤");    r.accept = false; return r; } break;
        case GW_2022_FrameType::ACK:    if (!f.allow_ack)    { r.reject_reason = trl::L("ACK 被过滤");    r.accept = false; return r; } break;
        case GW_2022_FrameType::COORD:  if (!f.allow_coord)  { r.reject_reason = trl::L("COORD 被过滤");  r.accept = false; return r; } break;
        default: break;
    }

    const quint8* p =
        reinterpret_cast<const quint8*>(r.payload_for_log.constData());

    if (ft == GW_2022_FrameType::SOF) {
        // SOF:FCH 字段 + 多 PB 重组(sof 模块)
        err = gw_2022_sof::assemble(r.payload_for_log, r.mpdu, msdu, r.msdu_body,
                                    r.meta.is_rf ? 0xFF : (quint8)r.meta.channel);
        if (!err.isEmpty()) { r.reject_reason = err; r.accept = false; return r; }
        if (f.tei_filter && !f.tei_list.contains(r.mpdu.src_tei)
                        && !f.tei_list.contains(r.mpdu.dst_tei)) {
            r.reject_reason = trl::L("TEI 不在白名单");
            r.accept = false;
            return r;
        }
        // MSDU 重组完整:解析 MAC 层字段
        if (!r.msdu_body.isEmpty()) {
            r.msdu = GW_2022_MsduParser::parse(r.msdu_body);
            // MSDU body 起点在 payload_for_log 中的偏移:FCH 16B + 1B pb_head = 17。
            // 仅当单块(整条 MSDU 连续位于本帧 17..)时才能把字段映射到 raw 高亮;
            // 多块重组时各 pb_body 在 raw 中被 pb_head/CRC 隔断,不连续,置 -1。
            r.msdu_raw_base = (r.mpdu.pb_num == 1) ? 17 : -1;
        }
    } else if (ft == GW_2022_FrameType::BEACON) {
        // BEACON:FCH 摘要 + 载荷区解析(beacon 模块)
        r.mpdu.beacon_timestamp = (quint32)get_bits(p, 4, 0, 32);
        r.mpdu.src_tei          = (quint16)get_bits(p, 8, 0, 12);
        r.mpdu.tmi              = (quint8) get_bits(p, 9, 4, 4);
        r.mpdu.pb_size          = (quint16)gw_2022_pb_size(r.mpdu.tmi);  // 单块帧块长
        r.mpdu.symbol_num       = (quint16)get_bits(p, 10, 0, 9);
        r.mpdu.beacon_line      = (quint8) get_bits(p, 11, 1, 2);
        r.beacon = GW_2022_BeaconParser::parse_beacon(r.payload_for_log);
        if (r.beacon.present) {
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
    } else if (ft == GW_2022_FrameType::ACK) {
        // ACK(ack 模块)
        gw_2022_ackp::parse_fch(p, r.mpdu);
    } else if (ft == GW_2022_FrameType::COORD) {
        // 网间协调帧 COORD(coord 模块)
        gw_2022_coordp::parse_fch(p, r.mpdu);
    }

    // parse 细分统计(成功路径):env=物理头/FCH, mpdu=MPDU 基础, body=MSDU/BEACON 字段树
    const qint64 us_body = pt.nsecsElapsed() / 1000 - us_env - us_mpdu;
    static std::atomic<quint64> n{0}, env_us{0}, mpdu_us{0}, body_us{0};
    const quint64 c = n.fetch_add(1) + 1;
    env_us.fetch_add(quint64(us_env));
    mpdu_us.fetch_add(quint64(us_mpdu));
    body_us.fetch_add(quint64(us_body));
    if (c % 50000 == 0)
        QLOG_DEBUG() << "parse细分:" << c << "帧: env累计" << env_us.load() / 1000
                     << "ms, mpdu累计" << mpdu_us.load() / 1000
                     << "ms, body累计" << body_us.load() / 1000 << "ms";
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
