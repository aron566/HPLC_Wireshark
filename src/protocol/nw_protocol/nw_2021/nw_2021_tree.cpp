/// @file nw_2021_tree.cpp
/// @brief 南网 NW_2021 协议字段树构建器实现
#include "nw_2021_tree.h"

void NW_2021_TreeBuilder::build(QTreeWidgetItem* root, const PacketEntry& e) {
    // ── MPDU Base(16B 帧控制,表10) ──
    auto* mpdu_base = tree_add_item(root, "MPDU Base", "", 0, 16);
    tree_add_bit_field(mpdu_base, "Frame Type",
        QStringLiteral("%1 (%2)").arg(e.mpdu.frame_type_name()).arg(e.mpdu.frame_type),
        0, 0, 3);
    {
        // 接入指示(表12):0=保留 1=MPDU 在宽带载波通信接入网络中传输
        QString conind = (e.mpdu.net_type == 1) ? QStringLiteral("1 (HPLC Network)")
                       : QStringLiteral("0 (Reserved)");
        tree_add_bit_field(mpdu_base, "ConInd", conind, 0, 3, 1);
    }
    tree_add_bit_field(mpdu_base, "SNID", QStringLiteral("0x%1")
        .arg(e.mpdu.net_id, 1, 16, QChar('0')), 0, 4, 4);
    {
        // 标准版本号(表13):0=保留 1=本标准版本号
        QString ver = (e.mpdu.version == 1) ? QStringLiteral("1 (Standard)")
                     : (e.mpdu.version == 0) ? QStringLiteral("0 (Reserved)")
                     : QStringLiteral("%1 (Reserved)").arg(e.mpdu.version);
        tree_add_bit_field(mpdu_base, "Version", ver, 12, 4, 4);
    }
    tree_add_bit_field(mpdu_base, "FCH CRC24", e.mpdu.fch_crc_ok ? "OK" : "FAIL", 13, 0, 24);

    switch (static_cast<NW_2021_FrameType>(e.mpdu.frame_type)) {
    case NW_2021_FrameType::BEACON: {
        auto* bcn = tree_add_item(root, "BEACON", "");
        tree_add_bit_field(bcn, "TimeStamp", QStringLiteral("0x%1")
            .arg(e.mpdu.beacon_timestamp, 8, 16, QChar('0')), 1, 0, 32);
        tree_add_bit_field(bcn, "BeaconPeriodCount", QString::number(e.mpdu.beacon_period_cnt), 5, 0, 32);
        tree_add_bit_field(bcn, "Source TEI", QString::number(e.mpdu.src_tei), 9, 0, 12);
        if (e.meta.is_rf) {
            // 无线:PBLen(11,0,4)
            tree_add_bit_field(bcn, "PBLen", QString::number(e.mpdu.tmi), 11, 0, 4);
        } else {
            // 载波:TMI(10,4,4) SymbolNum(11,0,9) 保留(12,1,1) 相线(12,2,2)
            tree_add_bit_field(bcn, "TMI", QString::number(e.mpdu.tmi), 10, 4, 4);
            tree_add_bit_field(bcn, "Symbol Num", QString::number(e.mpdu.symbol_num), 11, 0, 9);
            {
                const quint8 b12 = (e.raw_bytes.size() > 12) ? (quint8)e.raw_bytes[12] : 0;
                const quint8 rsv = (b12 >> 1) & 0x01;
                tree_add_bit_field(bcn, "RSV", QString::number(rsv), 12, 1, 1);
            }
            // 相线(表15):0=未知 1=A 2=B 3=C
            QString line;
            switch (e.mpdu.beacon_line) {
                case 0: line = QStringLiteral("0 (Unknown)"); break;
                case 1: line = QStringLiteral("1 (Phase A)"); break;
                case 2: line = QStringLiteral("2 (Phase B)"); break;
                case 3: line = QStringLiteral("3 (Phase C)"); break;
                default: line = QString::number(e.mpdu.beacon_line); break;
            }
            tree_add_bit_field(bcn, "Line", line, 12, 2, 2);
        }
        if (e.mpdu.pb_size > 0)
            tree_add_item(bcn, "PB Size", QString::number(e.mpdu.pb_size));
        if (e.beacon.present) {
            auto* load = tree_add_item(bcn, "Beacon Load", "");
            MsduRawMap rm;
            rm.base = 16;
            rm.pb_num = 1;
            rm.pb_size = 0;
            rm.fch_size = 16;
            rm.header_len = 4;
            rm.body = 0;
            tree_render_msdu(load, e.beacon.tree, rm);
        }
        break;
    }
    case NW_2021_FrameType::SOF: {
        auto* sof = tree_add_item(root, "SOF", "");
        tree_add_bit_field(sof, "Source TEI", QString::number(e.mpdu.src_tei), 1, 0, 12);
        tree_add_bit_field(sof, "Destination TEI", QString::number(e.mpdu.dst_tei), 2, 4, 12);
        tree_add_bit_field(sof, "Link ID", QString::number(e.mpdu.link_id), 4, 0, 8);
        if (e.meta.is_rf) {
            // 无线:FrameLen(5,0,12) PBLen(6,4,4)
            tree_add_bit_field(sof, "Frame Length", QString::number(e.mpdu.frame_len), 5, 0, 12);
            tree_add_bit_field(sof, "PBLen", QString::number(e.mpdu.tmi), 6, 4, 4);
        } else {
            // 载波:PBNum(7,0,4) TMI(7,4,4) FrameLen(8,0,12) TMI_EXT(12,0,4)
            tree_add_bit_field(sof, "PB Num", QString::number(e.mpdu.pb_num), 7, 0, 4);
            tree_add_bit_field(sof, "TMI", QString::number(e.mpdu.tmi), 7, 4, 4);
            tree_add_bit_field(sof, "Frame Length", QString::number(e.mpdu.frame_len), 8, 0, 12);
            tree_add_bit_field(sof, "TMI_EXT", QString::number(e.mpdu.tmi_ext), 12, 0, 4);
        }
        tree_add_item(sof, "PB Size", QString::number(e.mpdu.pb_size));

        // 逐 PB 块渲染:南网 PB 头 4B + 数据 pb_size-8 B + 保留 1B + CRC24 3B
        {
            const int header_len = 4;
            const int body_len   = e.mpdu.pb_size - 8;
            const int pb_num     = (e.mpdu.pb_num > 0) ? e.mpdu.pb_num : 1;
            const quint8* d = reinterpret_cast<const quint8*>(e.raw_bytes.constData());
            const int raw_sz = e.raw_bytes.size();
            for (int bi = 0; bi < pb_num; ++bi) {
                const int blk_off = 16 + bi * e.mpdu.pb_size;
                const bool multi = pb_num > 1;
                // PB Header(4B:SeqNum 16b + AggregateFlag 1b + RSV 15b)
                quint16 seq = 0;
                quint8  aggr = 0;
                if (blk_off + 2 < raw_sz) {
                    seq  = (quint16)(d[blk_off] | (d[blk_off + 1] << 8));
                    aggr = (quint8)(d[blk_off + 2] & 0x01);
                }
                tree_add_item(sof,
                    multi ? QStringLiteral("PB Header (%1/%2)").arg(bi).arg(pb_num)
                          : QStringLiteral("PB Header"),
                    QStringLiteral("seq=%1 agg=%2").arg(seq).arg(aggr),
                    blk_off, header_len);
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
                // PB CRC24(块尾 3B,前 1B 是保留)
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
            rm.header_len = 4;
            rm.body = e.mpdu.pb_size - 8;
            int msdu_start = tree_msdu_raw_of(rm, 0, e.msdu.total_len);
            if (msdu_start >= 0 && e.msdu.total_len > 0) {
                msdu->setData(0, Qt::UserRole, msdu_start);
                msdu->setData(1, Qt::UserRole, e.msdu.total_len);
            }
            tree_add_item(msdu, "Length", QString::number(e.msdu_body.size()));
            tree_render_msdu(msdu, e.msdu.tree, rm);
        } else if (!e.msdu_body.isEmpty()) {
            auto* msdu = tree_add_item(root, "MSDU (Reassembled)",
                QStringLiteral("%1 B").arg(e.msdu_body.size()));
            tree_add_item(msdu, "Length", QString::number(e.msdu_body.size()));
            tree_add_item(msdu, "Hex", QString(e.msdu_body.toHex(' ')));
        }
        break;
    }
    case NW_2021_FrameType::ACK: {
        auto* ack = tree_add_item(root, "ACK", "");
        tree_add_bit_field(ack, "ExtType", QString::number(e.mpdu.ack_ext_type), 12, 0, 4);
        switch (static_cast<NW_2021_AckExtType>(e.mpdu.ack_ext_type)) {
        case NW_2021_AckExtType::Normal:
            tree_add_bit_field(ack, "RxRes", QString::number(e.mpdu.ack_rx_res), 1, 0, 4);
            tree_add_bit_field(ack, "RxStatus", QString::number(e.mpdu.ack_rx_status), 1, 4, 4);
            tree_add_bit_field(ack, "Destination TEI", QString::number(e.mpdu.dst_tei), 2, 0, 12);
            tree_add_bit_field(ack, "RxPBNum", QString::number(e.mpdu.ack_rx_pb_num), 3, 4, 4);
            break;
        case NW_2021_AckExtType::Search:
            tree_add_bit_field(ack, "DstAddr", QStringLiteral("0x%1")
                .arg(e.mpdu.ack_dst_addr, 12, 16, QChar('0')), 1, 0, 48);
            tree_add_bit_field(ack, "SearchTEI", QString::number(e.mpdu.ack_search_tei), 7, 0, 12);
            break;
        case NW_2021_AckExtType::Sync:
            tree_add_bit_field(ack, "Timestamp", QStringLiteral("0x%1")
                .arg(e.mpdu.ack_sync_timestamp, 8, 16, QChar('0')), 1, 0, 32);
            tree_add_bit_field(ack, "SyncTEI", QString::number(e.mpdu.ack_sync_tei), 5, 0, 12);
            break;
        case NW_2021_AckExtType::SwitchChannel:
            tree_add_bit_field(ack, "DstAddr", QStringLiteral("0x%1")
                .arg(e.mpdu.ack_dst_addr, 12, 16, QChar('0')), 1, 0, 48);
            tree_add_bit_field(ack, "Channel", QString::number(e.mpdu.ack_channel_quality), 7, 0, 8);
            tree_add_bit_field(ack, "Option", QString::number(e.mpdu.ack_sta_load), 8, 0, 8);
            break;
        default:
            tree_add_item(ack, "Reserved ExtType", QString::number(e.mpdu.ack_ext_type));
            break;
        }
        break;
    }
    case NW_2021_FrameType::COORD: {
        auto* coord = tree_add_item(root, "COORD", "");
        tree_add_bit_field(coord, "NeighbourNIDBitMap", QStringLiteral("0x%1")
            .arg(e.mpdu.coord_neighbour_nid, 4, 16, QChar('0')), 1, 0, 16);
        tree_add_bit_field(coord, "Channel", QString::number(e.mpdu.coord_rf_channel), 3, 0, 8);
        tree_add_bit_field(coord, "TimeDuration", QStringLiteral("%1 (x40ms)").arg(e.mpdu.coord_duration), 5, 2, 14);
        tree_add_bit_field(coord, "BandSlotEndFlag", QString::number(e.mpdu.coord_band_end_flag), 7, 1, 1);
        tree_add_bit_field(coord, "Option", QString::number(e.mpdu.coord_option), 7, 2, 2);
        tree_add_bit_field(coord, "BandSlotEndOffset", QStringLiteral("%1 (x4ms)").arg(e.mpdu.coord_band_end_offset), 8, 0, 16);
        tree_add_bit_field(coord, "BandSlotStartOffset", QStringLiteral("%1 (x4ms)").arg(e.mpdu.coord_band_start_offset), 10, 0, 16);
        break;
    }
    }
}
