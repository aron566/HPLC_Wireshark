/// @file packetlistmodel.cpp
/// @brief PacketListModel 实现
#include "packetlistmodel.h"
#include <QColor>

PacketListModel::PacketListModel(QObject* parent) : QAbstractTableModel(parent) {}

int PacketListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_visible.size();
}

int PacketListModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return COL_COUNT;
}

QVariant PacketListModel::headerData(int section, Qt::Orientation orient, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orient == Qt::Horizontal) {
        switch (section) {
            case COL_INDEX:    return QStringLiteral("#");
            case COL_TIME:     return QStringLiteral("Time");
            case COL_DELTA:    return QStringLiteral("Delta");
            case COL_ORIG_SRC: return QStringLiteral("Orig Src");
            case COL_SOURCE:   return QStringLiteral("Source");
            case COL_DEST:     return QStringLiteral("Destination");
            case COL_ORIG_DST: return QStringLiteral("Orig Dst");
            case COL_DIR:      return QStringLiteral("Dir");
            case COL_PROTOCOL: return QStringLiteral("Protocol");
            case COL_FRAME_TYPE: return QStringLiteral("Frame Type");
            case COL_MSDU_TYPE: return QStringLiteral("MSDU Type");
            case COL_MSDU_SEQ: return QStringLiteral("MSDU Seq");
            case COL_LENGTH:   return QStringLiteral("Length");
            case COL_INFO:     return QStringLiteral("Info");
        }
    }
    return section + 1;
}

