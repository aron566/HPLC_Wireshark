/// @file packetlistmodel.cpp
/// @brief PacketListModel 实现(磁盘换页)
#include "packetlistmodel.h"
#include "packetentry_serialize.h"
#include <QColor>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QThreadPool>

namespace {
// 南网 NW_2021 应用层帧类型域(表4)确认/否认帧 + 业务标识(表9)确认/否认取值
constexpr quint8 kAppPacketAckNack = 0x0;   ///< 帧类型域=确认/否认
constexpr quint8 kBidConfirm       = 0x00;  ///< BID=确认(ACK)
constexpr quint8 kBidDeny          = 0x01;  ///< BID=否认(NACK)
constexpr quint16 kMmeTypeNone     = 0xFFFF; ///< 非管理消息的 mme_type 标记

/// @brief 48-bit MAC 帧内原始字节序 → "aa:bb:cc:dd:ee:ff"(与 fieldspec::mac_str 一致)
QString format_mac(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QStringLiteral("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += QLatin1Char(':');
    }
    return s;
}

/// @brief 管理消息 MMe 类型颜色:暖色系色相(20-179,避开 0-19 红色区),
///        与 APP 层冷色系(180-359)视觉区分;红色仅保留给异常报文
QColor mme_color(quint16 mme_type) {
    const int hue = 20 + int((quint64(mme_type) * 37) % 160);
    return QColor::fromHsv(hue, 190, 220);
}

/// @brief 应用层 BID 颜色:冷色系色相(180-339,避开 340-359 粉红区),
///        与管理消息暖色系区分;红色仅保留给异常报文
QColor bid_color(quint8 bid) {
    const int hue = 180 + int((quint64(bid) * 37) % 160);
    return QColor::fromHsv(hue, 190, 220);
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
        // 整行着色优先级:管理消息(MMe 暖色系) > APP 层(BID 冷色系) > 帧类型(DROP灰/ACK校验失败红不变)
        if (e.msdu.present && e.msdu.mme_type != kMmeTypeNone)
            return mme_color(e.msdu.mme_type);
        if (e.msdu.present && e.msdu.business_id != 0xFF) {
            if (e.msdu.app_packet_type == kAppPacketAckNack) {
                if (e.msdu.business_id == kBidConfirm) return QColor(46, 214, 106);   // 确认(ACK)亮绿
                if (e.msdu.business_id == kBidDeny)    return QColor(224, 64, 64);    // 否认(NACK)红
            }
            return bid_color(e.msdu.business_id);
        }
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
        case 2:  // ACK 选择确认帧:校验失败(≥1 PB CRC 未过)红色显著指示
            if (e.mpdu.ack_rx_res == 1) return QColor(224, 64, 64);
            return QColor(196, 181, 79);
        case 3: return QColor(140, 110, 190);   // COORD 协调帧:紫色(非异常,不用红)
        default: return QColor(170, 170, 170);
    }
}

