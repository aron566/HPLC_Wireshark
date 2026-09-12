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

namespace pser {

inline void write_u32(QDataStream& s, quint32 v) { s << v; }
inline void write_u16(QDataStream& s, quint16 v) { s << v; }
inline void write_u8(QDataStream& s, quint8 v)  { s << v; }
inline void write_i32(QDataStream& s, qint32 v) { s << v; }
inline void write_i64(QDataStream& s, qint64 v) { s << v; }
inline void write_bool(QDataStream& s, bool v)   { s << v; }
inline void write_bytes(QDataStream& s, const QByteArray& b) { s << b; }
inline void write_str(QDataStream& s, const QString& v)       { s << v; }

inline void read_u32(QDataStream& s, quint32& v) { s >> v; }
inline void read_u16(QDataStream& s, quint16& v) { s >> v; }
inline void read_u8(QDataStream& s, quint8& v)   { s >> v; }
inline void read_i32(QDataStream& s, qint32& v)  { s >> v; }
inline void read_i64(QDataStream& s, qint64& v)  { s >> v; }
inline void read_bool(QDataStream& s, bool& v)   { s >> v; }
inline void read_bytes(QDataStream& s, QByteArray& b) { s >> b; }
inline void read_str(QDataStream& s, QString& v)       { s >> v; }

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

inline void write_field_node(QDataStream& s, const MsduFieldNode& n) {
    write_str(s, n.name);
    write_str(s, n.value);
    write_i32(s, n.rel_start);
    write_i32(s, n.rel_len);
    write_u32(s, quint32(n.children.size()));
    for (const MsduFieldNode& c : n.children) write_field_node(s, c);
}

inline void read_field_node(QDataStream& s, MsduFieldNode& n) {
    read_str(s, n.name);
    read_str(s, n.value);
    read_i32(s, n.rel_start);
    read_i32(s, n.rel_len);
    quint32 cnt = 0; read_u32(s, cnt);
    n.children.clear(); n.children.reserve(int(cnt));
    for (quint32 i = 0; i < cnt; ++i) {
        MsduFieldNode c; read_field_node(s, c); n.children.append(c);
    }
}

inline void write_msdu_info(QDataStream& s, const MsduInfo& m) {
    write_bool(s, m.present);
    write_bool(s, m.simple_head);
    write_u16(s, m.msdu_seq);
    write_i32(s, m.msdu_src_tei);
    write_i32(s, m.msdu_dst_tei);
    write_i32(s, m.msdu_send_type);
    write_i32(s, m.total_len);
    write_str(s, m.summary);
    write_u32(s, quint32(m.tree.size()));
    for (const MsduFieldNode& n : m.tree) write_field_node(s, n);
    write_u32(s, quint32(m.tei_mac_pairs.size()));
    for (const TeiMacPair& p : m.tei_mac_pairs) { write_u16(s, p.tei); s << p.mac; }
}

inline void read_msdu_info(QDataStream& s, MsduInfo& m) {
    read_bool(s, m.present);
    read_bool(s, m.simple_head);
    read_u16(s, m.msdu_seq);
    read_i32(s, m.msdu_src_tei);
    read_i32(s, m.msdu_dst_tei);
    read_i32(s, m.msdu_send_type);
    read_i32(s, m.total_len);
    read_str(s, m.summary);
    quint32 cnt = 0; read_u32(s, cnt);
    m.tree.clear(); m.tree.reserve(int(cnt));
    for (quint32 i = 0; i < cnt; ++i) {
        MsduFieldNode n; read_field_node(s, n); m.tree.append(n);
    }
    quint32 pc = 0; read_u32(s, pc);
    m.tei_mac_pairs.clear(); m.tei_mac_pairs.reserve(int(pc));
    for (quint32 i = 0; i < pc; ++i) {
        TeiMacPair p; read_u16(s, p.tei); s >> p.mac; m.tei_mac_pairs.append(p);
    }
}

/// @brief 序列化一个 PacketEntry 到字节流(不含长度前缀)
inline QByteArray serialize_entry(const PacketEntry& e) {
    QByteArray buf;
    QDataStream s(&buf, QIODevice::WriteOnly);
    write_i32(s, e.index);
    write_i64(s, e.epoch_ms);
    write_i64(s, e.delta_us);
    write_bool(s, e.accepted);
    write_str(s, e.reason);
    write_bytes(s, e.raw_wire);
    write_meta(s, e.meta);
    write_mpdu(s, e.mpdu);
    write_bytes(s, e.msdu_body);
    write_msdu_info(s, e.msdu);
    write_msdu_info(s, e.beacon);
    write_i32(s, e.msdu_raw_base);
    write_bytes(s, e.raw_bytes);
    return buf;
}

/// @brief 反序列化一个 PacketEntry(与 serialize_entry 严格互逆)
inline bool deserialize_entry(const QByteArray& buf, PacketEntry& e) {
    QDataStream s(buf);
    read_i32(s, e.index);
    read_i64(s, e.epoch_ms);
    read_i64(s, e.delta_us);
    read_bool(s, e.accepted);
    read_str(s, e.reason);
    read_bytes(s, e.raw_wire);
    read_meta(s, e.meta);
    read_mpdu(s, e.mpdu);
    read_bytes(s, e.msdu_body);
    read_msdu_info(s, e.msdu);
    read_msdu_info(s, e.beacon);
    read_i32(s, e.msdu_raw_base);
    read_bytes(s, e.raw_bytes);
    return s.status() == QDataStream::Ok;
}

}  // namespace pser

#endif // PACKETENTRY_SERIALIZE_H
