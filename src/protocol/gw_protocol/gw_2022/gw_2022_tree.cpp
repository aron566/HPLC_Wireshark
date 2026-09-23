/// @file gw_2022_tree.cpp
/// @brief 国网 GW_2022 协议字段树构建器实现
#include "gw_2022_tree.h"
#include "i18n.h"

namespace {

// Link ID 中文释义(国网 SOF 链路标识符)
struct I18nReg {
    I18nReg() {
        trl::register_en("报文优先级(越小越低)", "message priority (smaller = lower)");
        trl::register_en("业务分类LID", "service-class LID");
        trl::register_en("无效值", "invalid value");
    }
};
const I18nReg g_i18n_reg_gw_tree;

}  // namespace

void GW_2022_TreeBuilder::build(QTreeWidgetItem* root, const PacketEntry& e) {
    // ── MPDU Base(16B 帧控制) ──
    auto* mpdu_base = tree_add_item(root, "MPDU Base", "", 0, 16);
    tree_add_bit_field(mpdu_base, "Frame Type",
        QStringLiteral("%1 (%2)").arg(e.mpdu.frame_type_name()).arg(e.mpdu.frame_type),
        0, 0, 3);
    tree_add_bit_field(mpdu_base, "Net Type", QString::number(e.mpdu.net_type), 0, 3, 5);
    tree_add_bit_field(mpdu_base, "Net ID", QStringLiteral("0x%1")
        .arg(e.mpdu.net_id, 6, 16, QChar('0')), 1, 0, 24);
    {
        // 版本号:0=保留 1=BPLC 2=SPLC
        QString ver;
        switch (static_cast<GW_2022_Version>(e.mpdu.version)) {
        case GW_2022_Version::BPLC: ver = QStringLiteral("1 (BPLC)"); break;
        case GW_2022_Version::SPLC: ver = QStringLiteral("2 (SPLC)"); break;
        default: ver = QStringLiteral("%1 (Reserved)").arg(e.mpdu.version); break;
        }
        tree_add_bit_field(mpdu_base, "Version", ver, 12, 4, 4);
    }
    tree_add_bit_field(mpdu_base, "FCH CRC24", e.mpdu.fch_crc_ok ? "OK" : "FAIL", 13, 0, 24);

    switch (static_cast<GW_2022_FrameType>(e.mpdu.frame_type)) {
    case GW_2022_FrameType::BEACON: {
        auto* bcn = tree_add_item(root, "BEACON", "");
        tree_add_bit_field(bcn, "TimeStamp", QStringLiteral("0x%1")
            .arg(e.mpdu.beacon_timestamp, 8, 16, QChar('0')), 4, 0, 32);
        tree_add_bit_field(bcn, "Source TEI", QString::number(e.mpdu.src_tei), 8, 0, 12);
        tree_add_bit_field(bcn, "TMI", QString::number(e.mpdu.tmi), 9, 4, 4);
        tree_add_bit_field(bcn, "Symbol Num", QString::number(e.mpdu.symbol_num), 10, 0, 9);
        tree_add_bit_field(bcn, "Line", QString::number(e.mpdu.beacon_line), 11, 1, 2);
        if (e.mpdu.pb_size > 0)
            tree_add_item(bcn, "PB Size", QString::number(e.mpdu.pb_size));
        // Beacon Load(载荷区,字段树由解析器生成)
        if (e.beacon.present) {
            auto* load = tree_add_item(bcn, "Beacon Load", "");
            MsduRawMap rm;
            rm.base = 16;
            rm.pb_num = 1;
            rm.pb_size = 0;
            rm.fch_size = 16;
            rm.header_len = 1;
            rm.body = 0;
            tree_render_msdu(load, e.beacon.tree, rm, e.raw_bytes.mid(16));
        }
        break;
    }
    case GW_2022_FrameType::SOF: {
        auto* sof = tree_add_item(root, "SOF", "");
        tree_add_bit_field(sof, "Source TEI", QString::number(e.mpdu.src_tei), 4, 0, 12);
        tree_add_bit_field(sof, "Destination TEI", QString::number(e.mpdu.dst_tei), 5, 4, 12);
        {
            QString link_desc;
            if (e.mpdu.link_id <= 3)
                link_desc = trl::L("报文优先级(越小越低)");
            else if (e.mpdu.link_id <= 254)
                link_desc = trl::L("业务分类LID");
            else
                link_desc = trl::L("无效值");
            tree_add_bit_field(sof, "Link ID",
                QStringLiteral("0x%1 - %2").arg(e.mpdu.link_id, 2, 16, QChar('0')).arg(link_desc),
                7, 0, 8);
        }
        tree_add_bit_field(sof, "Frame Length", QStringLiteral("%1 (x10us)").arg(e.mpdu.frame_len), 8, 0, 12);
        tree_add_bit_field(sof, "PB Num", QString::number(e.mpdu.pb_num), 9, 4, 4);
        tree_add_bit_field(sof, "Symbol Num", QString::number(e.mpdu.symbol_num), 10, 0, 9);
        tree_add_bit_field(sof, "Broadcast", e.mpdu.bc_flag ? "Yes" : "No", 11, 1, 1);
        tree_add_bit_field(sof, "ReSend", e.mpdu.re_send_flag ? "Yes" : "No", 11, 2, 1);
        tree_add_bit_field(sof, "Encrypted", e.mpdu.encryp_flag ? "Yes" : "No", 11, 3, 1);
        tree_add_bit_field(sof, "TMI", QString::number(e.mpdu.tmi), 11, 4, 4);
        tree_add_bit_field(sof, "TMI_EXT", QString::number(e.mpdu.tmi_ext), 12, 0, 4);
        tree_add_item(sof, "PB Size", QString::number(e.mpdu.pb_size));

        // 逐 PB 块渲染:国网 PB 头 1B + 数据 pb_size-4 B + CRC24 3B
        {
            const int header_len = 1;
            const int body_len   = e.mpdu.pb_size - 4;
            const int pb_num     = (e.mpdu.pb_num > 0) ? e.mpdu.pb_num : 1;
            const quint8* d = reinterpret_cast<const quint8*>(e.raw_bytes.constData());
            const int raw_sz = e.raw_bytes.size();
            for (int bi = 0; bi < pb_num; ++bi) {
                const int blk_off = 16 + bi * e.mpdu.pb_size;
                const bool multi = pb_num > 1;
                // PB Header(1B:END/START/seq)
                quint8 h = (bi < e.mpdu.pb_heads.size())
                               ? e.mpdu.pb_heads[bi]
                               : (blk_off < raw_sz ? d[blk_off] : 0);
                QStringList flags;
                if (h & 0x80) flags << QStringLiteral("END");
                if (h & 0x40) flags << QStringLiteral("START");
                QString desc = flags.isEmpty()
                                   ? QStringLiteral("(continuation)")
                                   : flags.join(QLatin1Char('|'));
                tree_add_bit_field(sof,
                    multi ? QStringLiteral("PB Header (%1/%2)").arg(bi).arg(pb_num)
                          : QStringLiteral("PB Header"),
                    QStringLiteral("0x%1 (%2, seq=%3)")
                        .arg(h, 2, 16, QChar('0')).arg(desc).arg(h & 0x3F),
                    blk_off, 0, 8);
                // PB Body
                if (body_len > 0 && blk_off + header_len < raw_sz) {
                    auto* pb = tree_add_item(sof,
                        multi ? QStringLiteral("PB Body (%1/%2)").arg(bi).arg(pb_num)
                              : QStringLiteral("PB Body"),
                        QStringLiteral("%1 B").arg(body_len),
                        blk_off + header_len, body_len);
                    if (e.msdu.present && e.msdu.total_len > 0) {
                        int buf_off = bi * body_len;
                        int msdu_end_in_blk = e.msdu.total_len - buf_off;
                        if (msdu_end_in_blk > 0 && msdu_end_in_blk < body_len) {
                            tree_add_item(pb, "PB Padding",
                                QStringLiteral("%1 B (0x00 fill)")
                                    .arg(body_len - msdu_end_in_blk),
                                blk_off + header_len + msdu_end_in_blk,
                                body_len - msdu_end_in_blk);
                        }
                    }
                }
                // PB CRC24(块尾 3B)
                bool have = (blk_off + e.mpdu.pb_size) <= raw_sz;
                quint32 crc = 0;
                if (have) {
                    int o = blk_off + e.mpdu.pb_size - 3;
                    crc = (quint32)d[o] | ((quint32)d[o + 1] << 8) | ((quint32)d[o + 2] << 16);
                }
                bool okb = (bi < e.mpdu.pb_crc_oks.size())
                               ? e.mpdu.pb_crc_oks[bi]
                               : e.mpdu.pb_crc_ok;
                tree_add_item(sof,
                    multi ? QStringLiteral("PB CRC24 (block %1/%2)").arg(bi).arg(pb_num)
                          : QStringLiteral("PB CRC24"),
                    have ? QStringLiteral("0x%1 %2").arg(crc, 6, 16, QChar('0')).arg(okb ? "OK" : "FAIL")
                         : (okb ? QStringLiteral("OK") : QStringLiteral("FAIL")),
                    blk_off + e.mpdu.pb_size - 3, 3);
            }
        }

        // MSDU 字段树挂载
        if (e.msdu.present) {
            auto* msdu = tree_add_item(root, "MSDU (Reassembled)",
                QStringLiteral("%1 B  %2").arg(e.msdu_body.size()).arg(e.msdu.summary));
            MsduRawMap rm;
            rm.base = e.msdu_raw_base;
            rm.pb_num = (e.msdu_raw_base >= 0) ? 1 : (e.mpdu.pb_num > 0 ? e.mpdu.pb_num : 1);
            rm.pb_size = e.mpdu.pb_size;
            rm.fch_size = 16;
            rm.header_len = 1;
            rm.body = e.mpdu.pb_size - 4;
            tree_apply_msdu_range(msdu, rm, 0, e.msdu.total_len, e.msdu_body);
            tree_add_item(msdu, "Length", QString::number(e.msdu_body.size()));
            tree_render_msdu(msdu, e.msdu.tree, rm, e.msdu_body);
        } else if (!e.msdu_body.isEmpty()) {
            auto* msdu = tree_add_item(root, "MSDU (Reassembled)",
                QStringLiteral("%1 B").arg(e.msdu_body.size()));
            tree_add_item(msdu, "Length", QString::number(e.msdu_body.size()));
            tree_add_item(msdu, "Hex", QString(e.msdu_body.toHex(' ')));
        }
        break;
    }
    case GW_2022_FrameType::ACK: {
        auto* ack = tree_add_item(root, "ACK", "");
        switch (static_cast<GW_2022_AckExtType>(e.mpdu.ack_ext_type)) {
        case GW_2022_AckExtType::Normal: {
            const quint8 rxres = e.mpdu.ack_rx_res;
            QString rv;
            if (static_cast<GW_2022_AckRxRes>(rxres) == GW_2022_AckRxRes::RECEIPT_OK)
                rv = QStringLiteral("0 - Receipt OK (all PB CRC passed)");
            else if (static_cast<GW_2022_AckRxRes>(rxres) == GW_2022_AckRxRes::RECEIPT_FAIL)
                rv = QStringLiteral("1 - Receipt FAIL (≥1 PB CRC failed)");
            else
                rv = QStringLiteral("%1 - Reserved").arg(rxres);
            tree_add_bit_field(ack, "RxRes", rv, 4, 0, 4);
            {
                const quint8 st = e.mpdu.ack_rx_status;
                QStringList oks;
                for (int i = 0; i < 4; ++i)
                    if (st & (1u << i)) oks << QStringLiteral("PB%1").arg(i + 1);
                tree_add_bit_field(ack, "RxStatus",
                    QStringLiteral("0x%1 (%2 CRC ok)")
                        .arg(st, 1, 16, QChar('0'))
                        .arg(oks.isEmpty() ? QStringLiteral("no PB") : oks.join(QLatin1String(", "))),
                    4, 4, 4);
            }
            tree_add_bit_field(ack, "Source TEI", QString::number(e.mpdu.src_tei), 5, 0, 12);
            tree_add_bit_field(ack, "Destination TEI", QString::number(e.mpdu.dst_tei), 6, 4, 12);
            tree_add_bit_field(ack, "RxPBNum", QString::number(e.mpdu.ack_rx_pb_num), 8, 0, 3);
            tree_add_bit_field(ack, "RSV0", QString::number(e.mpdu.ack_rsv0), 8, 3, 5);
            tree_add_bit_field(ack, "ChannelQuality", QStringLiteral("%1 dB").arg(e.mpdu.ack_channel_quality), 9, 0, 8);
            tree_add_bit_field(ack, "STALoad", QString::number(e.mpdu.ack_sta_load), 10, 0, 8);
            tree_add_bit_field(ack, "RSV1", QString::number(e.mpdu.ack_rsv1), 11, 0, 8);
            tree_add_bit_field(ack, "ExtFrameType", QString::number(e.mpdu.ack_ext_type), 12, 0, 4);
            break;
        }
        case GW_2022_AckExtType::Search:
            tree_add_bit_field(ack, "DstAddr", QStringLiteral("0x%1")
                .arg(e.mpdu.ack_dst_addr, 12, 16, QChar('0')), 4, 0, 48);
            tree_add_bit_field(ack, "SearchTEI", QString::number(e.mpdu.ack_search_tei), 10, 0, 12);
            tree_add_bit_field(ack, "SearchFreq", QString::number(e.mpdu.ack_search_freq), 11, 4, 4);
            break;
        case GW_2022_AckExtType::Sync:
            tree_add_bit_field(ack, "Timestamp", QStringLiteral("0x%1")
                .arg(e.mpdu.ack_sync_timestamp, 8, 16, QChar('0')), 4, 0, 32);
            tree_add_bit_field(ack, "SyncTEI", QString::number(e.mpdu.ack_sync_tei), 8, 0, 12);
            break;
        case GW_2022_AckExtType::SwitchChannel:
            tree_add_bit_field(ack, "DstAddr", QStringLiteral("0x%1")
                .arg(e.mpdu.ack_dst_addr, 12, 16, QChar('0')), 4, 0, 48);
            tree_add_bit_field(ack, "HRF Channel", QString::number(e.mpdu.ack_rx_pb_num), 10, 0, 8);
            tree_add_bit_field(ack, "HRF Option", QString::number(e.mpdu.ack_sta_load), 11, 0, 8);
            break;
        default:
            tree_add_item(ack, "Reserved ExtFrameType", QString::number(e.mpdu.ack_ext_type));
            break;
        }
        break;
    }
    case GW_2022_FrameType::COORD: {
        auto* coord = tree_add_item(root, "COORD", "");
        tree_add_bit_field(coord, "TimeDuration", QStringLiteral("%1 ms").arg(e.mpdu.coord_duration), 4, 0, 16);
        tree_add_bit_field(coord, "NextTimeSlotShift", QStringLiteral("%1 ms").arg(e.mpdu.coord_shift), 6, 0, 16);
        tree_add_bit_field(coord, "NeighbourNID", QStringLiteral("0x%1")
            .arg(e.mpdu.coord_neighbour_nid, 6, 16, QChar('0')), 8, 0, 24);
        tree_add_bit_field(coord, "NetRfChannel", QString::number(e.mpdu.coord_rf_channel), 11, 0, 8);
        tree_add_bit_field(coord, "RSV0", QString::number(e.mpdu.coord_rsv0), 12, 0, 4);
        break;
    }
    }
}
