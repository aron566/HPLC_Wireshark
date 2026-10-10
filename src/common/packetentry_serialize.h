/// @file packetentry_serialize.h
/// @brief PacketEntry 的磁盘序列化(供换页临时文件存储)
/// @details 手动逐字段序列化,避免 QDateTime / QVector<bool> 的流操作
///          版本/字节序不确定性;序列化文件仅在同一进程会话内往返
///          (换页临时文件,不跨版本/跨平台持久化)。
///          读写字段顺序必须严格一致,改动结构体后同步更新本文件。
#ifndef PACKETENTRY_SERIALIZE_H
#define PACKETENTRY_SERIALIZE_H

#include "bplcframe.h"
#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QVector>

namespace pser {

/// @brief 编码侧块内字符串池:千条目间字段名/值海量重复,存索引代替存字符串
/// @details 块内唯一字符串一般仅数千个;池本身随块一起被 qCompress 压缩。
struct StrPool {
    QHash<QString, quint32> idx;
    QVector<QString>       list;
    quint32 intern(const QString& s) {
        auto it = idx.find(s);
        if (it != idx.end()) return it.value();
        const quint32 id = quint32(list.size());
        idx.insert(s, id);
        list.append(s);
        return id;
    }
};

inline void write_u32(QDataStream& s, quint32 v) { s << v; }
inline void write_u16(QDataStream& s, quint16 v) { s << v; }
inline void write_u8(QDataStream& s, quint8 v)  { s << v; }
inline void write_i32(QDataStream& s, qint32 v) { s << v; }
inline void write_i64(QDataStream& s, qint64 v) { s << v; }
inline void write_bool(QDataStream& s, bool v)   { s << v; }
inline void write_bytes(QDataStream& s, const QByteArray& b) { s << b; }
inline void write_str(QDataStream& s, const QString& v, StrPool& pool) { s << pool.intern(v); }

inline void read_u32(QDataStream& s, quint32& v) { s >> v; }
inline void read_u16(QDataStream& s, quint16& v) { s >> v; }
inline void read_u8(QDataStream& s, quint8& v)   { s >> v; }
inline void read_i32(QDataStream& s, qint32& v)  { s >> v; }
inline void read_i64(QDataStream& s, qint64& v)  { s >> v; }
inline void read_bool(QDataStream& s, bool& v)   { s >> v; }
inline void read_bytes(QDataStream& s, QByteArray& b) { s >> b; }
/// @brief 按索引从字符串表取回;索引越界(损坏)时置空并标记流错误
inline void read_str(QDataStream& s, QString& v, const QVector<QString>& table) {
    quint32 id = 0;
    s >> id;
    if (id < quint32(table.size())) {
        v = table[int(id)];
    } else {
        v.clear();
        s.setStatus(QDataStream::ReadCorruptData);
    }
}

inline void write_meta(QDataStream& s, const PhysicalMeta& m) {
    write_u32(s, m.timestamp);
    write_u8(s, m.phr_mcs);
    write_u8(s, m.option);
    write_u16(s, m.channel);
    write_bool(s, m.is_rf);
    write_bool(s, m.has_time_tag);
    // QDateTime 存 valid + epoch ms
    write_bool(s, m.frame_time.isValid());
    write_i64(s, m.frame_time.isValid() ? m.frame_time.toMSecsSinceEpoch() : 0);
    write_bool(s, m.from_raw);
    write_bool(s, m.frame_ts_is_ntb);
    write_bool(s, m.seg_start);
}

inline void read_meta(QDataStream& s, PhysicalMeta& m) {
    read_u32(s, m.timestamp);
    read_u8(s, m.phr_mcs);
    read_u8(s, m.option);
    read_u16(s, m.channel);
    read_bool(s, m.is_rf);
    read_bool(s, m.has_time_tag);
    bool valid = false; qint64 ms = 0;
    read_bool(s, valid);
    read_i64(s, ms);
    m.frame_time = valid ? QDateTime::fromMSecsSinceEpoch(ms) : QDateTime();
    read_bool(s, m.from_raw);
    read_bool(s, m.frame_ts_is_ntb);
    read_bool(s, m.seg_start);
}

inline void write_mpdu(QDataStream& s, const MpduInfo& m) {
    write_u8(s, m.frame_type);
    write_u8(s, m.net_type);
    write_u32(s, m.net_id);
    write_u8(s, m.version);
    write_u16(s, m.src_tei);
    write_u16(s, m.dst_tei);
    write_u8(s, m.link_id);
    write_u16(s, m.frame_len);
    write_u8(s, m.pb_num);
    write_u16(s, m.symbol_num);
    write_bool(s, m.bc_flag);
    write_bool(s, m.re_send_flag);
    write_bool(s, m.encryp_flag);
    write_u8(s, m.tmi);
    write_u8(s, m.tmi_ext);
    write_u16(s, m.pb_size);
    write_bool(s, m.fch_crc_ok);
    write_bool(s, m.pb_crc_ok);
    write_i32(s, m.pb_index);
    write_u8(s, m.pb_head);
    // QVector<quint8>
    write_u32(s, quint32(m.pb_heads.size()));
    for (quint8 v : m.pb_heads) write_u8(s, v);
    // QVector<bool> → 逐字节
    write_u32(s, quint32(m.pb_crc_oks.size()));
    for (bool v : m.pb_crc_oks) write_bool(s, v);
    // BEACON
    write_u32(s, m.beacon_timestamp);
    write_u8(s, m.beacon_line);
    write_u8(s, m.beacon_type);
    write_u8(s, m.beacon_netsn);
    s << m.beacon_cco_mac;           // quint64
    write_u32(s, m.beacon_period_cnt);
    write_u8(s, m.beacon_rf_channel);
    write_u8(s, m.beacon_rf_option);
    write_i32(s, m.beacon_item_num);
    // COORD
    write_u16(s, m.coord_duration);
    write_u16(s, m.coord_shift);
    write_u32(s, m.coord_neighbour_nid);
    write_u8(s, m.coord_rf_channel);
    write_u8(s, m.coord_rsv0);
    write_u8(s, m.coord_band_end_flag);
    write_u8(s, m.coord_option);
    write_u16(s, m.coord_band_end_offset);
    write_u16(s, m.coord_band_start_offset);
    // ACK
    write_u8(s, m.ack_ext_type);
    write_u8(s, m.ack_rx_res);
    write_u8(s, m.ack_rx_status);
    write_u8(s, m.ack_rx_pb_num);
    write_u8(s, m.ack_rsv0);
    write_u8(s, m.ack_channel_quality);
    write_u8(s, m.ack_sta_load);
    write_u8(s, m.ack_rsv1);
    s << m.ack_dst_addr;             // quint64
    write_u16(s, m.ack_search_tei);
    write_u8(s, m.ack_search_freq);
    write_u32(s, m.ack_sync_timestamp);
    write_u16(s, m.ack_sync_tei);
}

inline void read_mpdu(QDataStream& s, MpduInfo& m) {
    read_u8(s, m.frame_type);
    read_u8(s, m.net_type);
    read_u32(s, m.net_id);
    read_u8(s, m.version);
    read_u16(s, m.src_tei);
    read_u16(s, m.dst_tei);
    read_u8(s, m.link_id);
    read_u16(s, m.frame_len);
    read_u8(s, m.pb_num);
    read_u16(s, m.symbol_num);
    read_bool(s, m.bc_flag);
    read_bool(s, m.re_send_flag);
    read_bool(s, m.encryp_flag);
    read_u8(s, m.tmi);
    read_u8(s, m.tmi_ext);
    read_u16(s, m.pb_size);
    read_bool(s, m.fch_crc_ok);
    read_bool(s, m.pb_crc_ok);
    read_i32(s, m.pb_index);
    read_u8(s, m.pb_head);
    quint32 n = 0; read_u32(s, n);
    m.pb_heads.clear(); m.pb_heads.reserve(int(n));
    for (quint32 i = 0; i < n; ++i) { quint8 v; read_u8(s, v); m.pb_heads.append(v); }
    read_u32(s, n);
    m.pb_crc_oks.clear(); m.pb_crc_oks.reserve(int(n));
    for (quint32 i = 0; i < n; ++i) { bool v; read_bool(s, v); m.pb_crc_oks.append(v); }
    read_u32(s, m.beacon_timestamp);
    read_u8(s, m.beacon_line);
    read_u8(s, m.beacon_type);
    read_u8(s, m.beacon_netsn);
    s >> m.beacon_cco_mac;
    read_u32(s, m.beacon_period_cnt);
    read_u8(s, m.beacon_rf_channel);
    read_u8(s, m.beacon_rf_option);
    read_i32(s, m.beacon_item_num);
    read_u16(s, m.coord_duration);
    read_u16(s, m.coord_shift);
    read_u32(s, m.coord_neighbour_nid);
    read_u8(s, m.coord_rf_channel);
    read_u8(s, m.coord_rsv0);
    read_u8(s, m.coord_band_end_flag);
    read_u8(s, m.coord_option);
    read_u16(s, m.coord_band_end_offset);
    read_u16(s, m.coord_band_start_offset);
    read_u8(s, m.ack_ext_type);
    read_u8(s, m.ack_rx_res);
    read_u8(s, m.ack_rx_status);
    read_u8(s, m.ack_rx_pb_num);
    read_u8(s, m.ack_rsv0);
    read_u8(s, m.ack_channel_quality);
    read_u8(s, m.ack_sta_load);
    read_u8(s, m.ack_rsv1);
    s >> m.ack_dst_addr;
    read_u16(s, m.ack_search_tei);
    read_u8(s, m.ack_search_freq);
    read_u32(s, m.ack_sync_timestamp);
    read_u16(s, m.ack_sync_tei);
}

inline void write_field_node(QDataStream& s, const MsduFieldNode& n, StrPool& pool) {
    write_str(s, n.name, pool);
    write_str(s, n.value, pool);
    write_i32(s, n.rel_start);
    write_i32(s, n.rel_len);
    write_u32(s, quint32(n.children.size()));
    for (const MsduFieldNode& c : n.children) write_field_node(s, c, pool);
}

inline void read_field_node(QDataStream& s, MsduFieldNode& n,
                            const QVector<QString>& table) {
    read_str(s, n.name, table);
    read_str(s, n.value, table);
    read_i32(s, n.rel_start);
    read_i32(s, n.rel_len);
    quint32 cnt = 0; read_u32(s, cnt);
    n.children.clear(); n.children.reserve(int(cnt));
    for (quint32 i = 0; i < cnt; ++i) {
        MsduFieldNode c; read_field_node(s, c, table); n.children.append(c);
    }
}

inline void write_topo_event(QDataStream& s, const TopoEvent& t, StrPool& pool) {
    write_u8(s, static_cast<quint8>(t.kind));
    write_u32(s, t.nid);
    s << t.cco_mac;
    write_u32(s, quint32(t.nodes.size()));
    for (const TeiMacPair& p : t.nodes) { write_u16(s, p.tei); s << p.mac; }
    write_u32(s, quint32(t.routes.size()));
    for (const auto& r : t.routes) { write_u16(s, r.first); write_u16(s, r.second); }
    write_u32(s, quint32(t.leaves.size()));
    for (quint64 mac : t.leaves) s << mac;
    write_u32(s, quint32(t.comm_rates.size()));
    for (const CommRateInfo& c : t.comm_rates) { write_u16(s, c.tei); write_u8(s, c.down); write_u8(s, c.up); }
    write_str(s, t.desc, pool);
    write_i64(s, t.epoch_ms);
}

inline void read_topo_event(QDataStream& s, TopoEvent& t,
                            const QVector<QString>& table) {
    quint8 k = 0; read_u8(s, k); t.kind = static_cast<TopoEventKind>(k);
    read_u32(s, t.nid);
    s >> t.cco_mac;
    quint32 nc = 0; read_u32(s, nc);
    t.nodes.clear(); t.nodes.reserve(int(nc));
    for (quint32 i = 0; i < nc; ++i) { TeiMacPair p; read_u16(s, p.tei); s >> p.mac; t.nodes.append(p); }
    quint32 rc = 0; read_u32(s, rc);
    t.routes.clear(); t.routes.reserve(int(rc));
    for (quint32 i = 0; i < rc; ++i) { quint16 a = 0, b = 0; read_u16(s, a); read_u16(s, b); t.routes.append({a, b}); }
    quint32 lc = 0; read_u32(s, lc);
    t.leaves.clear(); t.leaves.reserve(int(lc));
    for (quint32 i = 0; i < lc; ++i) { quint64 mac = 0; s >> mac; t.leaves.append(mac); }
    quint32 cc = 0; read_u32(s, cc);
    t.comm_rates.clear(); t.comm_rates.reserve(int(cc));
    for (quint32 i = 0; i < cc; ++i) {
        CommRateInfo c; read_u16(s, c.tei); read_u8(s, c.down); read_u8(s, c.up);
        t.comm_rates.append(c);
    }
    read_str(s, t.desc, table);
    read_i64(s, t.epoch_ms);
}

inline void write_msdu_info(QDataStream& s, const MsduInfo& m, StrPool& pool) {
    write_bool(s, m.present);
    write_bool(s, m.simple_head);
    write_u16(s, m.msdu_seq);
    write_i32(s, m.msdu_src_tei);
    write_i32(s, m.msdu_dst_tei);
    write_i32(s, m.msdu_send_type);
    s << m.msdu_src_mac;   // quint64(48-bit MAC)
    s << m.msdu_dst_mac;
    s << m.sta_mac;
    write_u32(s, m.vlan_tag);
    write_u16(s, m.msdu_type);
    write_u8(s, m.restart_count);
    write_u8(s, m.broadcast_direction);
    write_u8(s, m.business_id);
    write_u8(s, m.app_packet_type);
    write_u16(s, m.mme_type);
    write_i32(s, m.total_len);
    write_str(s, m.summary, pool);
    write_u32(s, quint32(m.tree.size()));
    for (const MsduFieldNode& n : m.tree) write_field_node(s, n, pool);
    write_u32(s, quint32(m.tei_mac_pairs.size()));
    for (const TeiMacPair& p : m.tei_mac_pairs) { write_u16(s, p.tei); s << p.mac; }
    write_topo_event(s, m.topo_event, pool);
}

inline void read_msdu_info(QDataStream& s, MsduInfo& m,
                           const QVector<QString>& table) {
    read_bool(s, m.present);
    read_bool(s, m.simple_head);
    read_u16(s, m.msdu_seq);
    read_i32(s, m.msdu_src_tei);
    read_i32(s, m.msdu_dst_tei);
    read_i32(s, m.msdu_send_type);
    s >> m.msdu_src_mac;
    s >> m.msdu_dst_mac;
    s >> m.sta_mac;
    read_u32(s, m.vlan_tag);
    read_u16(s, m.msdu_type);
    read_u8(s, m.restart_count);
    read_u8(s, m.broadcast_direction);
    read_u8(s, m.business_id);
    read_u8(s, m.app_packet_type);
    read_u16(s, m.mme_type);
    read_i32(s, m.total_len);
    read_str(s, m.summary, table);
    quint32 cnt = 0; read_u32(s, cnt);
    m.tree.clear(); m.tree.reserve(int(cnt));
    for (quint32 i = 0; i < cnt; ++i) {
        MsduFieldNode n; read_field_node(s, n, table); m.tree.append(n);
    }
    quint32 pc = 0; read_u32(s, pc);
    m.tei_mac_pairs.clear(); m.tei_mac_pairs.reserve(int(pc));
    for (quint32 i = 0; i < pc; ++i) {
        TeiMacPair p; read_u16(s, p.tei); s >> p.mac; m.tei_mac_pairs.append(p);
    }
    read_topo_event(s, m.topo_event, table);
}

/// @brief 序列化一个 PacketEntry 到字节流(不含长度前缀)
/// @details search_text 不在本载荷内:单条往返时由 deserialize_entry 按派生
///          规则重建;块文件 v3 则把它存进块索引,BlockReader 解码时回填。
///          字符串经 pool 去重,调用方(encode_block)负责先写字符串表。
inline QByteArray serialize_entry(const PacketEntry& e, StrPool& pool) {
    QByteArray buf;
    QDataStream s(&buf, QIODevice::WriteOnly);
    write_i32(s, e.index);
    write_i64(s, e.epoch_ms);
    write_i64(s, e.delta_us);
    write_bool(s, e.accepted);
    write_str(s, e.reason, pool);
    write_bytes(s, e.raw_wire);
    write_meta(s, e.meta);
    write_mpdu(s, e.mpdu);
    write_bytes(s, e.msdu_body);
    write_msdu_info(s, e.msdu, pool);
    write_msdu_info(s, e.beacon, pool);
    write_i32(s, e.msdu_raw_base);
    write_bytes(s, e.raw_bytes);
    return buf;
}

/// @brief 反序列化一个 PacketEntry(与 serialize_entry 严格互逆)
/// @param rebuild_search true=按派生规则重建 search_text(单条往返/旧路径);
///        false=不重建,由调用方从块索引回填 v3 落盘值(BlockReader 用)
inline bool deserialize_entry(const QByteArray& buf, PacketEntry& e,
                              const QVector<QString>& table,
                              bool rebuild_search = true) {
    QDataStream s(buf);
    read_i32(s, e.index);
    read_i64(s, e.epoch_ms);
    read_i64(s, e.delta_us);
    read_bool(s, e.accepted);
    read_str(s, e.reason, table);
    read_bytes(s, e.raw_wire);
    read_meta(s, e.meta);
    read_mpdu(s, e.mpdu);
    read_bytes(s, e.msdu_body);
    read_msdu_info(s, e.msdu, table);
    read_msdu_info(s, e.beacon, table);
    read_i32(s, e.msdu_raw_base);
    read_bytes(s, e.raw_bytes);
    if (s.status() != QDataStream::Ok)
        return false;
    if (rebuild_search)
        e.search_text = make_search_text(e);  // 派生字段:加载时重建
    return true;
}

// ---------------------------------------------------------------------------
// 块文件格式 v3(字符串池 + 轻量索引 + 条目分块独立压缩)
// 文件布局(QDataStream 顺序写入,偏移为文件绝对字节):
//   [magic u32][version u32=3][count u32]
//   [pool_zipped u8][pool_blob]        blob = qCompress(pool_raw) 或 pool_raw
//     pool_raw = [str_count u32][str...]        (块内字符串去重表)
//   [index_raw]                         每条 9B:[frame_type u8][src_tei u16]
//                                       [dst_tei u16][search_text 池索引 u32]
//   [nchunks u32][{off u32,len u32,zipped u8} × nchunks]
//   [chunk_blob × nchunks]              每块 ≤ kBlockChunkEntries 条:
//                                       chunk_raw = [len u32][payload] × n,
//                                       blob = qCompress(chunk_raw) 或 chunk_raw;
//                                       payload 与 v2 相同(serialize_entry,
//                                       字符串以池索引引用)
// 设计目标(2026-10 实测驱动):滚动只解一屏需要的条目(按条随机解码,不再整块
// 解压+逐条反序列化+重建 search_text);筛选只读池+索引(search_text 已落盘),
// 不碰条目正文。search_text 存索引区(池索引),反序列化后由 BlockReader 回填。
// 换页临时文件仅同一进程会话内往返,不做跨版本兼容;魔数/条数/偏移/解压校验
// 用于识别截断文件(磁盘满等),解码失败时调用方不得使用残缺数据。
// ---------------------------------------------------------------------------

inline quint32 block_magic()   { return 0x50424C4Bu; }  // "PBLK"
inline quint32 block_version() { return 3u; }
/// @brief 独立压缩分块的条目数(随机解码粒度:解一条最多解压一个分块)
inline constexpr int kBlockChunkEntries = 50;

namespace detail {
/// @brief 大端 u32 读取(与 QDataStream 默认字节序一致,供分块内偏移步进)
inline quint32 read_be32(const char* p) {
    return (quint32(quint8(p[0])) << 24) | (quint32(quint8(p[1])) << 16)
         | (quint32(quint8(p[2])) << 8)  |  quint32(quint8(p[3]));
}
/// @brief 压缩(1 档速度优先);压不动/失败则原样返回并置 zipped=false
inline QByteArray compress_or_raw(const QByteArray& raw, bool& zipped) {
    const QByteArray comp = qCompress(raw, 1);
    if (!comp.isEmpty() && comp.size() < raw.size()) {
        zipped = true;
        return comp;
    }
    zipped = false;
    return raw;
}
}  // namespace detail

/// @brief 把一整块条目编码为待写入文件的内容(v3 布局)
inline QByteArray encode_block(const QVector<PacketEntry>& entries) {
    if (entries.isEmpty()) return {};
    StrPool pool;
    QVector<QByteArray> payloads;
    QVector<quint32>    search_idx;
    payloads.reserve(entries.size());
    search_idx.reserve(entries.size());
    for (const PacketEntry& e : entries) {
        payloads.append(serialize_entry(e, pool));
        // search_text 落盘:空时按派生规则补算(模型路径恒已填充)
        search_idx.append(pool.intern(e.search_text.isEmpty()
                                          ? make_search_text(e)
                                          : e.search_text));
    }
    // 字符串池段
    QByteArray pool_raw;
    {
        QDataStream s(&pool_raw, QIODevice::WriteOnly);
        s << quint32(pool.list.size());
        for (const QString& str : pool.list) s << str;
    }
    bool pool_zipped = false;
    const QByteArray pool_blob = detail::compress_or_raw(pool_raw, pool_zipped);
    // 轻量索引段(帧型/TEI/search_text 索引,筛选专用,不解码正文)
    QByteArray index_raw;
    {
        QDataStream s(&index_raw, QIODevice::WriteOnly);
        for (int i = 0; i < entries.size(); ++i) {
            s << quint8(entries[i].mpdu.frame_type)
              << quint16(entries[i].mpdu.src_tei)
              << quint16(entries[i].mpdu.dst_tei)
              << search_idx[i];
        }
    }
    // 条目分块段(每分块独立压缩,支撑按条随机解码)
    const int nchunks = (entries.size() + kBlockChunkEntries - 1)
                        / kBlockChunkEntries;
    QVector<QByteArray> chunk_blobs;
    QVector<bool>       chunk_zipped;
    chunk_blobs.reserve(nchunks);
    for (int c = 0; c < nchunks; ++c) {
        QByteArray raw;
        QDataStream s(&raw, QIODevice::WriteOnly);
        const int end = qMin(entries.size(), (c + 1) * kBlockChunkEntries);
        for (int i = c * kBlockChunkEntries; i < end; ++i) {
            s << quint32(payloads[i].size());
            s.writeRawData(payloads[i].constData(), payloads[i].size());
        }
        bool z = false;
        chunk_blobs.append(detail::compress_or_raw(raw, z));
        chunk_zipped.append(z);
    }
    // 文件头(分块表偏移为绝对文件偏移,先按字段尺寸算出头长)
    const qint64 header_len = 4 + 4 + 4 + 1 + (4 + pool_blob.size())
                            + (4 + index_raw.size()) + 4 + qint64(nchunks) * 9;
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s << block_magic() << block_version() << quint32(entries.size());
    s << quint8(pool_zipped ? 1 : 0) << pool_blob;
    s << index_raw;
    s << quint32(nchunks);
    qint64 off = header_len;
    for (int c = 0; c < nchunks; ++c) {
        s << quint32(off) << quint32(chunk_blobs[c].size())
          << quint8(chunk_zipped[c] ? 1 : 0);
        off += chunk_blobs[c].size();
    }
    for (const QByteArray& b : chunk_blobs)
        s.writeRawData(b.constData(), b.size());
    return out;
}

/// @brief v3 块文件随机访问读取器:打开只解析头/池/索引,条目按需解码
/// @details 同一时刻只缓存最近一个分块的解压结果与一条条目(滚动/绘制的
///          实际访问模式);read_all 顺序遍历时分块只解压一次。
class BlockReader {
public:
    bool open(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray data = f.readAll();
        f.close();
        if (data.isEmpty()) return false;
        return open_data(data);
    }

