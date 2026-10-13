/// @file field_gen.cpp
/// @brief 通用字段树生成器实现（镜像 gw/nw 字段树构建器，产物为数据）
#include "field_gen.h"

#include "gw_2022_tree.h"
#include "i18n.h"
#include "nw_2021_beacon_parser.h"
#include "nw_2021_tree.h"

namespace {

// 英文注册:本文件新增文案(af3259e 同步的 SOF 字段语义)。内置树的同名
// 注册在 tree 文件里,但插件进程不编译 tree 源,故在此自带一份(键值与
// 内置注册逐字一致),保证插件进程英文界面下输出与内置路径相同。
struct I18nReg {
    I18nReg() {
        trl::register_en("广播报文", "broadcast frame");
        trl::register_en("非广播报文", "non-broadcast frame");
        trl::register_en("重传报文", "retransmitted frame");
        trl::register_en("非重传报文", "non-retransmitted frame");
        trl::register_en("预留信息段", "reserved for future encryption");
        trl::register_en("物理层不检查报文 TEI", "physical layer does not check TEI");
        trl::register_en("物理层检查报文 TEI", "physical layer checks TEI");
    }
};
const I18nReg g_i18n_reg_field_gen;

/// 字段累加器：镜像 tree_add_item / tree_add_bit_field 的命名与坐标换算
struct Sink {
    QVector<DissectField>* vec;
    DissectField& add(const QString& name, const QString& value,
                      int start = -1, int len = 0) {
        vec->append(DissectField(name, value, start, len));
        return vec->last();
    }
    static DissectField& add_to(DissectField& parent, const QString& name,
                                const QString& value, int start = -1,
                                int len = 0) {
        parent.children.append(DissectField(name, value, start, len));
        return parent.children.last();
    }
    static DissectField& bit(DissectField& parent, const QString& field,
                             const QString& value, int byte_idx, int bit_off,
                             int bit_len) {
        QString name = field;
        if (bit_len > 0) name += QStringLiteral(" [%1b]").arg(bit_len);
        const int first_bit = byte_idx * 8 + bit_off;
        const int last_bit = first_bit + bit_len - 1;
        const int bs = first_bit / 8;
        const int be = last_bit / 8;
        return add_to(parent, name, value, bs, be - bs + 1);
    }
};

/// MSDU 相对偏移 → raw 片段列表（镜像 tree_msdu_raw_ranges）
struct RawMap {
    int base, pb_num, pb_size, fch_size, header_len, body;
};

QList<QPair<int, int>> raw_ranges(const RawMap& m, int rel_start, int rel_len) {
    QList<QPair<int, int>> out;
    if (rel_start < 0 || rel_len <= 0) return out;
    if (m.base >= 0) {
        out.append(qMakePair(m.base + rel_start, rel_len));
        return out;
    }
    if (m.pb_num <= 0 || m.pb_size <= 4 || m.body <= 0) return out;
    const int end = rel_start + rel_len;
    const int b0 = rel_start / m.body;
    const int b1 = (end - 1) / m.body;
    if (b0 < 0 || b1 >= m.pb_num) return out;
    for (int b = b0; b <= b1; ++b) {
        const int seg_start = qMax(rel_start, b * m.body);
        const int seg_end = qMin(end, (b + 1) * m.body);
        if (seg_end <= seg_start) continue;
        const int raw_off = m.fch_size + b * m.pb_size + m.header_len
                            + (seg_start - b * m.body);
        out.append(qMakePair(raw_off, seg_end - seg_start));
    }
    return out;
}

/// MsduFieldNode 递归转换（镜像 tree_render_msdu，主区间取首片段）
void convert_msdu(DissectField& parent, const QVector<MsduFieldNode>& nodes,
                  const RawMap& m, const QByteArray& msdu_body) {
    for (const auto& n : nodes) {
        DissectField f(n.name, n.value);
        const auto ranges = raw_ranges(m, n.rel_start, n.rel_len);
        if (!ranges.isEmpty()) {
            f.start = ranges.first().first;
            f.len = ranges.first().second;
        }
        parent.children.append(f);
        if (!n.children.isEmpty())
            convert_msdu(parent.children.last(), n.children, m, msdu_body);
    }
}

/// MSDU 段挂载（镜像两构建器的 SOF 分支尾部；header_len/body 按协议传）
void append_msdu_section(Sink& sink, const QByteArray& /*raw*/,
                         const ParseResult& r, int header_len, int body_sub) {
    if (r.msdu.present) {
        DissectField& msdu = sink.add(
            QStringLiteral("MSDU (Reassembled)"),
            QStringLiteral("%1 B  %2").arg(r.msdu_body.size()).arg(r.msdu.summary));
        RawMap rm;
        rm.base = r.msdu_raw_base;
        rm.pb_num = (r.msdu_raw_base >= 0) ? 1 : (r.mpdu.pb_num > 0 ? r.mpdu.pb_num : 1);
        rm.pb_size = r.mpdu.pb_size;
        rm.fch_size = 16;
        rm.header_len = header_len;
        rm.body = r.mpdu.pb_size - body_sub;
        const auto ranges = raw_ranges(rm, 0, r.msdu.total_len);
        if (!ranges.isEmpty()) {
            msdu.start = ranges.first().first;
            msdu.len = ranges.first().second;
        }
        Sink::add_to(msdu, QStringLiteral("Length"),
                     QString::number(r.msdu_body.size()));
        convert_msdu(msdu, r.msdu.tree, rm, r.msdu_body);
    } else if (!r.msdu_body.isEmpty()) {
        DissectField& msdu = sink.add(QStringLiteral("MSDU (Reassembled)"),
                                      QStringLiteral("%1 B").arg(r.msdu_body.size()));
        Sink::add_to(msdu, QStringLiteral("Length"),
                     QString::number(r.msdu_body.size()));
        Sink::add_to(msdu, QStringLiteral("Hex"),
                     QString(r.msdu_body.toHex(' ')));
    }
}

}  // namespace