QVariant PacketListModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_visible.size()) return {};
    const PacketEntry& e = m_all[m_visible[idx.row()]];

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case COL_INDEX:  return e.index;
            case COL_TIME: {
                if (e.meta.frame_time.isValid())
                    return e.meta.frame_time.toString("HH:mm:ss.zzz");
                // 无绝对时钟(如回放)时回退为相对秒数,带单位
                return QStringLiteral("%1 s").arg(e.epoch_ms / 1000.0, 0, 'f', 6);
            }
            case COL_DELTA:
                return QStringLiteral("%1 s").arg(e.delta_us / 1e6, 0, 'f', 6);
            case COL_ORIG_SRC: {
                // 原始发起 TEI(MSDU 头 SourceTEI):仅 SOF 重组完成且长头时
                if (e.accepted && e.msdu.present && !e.msdu.simple_head
                    && e.msdu.msdu_src_tei > 0)
                    return (e.msdu.msdu_src_tei == 1)
                        ? QStringLiteral("CCO")
                        : QStringLiteral("STA-%1").arg(e.msdu.msdu_src_tei);
                return QString();
            }
            case COL_ORIG_DST: {
                if (e.accepted && e.msdu.present && !e.msdu.simple_head
                    && e.msdu.msdu_dst_tei > 0)
                    return (e.msdu.msdu_dst_tei == 0xFFF)
                        ? QStringLiteral("BCAST")
                        : (e.msdu.msdu_dst_tei == 1)
                            ? QStringLiteral("CCO")
                            : QStringLiteral("STA-%1").arg(e.msdu.msdu_dst_tei);
                return QString();
            }
            case COL_DIR: {
                // 方向:↑=上行(原始终点=CCO) ↓=下行(原始发起=CCO)
                // 广播(原始终点=0xFFF):按发送类型标识——本地广播=横线,全网/代理广播=省略号
                if (!e.accepted || !e.msdu.present || e.msdu.simple_head)
                    return QStringLiteral("*");
                if (e.msdu.msdu_dst_tei == 0xFFF) {
                    if (e.msdu.msdu_send_type == 1 || e.msdu.msdu_send_type == 3)
                        return QStringLiteral("\u2192");   // 全网/代理广播:需转发
                    return QStringLiteral("*");   // 本地广播(不转发)及其它
                }
                if (e.msdu.msdu_dst_tei == 1) return QStringLiteral("\u2191");
                if (e.msdu.msdu_src_tei == 1) return QStringLiteral("\u2193");
                return QStringLiteral("*");
            }
            case COL_SOURCE: {
                if (!e.accepted) return QStringLiteral("DROP");
                if (e.mpdu.src_tei == 0x0001) return QStringLiteral("CCO");
                if (e.mpdu.src_tei != 0)      return QStringLiteral("STA-%1").arg(e.mpdu.src_tei);
                return e.meta.is_rf ? QStringLiteral("HRF") : QStringLiteral("PLC");
            }
            case COL_DEST: {
                if (!e.accepted) return e.reason;
                if (e.mpdu.dst_tei == 0xFFFF) return QStringLiteral("BROADCAST");
                if (e.mpdu.dst_tei == 0x0001) return QStringLiteral("CCO");
                if (e.mpdu.dst_tei != 0)      return QStringLiteral("STA-%1").arg(e.mpdu.dst_tei);
                return QStringLiteral("*");
            }
            case COL_PROTOCOL: {
                if (!e.accepted) return QStringLiteral("ERR");
                return e.meta.is_rf ? QStringLiteral("HRF") : QStringLiteral("HPLC");
            }
            case COL_FRAME_TYPE: {
                if (!e.accepted) return QStringLiteral("-");
                return e.mpdu.frame_type_name();
            }
            case COL_MSDU_TYPE: {
                // SOF 帧重组完成时显示 MSDU 概要(MMe 类型 / APP PacketID)
                if (e.accepted && e.msdu.present) return e.msdu.summary;
                return QString();
            }
            case COL_MSDU_SEQ: {
                // SOF 帧重组完成时显示 MSDU 序号
                if (e.accepted && e.msdu.present) return (int)e.msdu.msdu_seq;
                return QString();
            }
            case COL_LENGTH: return e.raw_bytes.size();
            case COL_INFO: {
                if (!e.accepted) return e.reason;
                QString nid = QString::number(e.mpdu.net_id, 16).toUpper().rightJustified(6, QChar('0'));
                if (e.mpdu.frame_type == 0) {
                    return QStringLiteral("NetID=0x%1 ts=%2")
                           .arg(nid).arg(e.meta.timestamp);
                }
                if (e.mpdu.frame_type == 1) {
                    QString msdu = e.msdu_body.isEmpty() ? QString() :
                                   QStringLiteral(" MSDU[%1B]").arg(e.msdu_body.size());
                    return QStringLiteral("NetID=0x%1 src=%2 dst=%3 TMI=%4 PBNum=%5%6")
                           .arg(nid).arg(e.mpdu.src_tei).arg(e.mpdu.dst_tei)
                           .arg(e.mpdu.tmi).arg(e.mpdu.pb_num).arg(msdu);
                }
                return QStringLiteral("NetID=0x%1 src=%2 dst=%3")
                       .arg(nid).arg(e.mpdu.src_tei).arg(e.mpdu.dst_tei);
            }
        }
    } else if (role == Qt::ForegroundRole) {
        return data_color(e);
    } else if (role == Qt::TextAlignmentRole) {
        if (idx.column() == COL_INDEX || idx.column() == COL_LENGTH)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (idx.column() == COL_TIME || idx.column() == COL_DELTA)
            return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return {};
}

QVariant PacketListModel::data_color(const PacketEntry& e) const {
    if (!e.accepted) return QColor(128, 128, 128);
    switch (e.mpdu.frame_type) {
        case 0: return QColor(86, 156, 214);
        case 1: return QColor(106, 153, 78);
        case 2: return QColor(196, 181, 79);
        case 3: return QColor(206, 92,  92);
        default: return QColor(170, 170, 170);
    }
}

