/// @file column_gen.cpp
/// @brief 参考解剖器列值生成:逐项转录 PacketListModel::data 与
///        MainWindow::csv_row 的列渲染规则(两处口径相同)
#include "column_gen.h"

namespace dissect_gen {
namespace {

/// 48-bit MAC 帧内原始字节序 → "aa:bb:cc:dd:ee:ff"(与宿主 format_mac 一致)
QString format_mac(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QStringLiteral("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += QLatin1Char(':');
    }
    return s;
}

quint64 lookup_mac(const TeiMacMap& map, quint32 nid, quint16 tei) {
    auto it = map.constFind(nid);
    if (it == map.constEnd()) return 0;
    auto mit = it->constFind(tei);
    return (mit == it->constEnd()) ? 0 : mit.value();
}

DissectColumn col(const char* id, const char* title, int width,
                  quint32 alignment = 0x0081) {   // AlignLeft | AlignVCenter
    DissectColumn c;
    c.id = QString::fromLatin1(id);
    c.title = QString::fromLatin1(title);
    c.title_en = QString::fromLatin1(title);   // 内置列表头为英文,两语言同名
    c.width = width;
    c.alignment = alignment;
    return c;
}
constexpr quint32 kAlignCenter = 0x0084;   // AlignHCenter | AlignVCenter
constexpr quint32 kAlignRight  = 0x0082;   // AlignRight | AlignVCenter
}  // namespace

QVector<DissectColumn> reference_columns() {
    return {
        col("orig_src",   "Orig Src",    110),
        col("source",     "Source",      150),
        col("destination", "Destination", 150),
        col("orig_dst",   "Orig Dst",    110),
        col("dir",        "Dir",         50,  kAlignCenter),
        col("protocol",   "Protocol",    80,  kAlignCenter),
        col("frame_type", "Frame Type",  100),
        col("msdu_type",  "MSDU Type",   160),
        col("msdu_seq",   "MSDU Seq",    90,  kAlignRight),
        col("length",     "Length",      70,  kAlignRight),
        col("info",       "Info",        420),
    };
}

void update_tei_mac(TeiMacMap& map, const ParseResult& r) {
    if (!r.accept) return;
    const quint32 nid = r.mpdu.net_id;
    if (r.mpdu.frame_type == 0 && r.mpdu.beacon_cco_mac)
        map[nid][1] = r.mpdu.beacon_cco_mac;
    for (const TeiMacPair& p : r.beacon.tei_mac_pairs)
        if (p.tei != 0 && p.mac) map[nid][p.tei] = p.mac;
    for (const TeiMacPair& p : r.msdu.tei_mac_pairs)
        if (p.tei != 0 && p.mac) map[nid][p.tei] = p.mac;
    if (!r.msdu.present || r.msdu.simple_head) return;
    if (r.msdu.msdu_src_tei > 0 && r.msdu.msdu_src_mac)
        map[nid][quint16(r.msdu.msdu_src_tei)] = r.msdu.msdu_src_mac;
    if (r.msdu.msdu_dst_tei > 0 && r.msdu.msdu_dst_mac)
        map[nid][quint16(r.msdu.msdu_dst_tei)] = r.msdu.msdu_dst_mac;
}

QMap<QString, QString> reference_column_values(const ParseResult& r,
                                               const TeiMacMap& map) {
    QMap<QString, QString> v;
    const MsduInfo& msdu = r.msdu;
    const quint32 nid = r.mpdu.net_id;

    // Orig Src
    {
        QString s;
        if (r.accept && msdu.present && !msdu.simple_head
            && msdu.msdu_src_tei > 0) {
            const int tei = msdu.msdu_src_tei;
            s = (tei == 1) ? QStringLiteral("CCO")
                           : QStringLiteral("STA-%1").arg(tei);
            quint64 mac = msdu.msdu_src_mac;
            if (!mac) mac = lookup_mac(map, nid, quint16(tei));
            if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
        }
        v.insert(QStringLiteral("orig_src"), s);
    }
    // Source
    {
        QString s;
        if (!r.accept) {
            s = QStringLiteral("DROP");
        } else {
            const quint16 tei = r.mpdu.src_tei;
            if (r.mpdu.frame_type == 3 && tei == 0) {
                s = QStringLiteral("CCO");
                const quint64 mac = lookup_mac(map, nid, 1);
                if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
            } else if (tei != 0) {
                s = (tei == 1) ? QStringLiteral("CCO")
                               : QStringLiteral("STA-%1").arg(tei);
                const quint64 mac = lookup_mac(map, nid, tei);
                if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
            } else if (msdu.sta_mac) {
                s = QStringLiteral("STA-X [%1]").arg(format_mac(msdu.sta_mac));
            } else {
                s = r.meta.is_rf ? QStringLiteral("RF") : QStringLiteral("PLC");
            }
        }
        v.insert(QStringLiteral("source"), s);
    }
    // Destination
    {
        QString s;
        if (!r.accept) {
            s = r.reject_reason;
        } else if (r.mpdu.dst_tei == 0xFFF) {
            s = QStringLiteral("BROADCAST");
        } else {
            const quint16 tei = r.mpdu.dst_tei;
            if (tei != 0) {
                s = (tei == 1) ? QStringLiteral("CCO")
                               : QStringLiteral("STA-%1").arg(tei);
                const quint64 mac = lookup_mac(map, nid, tei);
                if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
            } else {
                s = QStringLiteral("*");
            }
        }
        v.insert(QStringLiteral("destination"), s);
    }
    // Orig Dst
    {
        QString s;
        if (r.accept && msdu.present && !msdu.simple_head
            && msdu.msdu_dst_tei > 0) {
            const int tei = msdu.msdu_dst_tei;
            s = (tei == 0xFFF) ? QStringLiteral("BCAST")
              : (tei == 1) ? QStringLiteral("CCO")
                           : QStringLiteral("STA-%1").arg(tei);
            if (tei == 0xFFF) {
                if (msdu.msdu_dst_mac)
                    s += QStringLiteral(" [%1]").arg(format_mac(msdu.msdu_dst_mac));
            } else {
                quint64 mac = msdu.msdu_dst_mac;
                if (!mac) mac = lookup_mac(map, nid, quint16(tei));
                if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
            }
        }
        v.insert(QStringLiteral("orig_dst"), s);
    }
    // Dir
    {
        QString dir;
        if (!r.accept || !msdu.present || msdu.simple_head) {
            dir = QStringLiteral("*");
        } else {
            const bool relay = (r.mpdu.src_tei != 0)
                            && (msdu.msdu_src_tei > 0)
                            && (quint16(msdu.msdu_src_tei) != r.mpdu.src_tei);
            if (msdu.msdu_dst_tei == 0xFFF)
                dir = (msdu.msdu_send_type == 1 || msdu.msdu_send_type == 3)
                          ? QStringLiteral("\u2192") : QStringLiteral("*");
            else if (msdu.msdu_dst_tei == 1)
                dir = QStringLiteral("\u2191");
            else if (msdu.msdu_src_tei == 1)
                dir = QStringLiteral("\u2193");
            else
                dir = QStringLiteral("*");
            if (relay && dir != QStringLiteral("*")) dir += QLatin1Char('R');
        }
        v.insert(QStringLiteral("dir"), dir);
    }
    // Protocol / Frame Type / MSDU Type / MSDU Seq / Length
    v.insert(QStringLiteral("protocol"),
             !r.accept ? QStringLiteral("ERR")
                       : r.meta.is_rf ? QStringLiteral("RF")
                                      : QStringLiteral("HPLC"));
    v.insert(QStringLiteral("frame_type"),
             !r.accept ? QStringLiteral("-") : r.mpdu.frame_type_name());
    v.insert(QStringLiteral("msdu_type"),
             (r.accept && msdu.present) ? msdu.summary : QString());
    v.insert(QStringLiteral("msdu_seq"),
             (r.accept && msdu.present) ? QString::number(msdu.msdu_seq)
                                        : QString());
    v.insert(QStringLiteral("length"),
             QString::number(r.payload_for_log.size()));
    // Info
    {
        QString s;
        if (!r.accept) {
            s = r.reject_reason;
        } else {
            const QString nidstr = QString::number(nid, 16).toUpper()
                                       .rightJustified(6, QChar('0'));
            if (r.mpdu.frame_type == 0) {
                s = QStringLiteral("NetID=0x%1 ts=%2")
                        .arg(nidstr).arg(r.meta.timestamp);
            } else if (r.mpdu.frame_type == 1) {
                s = QStringLiteral("NetID=0x%1 src=%2 dst=%3 TMI=%4 PBNum=%5%6")
                        .arg(nidstr).arg(r.mpdu.src_tei).arg(r.mpdu.dst_tei)
                        .arg(r.mpdu.tmi).arg(r.mpdu.pb_num)
                        .arg(r.msdu_body.isEmpty()
                                 ? QString()
                                 : QStringLiteral(" MSDU[%1B]")
                                       .arg(r.msdu_body.size()));
            } else {
                s = QStringLiteral("NetID=0x%1 src=%2 dst=%3")
                        .arg(nidstr).arg(r.mpdu.src_tei).arg(r.mpdu.dst_tei);
            }
        }
        v.insert(QStringLiteral("info"), s);
    }
    return v;
}

}  // namespace dissect_gen