    bool open_data(const QByteArray& data) {
        m_data = data;
        m_table.clear();
        m_index.clear();
        m_chunks.clear();
        m_chunk_no = -1;
        m_chunk_raw.clear();
        m_chunk_pay.clear();
        m_ref_idx = -1;
        QDataStream s(m_data);
        quint32 magic = 0, ver = 0, count = 0;
        quint8 pool_zipped = 0;
        QByteArray pool_blob, index_raw;
        s >> magic >> ver >> count >> pool_zipped >> pool_blob >> index_raw;
        if (s.status() != QDataStream::Ok || magic != block_magic()
            || ver != block_version() || count == 0 || count > 100000)
            return false;
        const QByteArray pool_raw = pool_zipped ? qUncompress(pool_blob)
                                                : pool_blob;
        if (pool_raw.isEmpty()) return false;
        {
            QDataStream ps(pool_raw);
            quint32 str_count = 0;
            ps >> str_count;
            if (ps.status() != QDataStream::Ok || str_count > 1000000)
                return false;
            m_table.reserve(int(str_count));
            for (quint32 i = 0; i < str_count; ++i) {
                QString str;
                ps >> str;
                if (ps.status() != QDataStream::Ok) return false;
                m_table.append(str);
            }
        }
        {
            QDataStream is(index_raw);
            m_index.reserve(int(count));
            for (quint32 i = 0; i < count; ++i) {
                IndexEntry ix;
                is >> ix.frame_type >> ix.src_tei >> ix.dst_tei >> ix.search_idx;
                if (is.status() != QDataStream::Ok) return false;
                if (ix.search_idx >= quint32(m_table.size())) return false;
                m_index.append(ix);
            }
        }
        quint32 nchunks = 0;
        s >> nchunks;
        if (s.status() != QDataStream::Ok
            || nchunks != quint32((count + kBlockChunkEntries - 1)
                                  / kBlockChunkEntries))
            return false;
        m_chunks.reserve(int(nchunks));
        for (quint32 c = 0; c < nchunks; ++c) {
            Chunk ch;
            quint8 z = 0;
            s >> ch.off >> ch.len >> z;
            ch.zipped = (z != 0);
            if (s.status() != QDataStream::Ok) return false;
            if (qint64(ch.off) + ch.len > m_data.size()) return false;
            m_chunks.append(ch);
        }
        m_count = int(count);
        return true;
    }