namespace {
/// @brief 过滤表达式预处理:split/trim/小写/关键词判定只做一次,逐条匹配零分配
/// @details 与旧版逐条 filter_match 语义严格一致:帧型关键词
///          (beacon/sof/ack/coord/search/switch)按帧型字节比对,其余条件词
///          按 search_text 子串比对,空条件词永假。
PacketPreparedFilter prepare_filter(const QString& filter) {
    PacketPreparedFilter pf;
    pf.empty = filter.isEmpty();
    if (pf.empty) return pf;
    auto type_byte_of = [](const QString& t) -> int {
        if (t == QLatin1String("beacon")) return 0;
        if (t == QLatin1String("sof"))    return 1;
        if (t == QLatin1String("ack"))    return 2;
        if (t == QLatin1String("coord"))  return 3;
        if (t == QLatin1String("search")) return 5;
        if (t == QLatin1String("switch")) return 6;
        return -1;
    };
    const QStringList or_groups = filter.split('|', Qt::SkipEmptyParts);
    for (const QString& g : or_groups) {
        QVector<PacketFilterCond> conds;
        for (const QString& c : g.split('&')) {
            const QString t = c.trimmed().toLower();
            PacketFilterCond cond;
            if (t.isEmpty()) {
                cond.type_byte = -2;          // 空条件永假(与旧语义一致)
            } else if (const int tb = type_byte_of(t); tb >= 0) {
                cond.type_byte = tb;
            } else {
                cond.type_byte = -1;
                cond.term = t;
            }
            conds.append(cond);
        }
        pf.groups.append(conds);
    }
    return pf;
}

/// @brief 预处理后的 DNF 匹配:frame_type 字节 + search_text 即可判定
bool prepared_match(const PacketPreparedFilter& pf,
                    quint8 frame_type, const QString& haystack) {
    if (pf.empty) return true;
    for (const auto& conds : pf.groups) {
        bool all = true;
        for (const PacketFilterCond& c : conds) {
            const bool hit = (c.type_byte >= 0) ? (frame_type == c.type_byte)
                           : (c.type_byte == -1) ? haystack.contains(c.term)
                                                 : false;
            if (!hit) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}

/// @brief 工作线程:遍历快照盘块(只读 v3 索引,不解码条目)+ 热区,
///        返回命中过滤器的全局行号
QVector<int> run_filter(const PacketListModel::ExportSnapshot& snap,
                        const QString& filter) {
    const PacketPreparedFilter pf = prepare_filter(filter);
    QVector<int> result;
    int g = 0;
    for (const QString& bp : snap.block_paths) {
        pser::BlockReader reader;
        if (!reader.open(bp)) {
            g += PacketListModel::kBlockSize;
            continue;
        }
        const int n = reader.count();
        for (int i = 0; i < n; ++i) {
            if (prepared_match(pf, reader.frame_type(i),
                                reader.search_text(i)))
                result.append(g + i);
        }
        g += n;
    }
    for (const PacketEntry& e : snap.hot) {
        if (prepared_match(pf, e.mpdu.frame_type, e.search_text))
            result.append(g);
        ++g;
    }
    return result;
}
}  // namespace

bool PacketListModel::passes_filter(const PacketEntry& e) const {
    return prepared_match(m_prepared, e.mpdu.frame_type, e.search_text);
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
    pser::BlockReader reader;
    // 打开只解析头/池/索引,条目按需解码;条数必须 == kBlockSize,否则视为
    // 截断/损坏,拒收(成功落盘的块必为整块;残缺块不进缓存,避免越界)
    if (!reader.open(block_path(idx)) || reader.count() != kBlockSize)
        return false;
    // LRU 淘汰(先插入再淘汰,保证新块存活)
    m_block_cache.insert(idx, std::move(reader));
    touch_lru(idx);
    while (m_lru.size() > kMaxCacheBlocks) {
        int old = m_lru.takeFirst();
        if (old != idx) m_block_cache.remove(old);
    }
    return true;
}

const PacketEntry& PacketListModel::locate(int g) const {
    // 兜底空条目:任何越界/缺块都不返回野引用(只显示空白行,不崩溃)
    static const PacketEntry kNullEntry;
    if (g < 0) return kNullEntry;
    const int hot_start = m_block_count * kBlockSize;
    if (g >= hot_start) {
        const int hi = g - hot_start;
        if (hi < m_hot.size())
            return m_hot[hi];
        return kNullEntry;
    }
    const int bi = g / kBlockSize;
    const int off = g % kBlockSize;
    if (!load_block(bi)) return kNullEntry;
    auto it = m_block_cache.constFind(bi);
    if (it == m_block_cache.constEnd() || off < 0 || off >= it->count())
        return kNullEntry;
    bool ok = false;
    const PacketEntry& e = it->entry_ref(off, ok);   // 只解码这一条
    return ok ? e : kNullEntry;
}

void PacketListModel::flush_hot_block() {
    if (m_hot.size() < kBlockSize) return;
    const int idx = m_block_count;
    // move 前 kBlockSize 条到待落盘块,热区立即腾出(UI 线程只做 move,不阻塞)
    QVector<PacketEntry> blk;
    blk.reserve(kBlockSize);
    for (int i = 0; i < kBlockSize; ++i)
        blk.append(std::move(m_hot[i]));
    m_hot.remove(0, kBlockSize);
    m_block_count++;   // 块号立即可用;文件在后台落盘(完成前 load_block 返回空块)
    // 后台线程:序列化 + qCompress + 写盘。
    // 安全:只捕获 path(值) + blk(move),不捕获 this —— 模型析构/clear 后,
    // 后台只碰局部资源;临时目录被删则 open 失败安全返回,不悬空不崩溃。
    const QString path = block_path(idx);
    QThreadPool::globalInstance()->start([path, blk = std::move(blk)]() {
        const QByteArray data = pser::encode_block(blk);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) return;
        const bool ok = (f.write(data) == data.size());
        f.close();
        // 只有完整落盘才保留;残缺文件删除,load_block 读到空块返回 false(不崩溃)
        if (!ok || f.error() != QFile::NoError)
            f.remove();
    });
}

void PacketListModel::append_packets(QVector<PacketEntry> entries) {
    if (entries.isEmpty()) return;
    if (m_filtering) {                        // 过滤中:暂存,过滤完成后再补 append
        m_deferred += entries;
        return;
    }
    QVector<int> new_visible;
    new_visible.reserve(entries.size());
    const int first_new = m_visible.size();
    for (PacketEntry& e : entries) {
        if (e.search_text.isEmpty())          // 兜底:非 make_entry 路径(测试等)补生成
            e.search_text = make_search_text(e);
        const int g = int(m_total);
        ++m_total;
        m_hot.append(std::move(e));           // move 进热缓存,避免深拷贝
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
    // 删除旧盘块文件:异步写盘的后台任务可能还在写,删掉后其 open 失败即安全;
    // 已被后台打开的文件(Windows 文件锁)删不掉,写盘完成后残留,由下次 clear 清理。
    const QDir dir(m_paging_dir.path());
    for (const QString& f : dir.entryList(QStringList() << QStringLiteral("blk_*.bin"),
                                          QDir::Files))
        QFile::remove(dir.filePath(f));
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

bool PacketListModel::entry_at(int visible_row, PacketEntry& out) const {
    if (visible_row < 0 || visible_row >= m_visible.size()) return false;
    out = locate(m_visible[visible_row]);   // 深拷贝,盘块被换出后仍安全
    return true;
}

void PacketListModel::set_display_filter(const QString& expr) {
    if (m_filter == expr) return;
    m_filter = expr;
    m_prepared = prepare_filter(expr);       // 预处理与表达式同步重建
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
            append_packets(std::move(def));
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
                append_packets(std::move(def));
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
        if (!load_block(b)) continue;
        auto it = m_block_cache.constFind(b);
        if (it == m_block_cache.constEnd()) continue;
        // 顺序访问:BlockReader 内分块只解压一次,逐条解码后回调(拷贝出参,
        // 避免回调期间缓存淘汰导致引用悬空)
        for (int j = 0; j < it->count(); ++j) {
            PacketEntry e;
            if (it->entry_at(j, e)) fn(e);
        }
    }
    for (const PacketEntry& e : m_hot) fn(e);
}

PacketListModel::ExportSnapshot PacketListModel::make_export_snapshot() const {
    ExportSnapshot s;
    s.hot = m_hot;   // 深拷贝(≤ kBlockSize 条)
    s.block_paths.reserve(m_block_count);
    for (int i = 0; i < m_block_count; ++i)
        s.block_paths << block_path(i);
    s.tei_mac = m_tei_mac;   // 深拷贝 TEI→MAC 映射表(CSV 导出显示 MAC 用)
    return s;
}