namespace dissect_gen {

QVector<DissectField> gw_fields(const QByteArray& raw, const ParseResult& r) {
    QVector<DissectField> out;
    Sink sink{&out};
    const MpduInfo& mpdu = r.mpdu;

    DissectField& base = sink.add(QStringLiteral("MPDU Base"), QString(), 0, 16);
    Sink::bit(base, QStringLiteral("Frame Type"),
              QStringLiteral("%1 (%2)").arg(mpdu.frame_type_name()).arg(mpdu.frame_type),
              0, 0, 3);
    Sink::bit(base, QStringLiteral("Net Type"), QString::number(mpdu.net_type), 0, 3, 5);
    Sink::bit(base, QStringLiteral("Net ID"),
              QStringLiteral("0x%1").arg(mpdu.net_id, 6, 16, QChar('0')), 1, 0, 24);
    {
        QString ver;
        switch (static_cast<GW_2022_Version>(mpdu.version)) {
        case GW_2022_Version::BPLC: ver = QStringLiteral("1 (BPLC)"); break;
        case GW_2022_Version::SPLC: ver = QStringLiteral("2 (SPLC)"); break;
        default: ver = QStringLiteral("%1 (Reserved)").arg(mpdu.version); break;
        }
        Sink::bit(base, QStringLiteral("Version"), ver, 12, 4, 4);
    }
    Sink::bit(base, QStringLiteral("FCH CRC24"), mpdu.fch_crc_ok ? "OK" : "FAIL",
              13, 0, 24);

    switch (static_cast<GW_2022_FrameType>(mpdu.frame_type)) {
    case GW_2022_FrameType::BEACON: {
        DissectField& bcn = sink.add(QStringLiteral("BEACON"), QString());
        Sink::bit(bcn, QStringLiteral("TimeStamp"),
                  QStringLiteral("0x%1").arg(mpdu.beacon_timestamp, 8, 16, QChar('0')),
                  4, 0, 32);
        Sink::bit(bcn, QStringLiteral("Source TEI"), QString::number(mpdu.src_tei), 8, 0, 12);
        Sink::bit(bcn, QStringLiteral("TMI"), QString::number(mpdu.tmi), 9, 4, 4);
        Sink::bit(bcn, QStringLiteral("Symbol Num"), QString::number(mpdu.symbol_num), 10, 0, 9);
        Sink::bit(bcn, QStringLiteral("Line"), QString::number(mpdu.beacon_line), 11, 1, 2);
        if (mpdu.pb_size > 0)
            Sink::add_to(bcn, QStringLiteral("PB Size"), QString::number(mpdu.pb_size));
        if (r.beacon.present) {
            DissectField& load = Sink::add_to(bcn, QStringLiteral("Beacon Load"), QString());
            RawMap rm;
            rm.base = 16; rm.pb_num = 1; rm.pb_size = 0;
            rm.fch_size = 16; rm.header_len = 1; rm.body = 0;
            convert_msdu(load, r.beacon.tree, rm, raw.mid(16));
        }
        break;
    }
    case GW_2022_FrameType::SOF: {
        DissectField& sof = sink.add(QStringLiteral("SOF"), QString());
        Sink::bit(sof, QStringLiteral("Source TEI"), QString::number(mpdu.src_tei), 4, 0, 12);
        Sink::bit(sof, QStringLiteral("Destination TEI"), QString::number(mpdu.dst_tei), 5, 4, 12);
        {
            QString link_desc;
            if (mpdu.link_id <= 3) link_desc = trl::L("报文优先级(越小越低)");
            else if (mpdu.link_id <= 254) link_desc = trl::L("业务分类LID");
            else link_desc = trl::L("无效值");
            Sink::bit(sof, QStringLiteral("Link ID"),
                      QStringLiteral("0x%1 - %2").arg(mpdu.link_id, 2, 16, QChar('0')).arg(link_desc),
                      7, 0, 8);
        }
        Sink::bit(sof, QStringLiteral("Frame Length"),
                  QStringLiteral("%1 (x10us)").arg(mpdu.frame_len), 8, 0, 12);
        Sink::bit(sof, QStringLiteral("PB Num"), QString::number(mpdu.pb_num), 9, 4, 4);
        Sink::bit(sof, QStringLiteral("Symbol Num"), QString::number(mpdu.symbol_num), 10, 0, 9);
        // 广播标志位(表21):0=非广播报文 1=广播报文(与内置树 af3259e 后文案一致)
        Sink::bit(sof, QStringLiteral("Broadcast"),
                  mpdu.bc_flag ? QStringLiteral("1 - %1").arg(trl::L("广播报文"))
                               : QStringLiteral("0 - %1").arg(trl::L("非广播报文")),
                  11, 1, 1);
        // 重传标志位(表22):0=非重传报文 1=重传报文
        Sink::bit(sof, QStringLiteral("ReSend"),
                  mpdu.re_send_flag ? QStringLiteral("1 - %1").arg(trl::L("重传报文"))
                                    : QStringLiteral("0 - %1").arg(trl::L("非重传报文")),
                  11, 2, 1);
        // 加密标志位:预留信息段(文档无 0/1 标准语义)
        Sink::bit(sof, QStringLiteral("Encrypted"),
                  QStringLiteral("%1 (%2)").arg(mpdu.encryp_flag).arg(trl::L("预留信息段")),
                  11, 3, 1);
        Sink::bit(sof, QStringLiteral("TMI"), QString::number(mpdu.tmi), 11, 4, 4);
        Sink::bit(sof, QStringLiteral("TMI_EXT"), QString::number(mpdu.tmi_ext), 12, 0, 4);
        Sink::add_to(sof, QStringLiteral("PB Size"), QString::number(mpdu.pb_size));
        {
            const int header_len = 1;
            const int body_len = mpdu.pb_size - 4;
            const int pb_num = (mpdu.pb_num > 0) ? mpdu.pb_num : 1;
            const quint8* d = reinterpret_cast<const quint8*>(raw.constData());
            const int raw_sz = raw.size();
            for (int bi = 0; bi < pb_num; ++bi) {
                const int blk_off = 16 + bi * mpdu.pb_size;
                const bool multi = pb_num > 1;
                quint8 h = (bi < mpdu.pb_heads.size())
                               ? mpdu.pb_heads[bi]
                               : (blk_off < raw_sz ? d[blk_off] : 0);
                QStringList flags;
                if (h & 0x80) flags << QStringLiteral("END");
                if (h & 0x40) flags << QStringLiteral("START");
                QString desc = flags.isEmpty() ? QStringLiteral("(continuation)")
                                               : flags.join(QLatin1Char('|'));
                Sink::bit(sof,
                          multi ? QStringLiteral("PB Header (%1/%2)").arg(bi).arg(pb_num)
                                : QStringLiteral("PB Header"),
                          QStringLiteral("0x%1 (%2, seq=%3)")
                              .arg(h, 2, 16, QChar('0')).arg(desc).arg(h & 0x3F),
                          blk_off, 0, 8);
                if (body_len > 0 && blk_off + header_len < raw_sz) {
                    DissectField& pb = Sink::add_to(
                        sof,
                        multi ? QStringLiteral("PB Body (%1/%2)").arg(bi).arg(pb_num)
                              : QStringLiteral("PB Body"),
                        QStringLiteral("%1 B").arg(body_len),
                        blk_off + header_len, body_len);
                    if (r.msdu.present && r.msdu.total_len > 0) {
                        int buf_off = bi * body_len;
                        int msdu_end_in_blk = r.msdu.total_len - buf_off;
                        if (msdu_end_in_blk > 0 && msdu_end_in_blk < body_len) {
                            Sink::add_to(pb, QStringLiteral("PB Padding"),
                                         QStringLiteral("%1 B (0x00 fill)")
                                             .arg(body_len - msdu_end_in_blk),
                                         blk_off + header_len + msdu_end_in_blk,
                                         body_len - msdu_end_in_blk);
                        }
                    }
                }
                bool have = (blk_off + mpdu.pb_size) <= raw_sz;
                quint32 crc = 0;
                if (have) {
                    int o = blk_off + mpdu.pb_size - 3;
                    crc = (quint32)d[o] | ((quint32)d[o + 1] << 8) | ((quint32)d[o + 2] << 16);
                }
                bool okb = (bi < mpdu.pb_crc_oks.size()) ? mpdu.pb_crc_oks[bi]
                                                         : mpdu.pb_crc_ok;
                Sink::add_to(sof,
                             multi ? QStringLiteral("PB CRC24 (block %1/%2)").arg(bi).arg(pb_num)
                                   : QStringLiteral("PB CRC24"),
                             have ? QStringLiteral("0x%1 %2").arg(crc, 6, 16, QChar('0')).arg(okb ? "OK" : "FAIL")
                                  : (okb ? QStringLiteral("OK") : QStringLiteral("FAIL")),
                             blk_off + mpdu.pb_size - 3, 3);
            }
        }
        append_msdu_section(sink, raw, r, 1, 4);
        break;
    }
    case GW_2022_FrameType::ACK: {
        DissectField& ack = sink.add(QStringLiteral("ACK"), QString());
        switch (static_cast<GW_2022_AckExtType>(mpdu.ack_ext_type)) {
        case GW_2022_AckExtType::Normal: {
            const quint8 rxres = mpdu.ack_rx_res;
            QString rv;
            if (static_cast<GW_2022_AckRxRes>(rxres) == GW_2022_AckRxRes::RECEIPT_OK)
                rv = QStringLiteral("0 - Receipt OK (all PB CRC passed)");
            else if (static_cast<GW_2022_AckRxRes>(rxres) == GW_2022_AckRxRes::RECEIPT_FAIL)
                rv = QStringLiteral("1 - Receipt FAIL (≥1 PB CRC failed)");
            else
                rv = QStringLiteral("%1 - Reserved").arg(rxres);
            Sink::bit(ack, QStringLiteral("RxRes"), rv, 4, 0, 4);
            {
                const quint8 st = mpdu.ack_rx_status;
                QStringList oks;
                for (int i = 0; i < 4; ++i)
                    if (st & (1u << i)) oks << QStringLiteral("PB%1").arg(i + 1);
                Sink::bit(ack, QStringLiteral("RxStatus"),
                          QStringLiteral("0x%1 (%2 CRC ok)")
                              .arg(st, 1, 16, QChar('0'))
                              .arg(oks.isEmpty() ? QStringLiteral("no PB") : oks.join(QLatin1String(", "))),
                          4, 4, 4);
            }
            Sink::bit(ack, QStringLiteral("Source TEI"), QString::number(mpdu.src_tei), 5, 0, 12);
            Sink::bit(ack, QStringLiteral("Destination TEI"), QString::number(mpdu.dst_tei), 6, 4, 12);
            Sink::bit(ack, QStringLiteral("RxPBNum"), QString::number(mpdu.ack_rx_pb_num), 8, 0, 3);
            Sink::bit(ack, QStringLiteral("RSV0"), QString::number(mpdu.ack_rsv0), 8, 3, 5);
            Sink::bit(ack, QStringLiteral("ChannelQuality"), QStringLiteral("%1 dB").arg(mpdu.ack_channel_quality), 9, 0, 8);
            Sink::bit(ack, QStringLiteral("STALoad"), QString::number(mpdu.ack_sta_load), 10, 0, 8);
            Sink::bit(ack, QStringLiteral("RSV1"), QString::number(mpdu.ack_rsv1), 11, 0, 8);
            Sink::bit(ack, QStringLiteral("ExtFrameType"), QString::number(mpdu.ack_ext_type), 12, 0, 4);
            break;
        }
        case GW_2022_AckExtType::Search:
            Sink::bit(ack, QStringLiteral("DstAddr"),
                      QStringLiteral("0x%1").arg(mpdu.ack_dst_addr, 12, 16, QChar('0')), 4, 0, 48);
            Sink::bit(ack, QStringLiteral("SearchTEI"), QString::number(mpdu.ack_search_tei), 10, 0, 12);
            Sink::bit(ack, QStringLiteral("SearchFreq"), QString::number(mpdu.ack_search_freq), 11, 4, 4);
            break;
        case GW_2022_AckExtType::Sync:
            Sink::bit(ack, QStringLiteral("Timestamp"),
                      QStringLiteral("0x%1").arg(mpdu.ack_sync_timestamp, 8, 16, QChar('0')), 4, 0, 32);
            Sink::bit(ack, QStringLiteral("SyncTEI"), QString::number(mpdu.ack_sync_tei), 8, 0, 12);
            break;
        case GW_2022_AckExtType::SwitchChannel:
            Sink::bit(ack, QStringLiteral("DstAddr"),
                      QStringLiteral("0x%1").arg(mpdu.ack_dst_addr, 12, 16, QChar('0')), 4, 0, 48);
            Sink::bit(ack, QStringLiteral("HRF Channel"), QString::number(mpdu.ack_rx_pb_num), 10, 0, 8);
            Sink::bit(ack, QStringLiteral("HRF Option"), QString::number(mpdu.ack_sta_load), 11, 0, 8);
            break;
        default:
            Sink::add_to(ack, QStringLiteral("Reserved ExtFrameType"), QString::number(mpdu.ack_ext_type));
            break;
        }
        break;
    }
    case GW_2022_FrameType::COORD: {
        DissectField& coord = sink.add(QStringLiteral("COORD"), QString());
        Sink::bit(coord, QStringLiteral("TimeDuration"), QStringLiteral("%1 ms").arg(mpdu.coord_duration), 4, 0, 16);
        Sink::bit(coord, QStringLiteral("NextTimeSlotShift"), QStringLiteral("%1 ms").arg(mpdu.coord_shift), 6, 0, 16);
        Sink::bit(coord, QStringLiteral("NeighbourNID"),
                  QStringLiteral("0x%1").arg(mpdu.coord_neighbour_nid, 6, 16, QChar('0')), 8, 0, 24);
        Sink::bit(coord, QStringLiteral("NetRfChannel"), QString::number(mpdu.coord_rf_channel), 11, 0, 8);
        Sink::bit(coord, QStringLiteral("RSV0"), QString::number(mpdu.coord_rsv0), 12, 0, 4);
        break;
    }
    default:
        break;
    }
    return out;
}

QVector<DissectField> nw_fields(const QByteArray& raw, const ParseResult& r) {
    QVector<DissectField> out;
    Sink sink{&out};
    const MpduInfo& mpdu = r.mpdu;

    DissectField& base = sink.add(QStringLiteral("MPDU Base"), QString(), 0, 16);
    Sink::bit(base, QStringLiteral("Frame Type"),
              QStringLiteral("%1 (%2)").arg(mpdu.frame_type_name()).arg(mpdu.frame_type),
              0, 0, 3);
    {
        QString conind = (mpdu.net_type == 1)
            ? QStringLiteral("1 - %1").arg(trl::L("载波接入网络传输"))
            : QStringLiteral("0 - %1").arg(trl::L("保留"));
        Sink::bit(base, QStringLiteral("ConInd"), conind, 0, 3, 1);
    }
    Sink::bit(base, QStringLiteral("SNID"),
              QStringLiteral("0x%1").arg(mpdu.net_id, 1, 16, QChar('0')), 0, 4, 4);
    {
        QString ver = (mpdu.version == 1)
            ? QStringLiteral("1 - %1").arg(trl::L("本标准版本号"))
            : QStringLiteral("%1 - %2").arg(mpdu.version).arg(trl::L("保留"));
        Sink::bit(base, QStringLiteral("Version"), ver, 12, 4, 4);
    }
    Sink::bit(base, QStringLiteral("FCH CRC24"), mpdu.fch_crc_ok ? "OK" : "FAIL",
              13, 0, 24);

    switch (static_cast<NW_2021_FrameType>(mpdu.frame_type)) {
    case NW_2021_FrameType::BEACON: {
        DissectField& bcn = sink.add(QStringLiteral("BEACON"), QString());
        Sink::bit(bcn, QStringLiteral("TimeStamp"),
                  QStringLiteral("0x%1").arg(mpdu.beacon_timestamp, 8, 16, QChar('0')),
                  1, 0, 32);
        Sink::bit(bcn, QStringLiteral("BeaconPeriodCount"), QString::number(mpdu.beacon_period_cnt), 5, 0, 32);
        Sink::bit(bcn, QStringLiteral("Source TEI"), QString::number(mpdu.src_tei), 9, 0, 12);
        if (r.meta.is_rf) {
            Sink::bit(bcn, QStringLiteral("PBLen"), QString::number(mpdu.tmi), 11, 0, 4);
        } else {
            Sink::bit(bcn, QStringLiteral("TMI"), QString::number(mpdu.tmi), 10, 4, 4);
            Sink::bit(bcn, QStringLiteral("Symbol Num"), QString::number(mpdu.symbol_num), 11, 0, 9);
            {
                const quint8 b12 = (raw.size() > 12) ? (quint8)raw[12] : 0;
                const quint8 rsv = (b12 >> 1) & 0x01;
                Sink::bit(bcn, QStringLiteral("RSV"), QString::number(rsv), 12, 1, 1);
            }
            QString line;
            switch (static_cast<NW_2021_PhaseLine>(mpdu.beacon_line)) {
            case NW_2021_PhaseLine::ALL_LINES: line = trl::L("未知相线"); break;
            case NW_2021_PhaseLine::LINE_A:    line = trl::L("A相线"); break;
            case NW_2021_PhaseLine::LINE_B:    line = trl::L("B相线"); break;
            case NW_2021_PhaseLine::LINE_C:    line = trl::L("C相线"); break;
            default: line = QString::number(mpdu.beacon_line); break;
            }
            Sink::bit(bcn, QStringLiteral("Line"),
                      QStringLiteral("%1 - %2").arg(mpdu.beacon_line).arg(line), 12, 2, 2);
        }
        if (mpdu.pb_size > 0)
            Sink::add_to(bcn, QStringLiteral("PB Size"), QString::number(mpdu.pb_size));
        if (r.beacon.present) {
            DissectField& load = Sink::add_to(bcn, QStringLiteral("Beacon Load"), QString());
            RawMap rm;
            rm.base = 16; rm.pb_num = 1; rm.pb_size = 0;
            rm.fch_size = 16; rm.header_len = 4; rm.body = 0;
            convert_msdu(load, r.beacon.tree, rm, raw.mid(16));
        }
        break;
    }
    case NW_2021_FrameType::SOF: {
        DissectField& sof = sink.add(QStringLiteral("SOF"), QString());
        Sink::bit(sof, QStringLiteral("Source TEI"), QString::number(mpdu.src_tei), 1, 0, 12);
        Sink::bit(sof, QStringLiteral("Destination TEI"), QString::number(mpdu.dst_tei), 2, 4, 12);
        Sink::bit(sof, QStringLiteral("Link ID"), QString::number(mpdu.link_id), 4, 0, 8);
        // SOF 字段集不区分载波/无线(与内置树逐行对齐,单套渲染):
        // 字段值由解析器按介质填入 MpduInfo,保留字段同内置树从原始字节取
        const quint8* d = reinterpret_cast<const quint8*>(raw.constData());
        const int raw_sz = raw.size();
        const quint16 rsv0 = (raw_sz > 6) ? (quint16)(d[5] | (d[6] << 8)) : 0;
        Sink::bit(sof, QStringLiteral("RSV"), QStringLiteral("0x%1").arg(rsv0, 4, 16, QChar('0')), 5, 0, 16);
        Sink::bit(sof, QStringLiteral("PB Num"), QString::number(mpdu.pb_num), 7, 0, 4);
        Sink::bit(sof, QStringLiteral("TMI"), QString::number(mpdu.tmi), 7, 4, 4);
        Sink::bit(sof, QStringLiteral("Frame Length"), QStringLiteral("%1 (x10us)").arg(mpdu.frame_len), 8, 0, 12);
        const quint16 rsv1 = (raw_sz > 10) ? (quint16)(((d[9] >> 4) & 0x0F) | ((d[10] & 0x1F) << 4)) : 0;
        Sink::bit(sof, QStringLiteral("RSV"), QStringLiteral("0x%1").arg(rsv1, 3, 16, QChar('0')), 9, 4, 9);
        const quint8 tei_filter = (raw_sz > 10) ? ((d[10] >> 5) & 0x01) : 1;
        Sink::bit(sof, QStringLiteral("TEI Filter"),
                  tei_filter ? QStringLiteral("1 - %1").arg(trl::L("物理层不检查报文 TEI"))
                             : QStringLiteral("0 - %1").arg(trl::L("物理层检查报文 TEI")),
                  10, 5, 1);
        Sink::bit(sof, QStringLiteral("ReSend"),
                  mpdu.re_send_flag ? QStringLiteral("1 - %1").arg(trl::L("重传报文"))
                                    : QStringLiteral("0 - %1").arg(trl::L("非重传报文")),
                  10, 6, 1);
        Sink::bit(sof, QStringLiteral("Symbol Num"), QString::number(mpdu.symbol_num), 10, 7, 9);
        Sink::bit(sof, QStringLiteral("TMI_EXT"), QString::number(mpdu.tmi_ext), 12, 0, 4);
        Sink::add_to(sof, QStringLiteral("PB Size"), QString::number(mpdu.pb_size));
        {
            const int header_len = 4;
            const int body_len = mpdu.pb_size - 8;
            const int pb_num = (mpdu.pb_num > 0) ? mpdu.pb_num : 1;
            const quint8* d = reinterpret_cast<const quint8*>(raw.constData());
            const int raw_sz = raw.size();
            for (int bi = 0; bi < pb_num; ++bi) {
                const int blk_off = 16 + bi * mpdu.pb_size;
                const bool multi = pb_num > 1;
                {
                    quint16 seq = 0;
                    quint16 rsv = 0;
                    if (blk_off + 4 <= raw_sz) {
                        seq = (quint16)(d[blk_off] | (d[blk_off + 1] << 8));
                        rsv = (quint16)(d[blk_off + 2] | (d[blk_off + 3] << 8));
                    }
                    DissectField& pbh = Sink::add_to(
                        sof,
                        multi ? QStringLiteral("PB Header (%1/%2)").arg(bi).arg(pb_num)
                              : QStringLiteral("PB Header"),
                        QStringLiteral("4 B"), blk_off, header_len);
                    Sink::bit(pbh, QStringLiteral("SeqNum"), QString::number(seq), blk_off, 0, 16);
                    Sink::bit(pbh, QStringLiteral("RSV"), QStringLiteral("0x%1").arg(rsv, 4, 16, QChar('0')),
                              blk_off + 2, 0, 16);
                }
                if (body_len > 0 && blk_off + header_len < raw_sz) {
                    DissectField& pb = Sink::add_to(
                        sof,
                        multi ? QStringLiteral("PB Body (%1/%2)").arg(bi).arg(pb_num)
                              : QStringLiteral("PB Body"),
                        QStringLiteral("%1 B").arg(body_len),
                        blk_off + header_len, body_len);
                    if (r.msdu.present && r.msdu.total_len > 0) {
                        int buf_off = bi * body_len;
                        int msdu_end_in_blk = r.msdu.total_len - buf_off;
                        if (msdu_end_in_blk > 0 && msdu_end_in_blk < body_len) {
                            Sink::add_to(pb, QStringLiteral("PB Padding"),
                                         QStringLiteral("%1 B (0x00 fill)")
                                             .arg(body_len - msdu_end_in_blk),
                                         blk_off + header_len + msdu_end_in_blk,
                                         body_len - msdu_end_in_blk);
                        }
                    }
                }
                {
                    const int rsv_off = blk_off + mpdu.pb_size - 4;
                    const quint8 rsv = (rsv_off >= 0 && rsv_off < raw_sz) ? d[rsv_off] : 0;
                    Sink::bit(sof, QStringLiteral("RSV"), QStringLiteral("0x%1").arg(rsv, 2, 16, QChar('0')),
                              rsv_off, 0, 8);
                }
                bool have = (blk_off + mpdu.pb_size) <= raw_sz;
                quint32 crc = 0;
                if (have) {
                    int o = blk_off + mpdu.pb_size - 3;
                    crc = (quint32)d[o] | ((quint32)d[o + 1] << 8) | ((quint32)d[o + 2] << 16);
                }
                bool okb = (bi < mpdu.pb_crc_oks.size()) ? mpdu.pb_crc_oks[bi]
                                                         : mpdu.pb_crc_ok;
                Sink::add_to(sof,
                             multi ? QStringLiteral("PB CRC24 (block %1/%2)").arg(bi).arg(pb_num)
                                   : QStringLiteral("PB CRC24"),
                             have ? QStringLiteral("0x%1 %2").arg(crc, 6, 16, QChar('0')).arg(okb ? "OK" : "FAIL")
                                  : (okb ? QStringLiteral("OK") : QStringLiteral("FAIL")),
                             blk_off + mpdu.pb_size - 3, 3);
            }
        }
        append_msdu_section(sink, raw, r, 4, 8);
        break;
    }
    case NW_2021_FrameType::ACK: {
        DissectField& ack = sink.add(QStringLiteral("ACK"), QString());
        const quint8 et = mpdu.ack_ext_type;
        QString ext_ev;
        switch (static_cast<NW_2021_AckExtType>(et)) {
        case NW_2021_AckExtType::Normal:        ext_ev = QStringLiteral("0 - %1").arg(trl::L("选择确认帧")); break;
        case NW_2021_AckExtType::Search:        ext_ev = QStringLiteral("1 - %1").arg(trl::L("网络搜索帧(抄控器)")); break;
        case NW_2021_AckExtType::Sync:          ext_ev = QStringLiteral("2 - %1").arg(trl::L("同步帧(抄控器)")); break;
        case NW_2021_AckExtType::SwitchChannel: ext_ev = QStringLiteral("3 - %1").arg(trl::L("无线切频")); break;
        default: ext_ev = QStringLiteral("%1 - %2").arg(et).arg(trl::L("保留")); break;
        }
        switch (static_cast<NW_2021_AckExtType>(et)) {
        case NW_2021_AckExtType::Normal: {
            const quint8 r = mpdu.ack_rx_res;
            QString rv;
            if (r == 0) rv = QStringLiteral("0 - %1").arg(trl::L("SOF帧全部接收成功"));
            else if (r == 1) rv = QStringLiteral("1 - %1").arg(trl::L("物理块存在CRC校验失败"));
            else rv = QStringLiteral("%1 - %2").arg(r).arg(trl::L("保留"));
            Sink::bit(ack, QStringLiteral("RxRes"), rv, 1, 0, 4);
            {
                const quint8 st = mpdu.ack_rx_status;
                QStringList oks;
                for (int i = 0; i < 4; ++i)
                    if (st & (1u << i)) oks << QStringLiteral("PB%1").arg(i);
                Sink::bit(ack, QStringLiteral("RxStatus"),
                          QStringLiteral("0x%1 (%2)")
                              .arg(st, 1, 16, QChar('0'))
                              .arg(oks.isEmpty() ? trl::L("全部校验失败")
                                                 : oks.join(QLatin1String(", ")) + QStringLiteral(" %1").arg(trl::L("校验成功"))),
                          1, 4, 4);
            }
            Sink::bit(ack, QStringLiteral("Destination TEI"), QString::number(mpdu.dst_tei), 2, 0, 12);
            Sink::bit(ack, QStringLiteral("RxPBNum"),
                      QStringLiteral("%1 (%2)").arg(mpdu.ack_rx_pb_num).arg(trl::L("含解析错误")),
                      3, 4, 4);
            {
                quint64 rsv = 0;
                const int avail = raw.size() - 4;
                for (int i = 0; i < 8 && i < avail; ++i)
                    rsv = (rsv << 8) | (quint8)raw[4 + i];
                Sink::bit(ack, QStringLiteral("Reserved"),
                          QStringLiteral("0x%1").arg(rsv, 16, 16, QChar('0')), 4, 0, 64);
            }
            Sink::bit(ack, QStringLiteral("ExtType"), ext_ev, 12, 0, 4);
            break;
        }
        case NW_2021_AckExtType::Search:
            Sink::bit(ack, QStringLiteral("DstAddr"),
                      QStringLiteral("0x%1").arg(mpdu.ack_dst_addr, 12, 16, QChar('0')), 1, 0, 48);
            Sink::bit(ack, QStringLiteral("SearchTEI"), QString::number(mpdu.ack_search_tei), 7, 0, 12);
            Sink::bit(ack, QStringLiteral("ExtType"), ext_ev, 12, 0, 4);
            break;
        case NW_2021_AckExtType::Sync:
            Sink::bit(ack, QStringLiteral("Timestamp"),
                      QStringLiteral("0x%1").arg(mpdu.ack_sync_timestamp, 8, 16, QChar('0')), 1, 0, 32);
            Sink::bit(ack, QStringLiteral("SyncTEI"), QString::number(mpdu.ack_sync_tei), 5, 0, 12);
            Sink::bit(ack, QStringLiteral("ExtType"), ext_ev, 12, 0, 4);
            break;
        case NW_2021_AckExtType::SwitchChannel:
            Sink::bit(ack, QStringLiteral("DstAddr"),
                      QStringLiteral("0x%1").arg(mpdu.ack_dst_addr, 12, 16, QChar('0')), 1, 0, 48);
            Sink::bit(ack, QStringLiteral("Channel"), QString::number(mpdu.ack_channel_quality), 7, 0, 8);
            Sink::bit(ack, QStringLiteral("Option"), QString::number(mpdu.ack_sta_load), 8, 0, 8);
            Sink::bit(ack, QStringLiteral("ExtType"), ext_ev, 12, 0, 4);
            break;
        default:
            Sink::add_to(ack, QStringLiteral("Reserved ExtType"), QString::number(mpdu.ack_ext_type));
            break;
        }
        break;
    }
    case NW_2021_FrameType::COORD: {
        DissectField& coord = sink.add(QStringLiteral("COORD"), QString());
        Sink::bit(coord, QStringLiteral("NeighbourNIDBitMap"),
                  QStringLiteral("0x%1").arg(mpdu.coord_neighbour_nid, 4, 16, QChar('0')), 1, 0, 16);
        Sink::bit(coord, QStringLiteral("Channel"), QString::number(mpdu.coord_rf_channel), 3, 0, 8);
        Sink::bit(coord, QStringLiteral("RSV1"),
                  QStringLiteral("0x%1").arg(mpdu.coord_rsv1, 3, 16, QChar('0')), 4, 0, 10);
        Sink::bit(coord, QStringLiteral("TimeDuration"), QStringLiteral("%1 (x40ms)").arg(mpdu.coord_duration), 5, 2, 14);
        Sink::bit(coord, QStringLiteral("RSV2"), QString::number(mpdu.coord_rsv2), 7, 0, 1);
        Sink::bit(coord, QStringLiteral("BandSlotEndFlag"), QString::number(mpdu.coord_band_end_flag), 7, 1, 1);
        Sink::bit(coord, QStringLiteral("Option"), QString::number(mpdu.coord_option), 7, 2, 2);
        Sink::bit(coord, QStringLiteral("RSV3"),
                  QStringLiteral("0x%1").arg(mpdu.coord_rsv3, 1, 16, QChar('0')), 7, 4, 4);
        Sink::bit(coord, QStringLiteral("BandSlotEndOffset"), QStringLiteral("%1 (x4ms)").arg(mpdu.coord_band_end_offset), 8, 0, 16);
        Sink::bit(coord, QStringLiteral("BandSlotStartOffset"), QStringLiteral("%1 (x4ms)").arg(mpdu.coord_band_start_offset), 10, 0, 16);
        Sink::bit(coord, QStringLiteral("RSV0"),
                  QStringLiteral("0x%1").arg(mpdu.coord_rsv0, 1, 16, QChar('0')), 12, 0, 4);
        break;
    }
    default:
        break;
    }
    return out;
}

}  // namespace dissect_gen