    int count() const { return m_count; }

    // ---- 轻量索引访问(筛选扫描用,不解码条目正文) ----
    quint8  frame_type(int i) const { return m_index[i].frame_type; }
    quint16 src_tei(int i) const    { return m_index[i].src_tei; }
    quint16 dst_tei(int i) const    { return m_index[i].dst_tei; }
    const QString& search_text(int i) const {
        return m_table[int(m_index[i].search_idx)];
    }

    /// @brief 按条随机解码(只解压所在分块,只反序列化目标条)
    bool entry_at(int i, PacketEntry& out) const {
        if (i < 0 || i >= m_count) return false;
        const int c = i / kBlockChunkEntries;
        if (c != m_chunk_no && !load_chunk(c)) return false;
        const int j = i - c * kBlockChunkEntries;
        if (j >= m_chunk_pay.size()) return false;
        const PayRec& rec = m_chunk_pay[j];
        const QByteArray payload =
            m_chunk_raw.mid(rec.off, rec.len);
        if (!deserialize_entry(payload, out, m_table, /*rebuild_search=*/false))
            return false;
        out.search_text = search_text(i);   // 回填落盘值,不重建
        return true;
    }

    /// @brief entry_at 的引用版(单条缓存,供模型 locate 的瞬时使用场景)
    const PacketEntry& entry_ref(int i, bool& ok) const {
        if (i == m_ref_idx) { ok = true; return m_ref_entry; }
        ok = entry_at(i, m_ref_entry);
        if (ok) m_ref_idx = i;
        return m_ref_entry;
    }

