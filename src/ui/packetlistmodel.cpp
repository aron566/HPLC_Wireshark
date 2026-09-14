/// @file packetlistmodel.cpp
/// @brief PacketListModel 实现(磁盘换页)
#include "packetlistmodel.h"
#include "packetentry_serialize.h"
#include <QColor>
#include <QDataStream>
#include <QFile>
#include <QtConcurrent>
#include <QFutureWatcher>

namespace {
/// @brief 48-bit MAC 帧内原始字节序 → "aa:bb:cc:dd:ee:ff"(与 fieldspec::mac_str 一致)
QString format_mac(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QStringLiteral("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += QLatin1Char(':');
    }
    return s;
}
}  // namespace

PacketListModel::PacketListModel(QObject* parent)
    : QAbstractTableModel(parent), m_block_count(0), m_total(0) {}

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
    const PacketEntry& e = locate(m_visible[idx.row()]);

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case COL_INDEX:  return e.index;
            case COL_TIME: {
                if (e.meta.frame_time.isValid())
                    return e.meta.frame_time.toString("HH:mm:ss.zzz");
                return QStringLiteral("%1 s").arg(e.epoch_ms / 1000.0, 0, 'f', 6);
            }
            case COL_DELTA:
                return QStringLiteral("%1 s").arg(e.delta_us / 1e6, 0, 'f', 6);
            case COL_ORIG_SRC: {
                if (e.accepted && e.msdu.present && !e.msdu.simple_head
                    && e.msdu.msdu_src_tei > 0) {
                    const int tei = e.msdu.msdu_src_tei;
                    QString s = (tei == 1) ? QStringLiteral("CCO")
                                           : QStringLiteral("STA-%1").arg(tei);
                    // MACAddrFlag=1 → 扩展源 MAC;否则查映射表
                    quint64 mac = e.msdu.msdu_src_mac;
                    if (!mac) mac = lookup_mac(e.mpdu.net_id, quint16(tei));
                    if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
                    return s;
                }
                return QString();
            }
            case COL_ORIG_DST: {
                if (e.accepted && e.msdu.present && !e.msdu.simple_head
                    && e.msdu.msdu_dst_tei > 0) {
                    const int tei = e.msdu.msdu_dst_tei;
                    QString s = (tei == 0xFFF) ? QStringLiteral("BCAST")
                               : (tei == 1) ? QStringLiteral("CCO")
                               : QStringLiteral("STA-%1").arg(tei);
                    if (tei == 0xFFF) {
                        // 广播:MACAddrFlag=1 显示帧字段里的 MAC(不一定是 ff:ff:ff:ff:ff:ff);
                        // MACAddrFlag=0 不显示(不查映射表)
                        if (e.msdu.msdu_dst_mac)
                            s += QStringLiteral(" [%1]").arg(format_mac(e.msdu.msdu_dst_mac));
                    } else {
                        // 非广播:MACAddrFlag=1 → 扩展目的 MAC;否则查映射表
                        quint64 mac = e.msdu.msdu_dst_mac;
                        if (!mac) mac = lookup_mac(e.mpdu.net_id, quint16(tei));
                        if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
                    }
                    return s;
                }
                return QString();
            }
            case COL_DIR: {
                if (!e.accepted || !e.msdu.present || e.msdu.simple_head)
                    return QStringLiteral("*");
                // 中继判定:当前发送者(MPDU src_tei)≠原始发起者(MSDU src_tei) → 中继转发
                const bool relay = (e.mpdu.src_tei != 0)
                                && (e.msdu.msdu_src_tei > 0)
                                && (quint16(e.msdu.msdu_src_tei) != e.mpdu.src_tei);
                QString dir;
                if (e.msdu.msdu_dst_tei == 0xFFF) {
                    dir = (e.msdu.msdu_send_type == 1 || e.msdu.msdu_send_type == 3)
                            ? QStringLiteral("\u2192") : QStringLiteral("*");
                } else if (e.msdu.msdu_dst_tei == 1) {
                    dir = QStringLiteral("\u2191");
                } else if (e.msdu.msdu_src_tei == 1) {
                    dir = QStringLiteral("\u2193");
                } else {
                    dir = QStringLiteral("*");
                }
                if (relay && dir != QStringLiteral("*"))
                    dir += QStringLiteral("R");
                return dir;
            }
            case COL_SOURCE: {
                if (!e.accepted) return QStringLiteral("DROP");
                const quint16 tei = e.mpdu.src_tei;
                // COORD 帧:CCO 发出(src_tei 未解析),用 NID 查 CCO MAC
                if (e.mpdu.frame_type == 3 && tei == 0) {
                    QString s = QStringLiteral("CCO");
                    const quint64 mac = lookup_mac(e.mpdu.net_id, 1);
                    if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
                    return s;
                }
                if (tei != 0) {
                    QString s = (tei == 0x0001) ? QStringLiteral("CCO")
                                                : QStringLiteral("STA-%1").arg(tei);
                    const quint64 mac = lookup_mac(e.mpdu.net_id, tei);
                    if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
                    return s;
                }
                // 源 TEI 未知(0,如关联请求),用报文体 STAMACAddr 显示 STA-X [MAC]
                if (e.msdu.sta_mac)
                    return QStringLiteral("STA-X [%1]").arg(format_mac(e.msdu.sta_mac));
                return e.meta.is_rf ? QStringLiteral("RF") : QStringLiteral("PLC");
            }
            case COL_DEST: {
                if (!e.accepted) return e.reason;
                if (e.mpdu.dst_tei == 0xFFF) return QStringLiteral("BROADCAST");
                const quint16 tei = e.mpdu.dst_tei;
                if (tei != 0) {
                    QString s = (tei == 0x0001) ? QStringLiteral("CCO")
                                                : QStringLiteral("STA-%1").arg(tei);
                    const quint64 mac = lookup_mac(e.mpdu.net_id, tei);
                    if (mac) s += QStringLiteral(" [%1]").arg(format_mac(mac));
                    return s;
                }
                return QStringLiteral("*");
            }
            case COL_PROTOCOL: {
                if (!e.accepted) return QStringLiteral("ERR");
                return e.meta.is_rf ? QStringLiteral("RF") : QStringLiteral("HPLC");
            }
            case COL_FRAME_TYPE: {
                if (!e.accepted) return QStringLiteral("-");
                return e.mpdu.frame_type_name();
            }
            case COL_MSDU_TYPE: {
                if (e.accepted && e.msdu.present) return e.msdu.summary;
                return QString();
            }
            case COL_MSDU_SEQ: {
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
/// @brief 过滤器 DNF 匹配(不依赖成员 m_filter,供工作线程复用)
bool filter_match(const QString& filter, const PacketEntry& e) {
    if (filter.isEmpty()) return true;
    const QString& haystack = e.search_text;   // 预计算缓存
    const QString ft = e.mpdu.frame_type_name().toLower();
    auto cond_hit = [&](const QString& raw) -> bool {
        const QString t = raw.trimmed().toLower();
        if (t.isEmpty()) return false;
        if (t == QLatin1String("beacon") || t == QLatin1String("sof")
            || t == QLatin1String("ack")   || t == QLatin1String("coord")
            || t == QLatin1String("search")|| t == QLatin1String("switch")) {
            return ft == t;
        }
        return haystack.contains(t);
    };
    const QStringList or_groups = filter.split('|', Qt::SkipEmptyParts);
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

/// @brief 工作线程:遍历快照盘块(独立读盘)+ 热区,返回命中过滤器的全局行号
QVector<int> run_filter(const PacketListModel::ExportSnapshot& snap,
                        const QString& filter) {
    QVector<int> result;
    int g = 0;
    for (const QString& bp : snap.block_paths) {
        QFile f(bp);
        if (!f.open(QIODevice::ReadOnly)) { g += PacketListModel::kBlockSize; continue; }
        QDataStream s(&f);
        while (!f.atEnd()) {
            quint32 len = 0;
            s >> len;
            if (s.status() != QDataStream::Ok || len == 0) break;
            QByteArray payload(int(len), Qt::Uninitialized);
            if (s.readRawData(payload.data(), int(len)) != int(len)) break;
            PacketEntry e;
            if (pser::deserialize_entry(payload, e) && filter_match(filter, e))
                result.append(g);
            ++g;
        }
        f.close();
    }
    for (const PacketEntry& e : snap.hot) {
        if (filter_match(filter, e)) result.append(g);
        ++g;
    }
    return result;
}
}  // namespace

bool PacketListModel::passes_filter(const PacketEntry& e) const {
    return filter_match(m_filter, e);
}

QString PacketListModel::block_path(int idx) const {
    return m_paging_dir.filePath(QStringLiteral("blk_%1.bin").arg(idx));
}

void PacketListModel::touch_lru(int idx) const {
    m_lru.removeAll(idx);
    m_lru.append(idx);
}

bool PacketListModel::load_block(int idx) const {
    if (m_block_cache.contains(idx)) { touch_lru(idx); return true; }
    QFile f(block_path(idx));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QVector<PacketEntry> block;
    block.reserve(kBlockSize);
    QDataStream s(&f);
    while (!f.atEnd()) {
        quint32 len = 0;
        s >> len;
        if (s.status() != QDataStream::Ok || len == 0) break;
        QByteArray payload(int(len), Qt::Uninitialized);
        if (s.readRawData(payload.data(), int(len)) != int(len)) break;
        PacketEntry e;
        if (pser::deserialize_entry(payload, e)) block.append(std::move(e));
    }
    f.close();
    // LRU 淘汰(先插入再淘汰,保证新块存活)
    m_block_cache.insert(idx, block);
    touch_lru(idx);
    while (m_lru.size() > kMaxCacheBlocks) {
        int old = m_lru.takeFirst();
        if (old != idx) m_block_cache.remove(old);
    }
    return true;
}

const PacketEntry& PacketListModel::locate(int g) const {
    const int hot_start = m_block_count * kBlockSize;
    if (g >= hot_start)
        return m_hot[g - hot_start];
    const int bi = g / kBlockSize;
    const int off = g % kBlockSize;
    load_block(bi);
    return m_block_cache[bi].at(off);
}

void PacketListModel::flush_hot_block() {
    if (m_hot.size() < kBlockSize) return;
    const int idx = m_block_count;
    QFile f(block_path(idx));
    if (!f.open(QIODevice::WriteOnly)) return;   // 写失败:保留在热区,不丢弃数据
    QDataStream s(&f);
    for (int i = 0; i < kBlockSize; ++i) {
        const QByteArray payload = pser::serialize_entry(m_hot[i]);
        s << quint32(payload.size());
        s.writeRawData(payload.constData(), payload.size());
    }
    f.close();
    m_block_count++;
    m_hot.remove(0, kBlockSize);
}

void PacketListModel::append_packets(const QVector<PacketEntry>& entries) {
    if (entries.isEmpty()) return;
    if (m_filtering) {                        // 过滤中:暂存,过滤完成后再补 append
        m_deferred += entries;
        return;
    }
    QVector<int> new_visible;
    new_visible.reserve(entries.size());
    const int first_new = m_visible.size();
    for (const PacketEntry& e : entries) {
        PacketEntry ee = e;                      // 拷贝(与 m_hot.append 合并为一次)
        if (ee.search_text.isEmpty())            // 兜底:非 make_entry 路径(测试等)补生成
            ee.search_text = make_search_text(ee);
        const int g = int(m_total);
        ++m_total;
        m_hot.append(std::move(ee));
        update_tei_mac(m_hot.last());
        if (passes_filter(m_hot.last())) new_visible.append(g);
        if (m_hot.size() >= kBlockSize) flush_hot_block();
    }
    if (new_visible.isEmpty()) return;
    beginInsertRows(QModelIndex(), first_new, first_new + new_visible.size() - 1);
    m_visible += new_visible;
    endInsertRows();
}

void PacketListModel::append_packet(const PacketEntry& entry) {
    append_packets(QVector<PacketEntry>{entry});
}

void PacketListModel::clear_all() {
    ++m_filter_gen;          // 作废在跑的异步过滤(结果作废,不再回填)
    m_filtering = false;
    m_deferred.clear();
    beginResetModel();
    m_hot.clear();
    m_block_cache.clear();
    m_lru.clear();
    m_visible.clear();
    m_block_count = 0;
    m_total = 0;
    m_tei_mac.clear();
    endResetModel();
}

void PacketListModel::update_tei_mac(const PacketEntry& e) {
    if (!e.accepted) return;
    const quint32 nid = e.mpdu.net_id;
    // BEACON 帧:CCO 的 MAC(TEI=1),供 COORD 等 CCO 发出帧查表
    if (e.mpdu.frame_type == 0 && e.mpdu.beacon_cco_mac)
        m_tei_mac[nid][1] = e.mpdu.beacon_cco_mac;
    // 信标管理信息条目(站点能力/精简站点)携带的 TEI→SourceMAC 学习对
    for (const TeiMacPair& p : e.beacon.tei_mac_pairs)
        if (p.tei != 0 && p.mac) m_tei_mac[nid][p.tei] = p.mac;
    // 管理帧(发现列表等)携带的 TEI→MAC 学习对
    for (const TeiMacPair& p : e.msdu.tei_mac_pairs)
        if (p.tei != 0 && p.mac) m_tei_mac[nid][p.tei] = p.mac;
    if (!e.msdu.present || e.msdu.simple_head) return;
    if (e.msdu.msdu_src_tei > 0 && e.msdu.msdu_src_mac)
        m_tei_mac[nid][quint16(e.msdu.msdu_src_tei)] = e.msdu.msdu_src_mac;
    if (e.msdu.msdu_dst_tei > 0 && e.msdu.msdu_dst_mac)
        m_tei_mac[nid][quint16(e.msdu.msdu_dst_tei)] = e.msdu.msdu_dst_mac;
}

quint64 PacketListModel::lookup_mac(quint32 nid, quint16 tei) const {
    auto it = m_tei_mac.constFind(nid);
    if (it == m_tei_mac.constEnd()) return 0;
    auto mit = it->constFind(tei);
    return (mit == it->constEnd()) ? 0 : mit.value();
}

void PacketListModel::activate_row(int visible_row) {
    if (visible_row < 0 || visible_row >= m_visible.size()) return;
    emit packet_activated(locate(m_visible[visible_row]));
}

void PacketListModel::set_display_filter(const QString& expr) {
    if (m_filter == expr) return;
    m_filter = expr;
    const int gen = ++m_filter_gen;          // 换代,丢弃在跑的旧过滤结果
    if (expr.isEmpty()) {                    // 清空过滤:恢复全可见(同步)
        m_filtering = false;                 // 复位过滤中状态(在跑旧过滤结果已由 gen 作废)
        beginResetModel();
        m_visible.clear();
        const qint64 total = m_total;
        m_visible.reserve(int(qMin<qint64>(total, 1000000)));
        for (int g = 0; g < int(total); ++g) m_visible.append(g);
        endResetModel();
        if (!m_deferred.isEmpty()) {         // 过滤期间暂存的 append 补进来(空过滤全可见)
            QVector<PacketEntry> def;
            def.swap(m_deferred);
            append_packets(def);
        }
        return;
    }
    // 异步过滤:快照 + 工作线程遍历,完成后再更新 m_visible(不阻塞 GUI)
    const ExportSnapshot snap = make_export_snapshot();
    m_filtering = true;
    auto* watcher = new QFutureWatcher<QVector<int>>(this);
    connect(watcher, &QFutureWatcher<QVector<int>>::finished, this,
        [this, watcher, gen]() {
            const QVector<int> res = watcher->result();
            watcher->deleteLater();
            if (gen != m_filter_gen) return;  // 过期结果(期间又改了过滤),丢弃
            m_visible = res;
            m_filtering = false;
            if (!m_deferred.isEmpty()) {      // 过滤期间暂存的 append 补进来
                QVector<PacketEntry> def;
                def.swap(m_deferred);
                append_packets(def);
            }
            beginResetModel();
            endResetModel();
        });
    watcher->setFuture(QtConcurrent::run([snap, expr]() {
        return run_filter(snap, expr);
    }));
}

void PacketListModel::ensure_loaded(int visible_row) {
    if (visible_row < 0 || visible_row >= m_visible.size()) return;
    const int g = m_visible[visible_row];
    const int hot_start = m_block_count * kBlockSize;
    if (g >= hot_start) return;   // 热区无需加载
    const int bi = g / kBlockSize;
    load_block(bi);
    if (bi + 1 < m_block_count) load_block(bi + 1);
    if (bi - 1 >= 0)             load_block(bi - 1);
}

void PacketListModel::for_each_entry(const std::function<void(const PacketEntry&)>& fn) {
    for (int b = 0; b < m_block_count; ++b) {
        load_block(b);
        const QVector<PacketEntry>& blk = m_block_cache[b];
        for (const PacketEntry& e : blk) fn(e);
    }
    for (const PacketEntry& e : m_hot) fn(e);
}

PacketListModel::ExportSnapshot PacketListModel::make_export_snapshot() const {
    ExportSnapshot s;
    s.hot = m_hot;   // 深拷贝(≤ kBlockSize 条)
    s.block_paths.reserve(m_block_count);
    for (int i = 0; i < m_block_count; ++i)
        s.block_paths << block_path(i);
    return s;
}