namespace {
/// @brief 生成一行的可搜索全文(小写):所有列文本 + NetID hex + MSDU 类型
QString entry_search_text(const PacketEntry& e) {
    QStringList parts;
    parts << QString::number(e.index);
    if (!e.accepted) {
        parts << QStringLiteral("drop") << QStringLiteral("err");
        parts << e.reason.toLower();
        return parts.join(' ');
    }
    parts << (e.meta.is_rf ? QStringLiteral("hrf") : QStringLiteral("hplc"))
          << QStringLiteral("plc");
    parts << e.mpdu.frame_type_name().toLower();
    // 源/目的文本与列表列一致
    parts << (e.mpdu.src_tei == 0x0001 ? QStringLiteral("cco")
            : e.mpdu.src_tei != 0      ? QStringLiteral("sta-%1").arg(e.mpdu.src_tei)
            : e.meta.is_rf ? QStringLiteral("hrf") : QStringLiteral("plc"));
    parts << (e.mpdu.dst_tei == 0xFFFF ? QStringLiteral("broadcast")
            : e.mpdu.dst_tei == 0x0001 ? QStringLiteral("cco")
            : e.mpdu.dst_tei != 0      ? QStringLiteral("sta-%1").arg(e.mpdu.dst_tei)
                                       : QStringLiteral("*"));
    // NetID:支持 "cda1d5" / "0xcda1d5" 两种写法
    QString nid = QString::number(e.mpdu.net_id, 16);
    parts << nid << QStringLiteral("0x%1").arg(nid);
    parts << QString::number(e.mpdu.src_tei) << QString::number(e.mpdu.dst_tei);
    // MSDU 概要(MMeAssocReq / APP EventPacket 等)
    if (e.msdu.present) parts << e.msdu.summary.toLower();
    return parts.join(' ');
}
}  // namespace

bool PacketListModel::passes_filter(const PacketEntry& e) const {
    if (m_filter.isEmpty()) return true;
    // 支持 "&" 与 "|" 组合:
    //   "|" 分隔的组之间 OR(任一命中即通过);
    //   "&" 分隔的组内条件 AND(全部命中才通过)。
    //   例: "beacon & cda1d5 | sof & sta-2" = (beacon 且含 cda1d5) 或 (sof 且含 sta-2)
    const QString haystack = entry_search_text(e);
    const QString ft = e.mpdu.frame_type_name().toLower();
    auto cond_hit = [&](const QString& raw) -> bool {
        const QString t = raw.trimmed().toLower();
        if (t.isEmpty()) return false;
        // 帧类型词精确语义:避免 "ack" 误中 "packetid" 之类的子串
        if (t == QLatin1String("beacon") || t == QLatin1String("sof")
            || t == QLatin1String("ack")   || t == QLatin1String("coord")
            || t == QLatin1String("search")|| t == QLatin1String("switch")) {
            return ft == t;
        }
        return haystack.contains(t);
    };
    const QStringList or_groups = m_filter.split('|', Qt::SkipEmptyParts);
    for (const QString& g : or_groups) {
        const QStringList ands = g.split('&');
        bool all = true;
        for (const QString& c : ands) {
            if (!cond_hit(c)) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}

void PacketListModel::append_packets(const QVector<PacketEntry>& entries) {
    if (entries.isEmpty()) return;
    int first_new_row = m_visible.size();
    QVector<int> new_visible;
    new_visible.reserve(entries.size());
    for (const auto& e : entries) {
        int all_idx = m_all.size();
        m_all.append(e);
        if (passes_filter(e)) new_visible.append(all_idx);
    }
    int last_new_row = first_new_row + new_visible.size() - 1;
    if (new_visible.isEmpty()) return;
    beginInsertRows(QModelIndex(), first_new_row, last_new_row);
    for (int idx : new_visible) m_visible.append(idx);
    endInsertRows();
}

void PacketListModel::append_packet(const PacketEntry& entry) {
    append_packets(QVector<PacketEntry>{entry});
}

void PacketListModel::clear_all() {
    beginResetModel();
    m_all.clear();
    m_visible.clear();
    endResetModel();
}

void PacketListModel::activate_row(int visible_row) {
    if (visible_row < 0 || visible_row >= m_visible.size()) return;
    emit packet_activated(m_all[m_visible[visible_row]]);
}

void PacketListModel::set_display_filter(const QString& expr) {
    if (m_filter == expr) return;
    m_filter = expr;
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (passes_filter(m_all[i])) m_visible.append(i);
    }
    endResetModel();
}