    /// @brief 顺序解出整块(导出/for_each/测试用;分块各只解压一次)
    bool read_all(QVector<PacketEntry>& entries) const {
        entries.clear();
        entries.reserve(m_count);
        for (int i = 0; i < m_count; ++i) {
            PacketEntry e;
            if (!entry_at(i, e)) { entries.clear(); return false; }
            entries.append(std::move(e));
        }
        return entries.size() == m_count;
    }

private:
    struct IndexEntry {
        quint8  frame_type = 0;
        quint16 src_tei = 0;
        quint16 dst_tei = 0;
        quint32 search_idx = 0;
    };
    struct Chunk {
        quint32 off = 0;
        quint32 len = 0;
        bool    zipped = false;
    };
    struct PayRec { int off = 0; int len = 0; };

    bool load_chunk(int c) const {
        const Chunk& ch = m_chunks[c];
        const QByteArray slice = m_data.mid(int(ch.off), int(ch.len));
        const QByteArray raw = ch.zipped ? qUncompress(slice) : slice;
        if (raw.isEmpty()) return false;
        const int expect = qMin(kBlockChunkEntries,
                                m_count - c * kBlockChunkEntries);
        QVector<PayRec> pays;
        pays.reserve(expect);
        int pos = 0;
        for (int j = 0; j < expect; ++j) {
            if (pos + 4 > raw.size()) return false;
            const quint32 len = detail::read_be32(raw.constData() + pos);
            pos += 4;
            if (len == 0 || pos + int(len) > raw.size()) return false;
            pays.append({pos, int(len)});
            pos += int(len);
        }
        if (pos != raw.size()) return false;   // 分块内容必须恰好用尽
        m_chunk_raw = raw;
        m_chunk_pay = pays;
        m_chunk_no = c;
        m_ref_idx = -1;   // 分块换出,单条缓存随之失效
        return true;
    }

    QByteArray          m_data;
    int                 m_count = 0;
    QVector<QString>    m_table;
    QVector<IndexEntry> m_index;
    QVector<Chunk>      m_chunks;
    mutable int         m_chunk_no = -1;
    mutable QByteArray  m_chunk_raw;
    mutable QVector<PayRec> m_chunk_pay;
    mutable int         m_ref_idx = -1;
    mutable PacketEntry m_ref_entry;
};

/// @brief 解码块文件内容;任一步校验失败返回 false 并清空 entries
inline bool decode_block(const QByteArray& data, QVector<PacketEntry>& entries) {
    BlockReader r;
    if (!r.open_data(data)) { entries.clear(); return false; }
    return r.read_all(entries);
}

/// @brief 从块文件路径直接读出一整块条目,失败返回 false
inline bool read_block_file(const QString& path, QVector<PacketEntry>& entries) {
    BlockReader r;
    if (!r.open(path)) { entries.clear(); return false; }
    return r.read_all(entries);
}

}  // namespace pser

#endif // PACKETENTRY_SERIALIZE_H
