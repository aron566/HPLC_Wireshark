/// @file plugin_serialization.cpp
/// @brief 插件 IPC 用 QDataStream 序列化实现
#include "plugin_serialization.h"

#include <QBuffer>
#include <QImage>

// ---- PhysicalMeta ----
QDataStream& operator<<(QDataStream& out, const PhysicalMeta& m) {
    out << m.timestamp << m.phr_mcs << m.option << m.channel << m.is_rf;
    return out;
}
QDataStream& operator>>(QDataStream& in, PhysicalMeta& m) {
    in >> m.timestamp >> m.phr_mcs >> m.option >> m.channel >> m.is_rf;
    return in;
}

// ---- BplcFrame ----
QDataStream& operator<<(QDataStream& out, const BplcFrame& f) {
    out << f.meta << f.data << f.error_reason
        << f.arrival_ms << f.arrival_us << f.raw_wire
        << f.decoded_index << f.decoded_epoch_ms << f.topo_event
        << f.accepted << f.mpdu << f.msdu_summary << f.msdu_present;
    return out;
}
QDataStream& operator>>(QDataStream& in, BplcFrame& f) {
    in >> f.meta >> f.data >> f.error_reason
       >> f.arrival_ms >> f.arrival_us >> f.raw_wire
       >> f.decoded_index >> f.decoded_epoch_ms >> f.topo_event
       >> f.accepted >> f.mpdu >> f.msdu_summary >> f.msdu_present;
    return in;
}

// ---- MsduState ----
QDataStream& operator<<(QDataStream& out, const MsduState& s) {
    out << s.buffer << s.received_count << s.expected_len << s.complete;
    return out;
}
QDataStream& operator>>(QDataStream& in, MsduState& s) {
    in >> s.buffer >> s.received_count >> s.expected_len >> s.complete;
    return in;
}

// ---- ParseFilter ----
QDataStream& operator<<(QDataStream& out, const ParseFilter& f) {
    out << f.enable_type_filter << f.allow_beacon << f.allow_sof << f.allow_ack
        << f.allow_coord << f.link_hplc << f.link_hrf
        << f.nid_filter << f.nid_mask << f.nid_list
        << f.tei_filter << f.tei_list;
    return out;
}
QDataStream& operator>>(QDataStream& in, ParseFilter& f) {
    in >> f.enable_type_filter >> f.allow_beacon >> f.allow_sof >> f.allow_ack
       >> f.allow_coord >> f.link_hplc >> f.link_hrf
       >> f.nid_filter >> f.nid_mask >> f.nid_list
       >> f.tei_filter >> f.tei_list;
    return in;
}

// ---- MpduInfo ----
QDataStream& operator<<(QDataStream& out, const MpduInfo& m) {
    out << m.frame_type << m.net_type << m.net_id << m.version
        << m.src_tei << m.dst_tei << m.link_id << m.frame_len
        << m.pb_num << m.symbol_num
        << m.bc_flag << m.re_send_flag << m.encryp_flag
        << m.tmi << m.tmi_ext << m.pb_size
        << m.fch_crc_ok << m.pb_crc_ok << m.pb_index << m.pb_head
        << m.pb_heads << m.pb_crc_oks
        << m.beacon_timestamp << m.beacon_line
        << m.beacon_type << m.beacon_netsn << m.beacon_cco_mac
        << m.beacon_period_cnt << m.beacon_rf_channel << m.beacon_rf_option
        << m.beacon_item_num
        << m.coord_duration << m.coord_shift << m.coord_neighbour_nid
        << m.coord_rf_channel << m.coord_rsv0 << m.coord_rsv1
        << m.coord_rsv2 << m.coord_rsv3
        << m.coord_band_end_flag << m.coord_option
        << m.coord_band_end_offset << m.coord_band_start_offset
        << m.ack_ext_type << m.ack_rx_res << m.ack_rx_status << m.ack_rx_pb_num
        << m.ack_rsv0 << m.ack_channel_quality << m.ack_sta_load << m.ack_rsv1
        << m.ack_dst_addr
        << m.ack_search_tei << m.ack_search_freq
        << m.ack_sync_timestamp << m.ack_sync_tei;
    return out;
}
QDataStream& operator>>(QDataStream& in, MpduInfo& m) {
    in >> m.frame_type >> m.net_type >> m.net_id >> m.version
       >> m.src_tei >> m.dst_tei >> m.link_id >> m.frame_len
       >> m.pb_num >> m.symbol_num
       >> m.bc_flag >> m.re_send_flag >> m.encryp_flag
       >> m.tmi >> m.tmi_ext >> m.pb_size
       >> m.fch_crc_ok >> m.pb_crc_ok >> m.pb_index >> m.pb_head
       >> m.pb_heads >> m.pb_crc_oks
       >> m.beacon_timestamp >> m.beacon_line
       >> m.beacon_type >> m.beacon_netsn >> m.beacon_cco_mac
       >> m.beacon_period_cnt >> m.beacon_rf_channel >> m.beacon_rf_option
       >> m.beacon_item_num
       >> m.coord_duration >> m.coord_shift >> m.coord_neighbour_nid
       >> m.coord_rf_channel >> m.coord_rsv0 >> m.coord_rsv1
       >> m.coord_rsv2 >> m.coord_rsv3
       >> m.coord_band_end_flag >> m.coord_option
       >> m.coord_band_end_offset >> m.coord_band_start_offset
       >> m.ack_ext_type >> m.ack_rx_res >> m.ack_rx_status >> m.ack_rx_pb_num
       >> m.ack_rsv0 >> m.ack_channel_quality >> m.ack_sta_load >> m.ack_rsv1
       >> m.ack_dst_addr
       >> m.ack_search_tei >> m.ack_search_freq
       >> m.ack_sync_timestamp >> m.ack_sync_tei;
    return in;
}

// ---- MsduFieldNode (递归) ----
QDataStream& operator<<(QDataStream& out, const MsduFieldNode& n) {
    out << n.name << n.value << n.children << n.rel_start << n.rel_len;
    return out;
}
QDataStream& operator>>(QDataStream& in, MsduFieldNode& n) {
    in >> n.name >> n.value >> n.children >> n.rel_start >> n.rel_len;
    return in;
}

// ---- TeiMacPair ----
QDataStream& operator<<(QDataStream& out, const TeiMacPair& p) {
    out << p.tei << p.mac;
    return out;
}
QDataStream& operator>>(QDataStream& in, TeiMacPair& p) {
    in >> p.tei >> p.mac;
    return in;
}

// ---- CommRateInfo ----
QDataStream& operator<<(QDataStream& out, const CommRateInfo& c) {
    out << c.tei << c.down << c.up;
    return out;
}
QDataStream& operator>>(QDataStream& in, CommRateInfo& c) {
    in >> c.tei >> c.down >> c.up;
    return in;
}

// ---- TopoEvent ----
QDataStream& operator<<(QDataStream& out, const TopoEvent& e) {
    out << static_cast<quint8>(e.kind)
        << e.nid << e.cco_mac << e.nodes << e.routes << e.up_routes
        << e.discover_src_tei << e.neighbor_teis << e.leaves << e.comm_rates
        << e.is_rf << e.restart_count << e.desc << e.epoch_ms << e.frame_index;
    return out;
}
QDataStream& operator>>(QDataStream& in, TopoEvent& e) {
    quint8 k = 0;
    in >> k
       >> e.nid >> e.cco_mac >> e.nodes >> e.routes >> e.up_routes
       >> e.discover_src_tei >> e.neighbor_teis >> e.leaves >> e.comm_rates
       >> e.is_rf >> e.restart_count >> e.desc >> e.epoch_ms >> e.frame_index;
    e.kind = static_cast<TopoEventKind>(k);
    return in;
}

// ---- MsduInfo ----
QDataStream& operator<<(QDataStream& out, const MsduInfo& m) {
    out << m.present << m.simple_head << m.msdu_seq
        << m.msdu_src_tei << m.msdu_dst_tei << m.msdu_send_type
        << m.msdu_src_mac << m.msdu_dst_mac << m.sta_mac
        << m.vlan_tag << m.msdu_type << m.restart_count
        << m.broadcast_direction << m.business_id << m.app_packet_type
        << m.mme_type << m.total_len << m.summary << m.tree
        << m.tei_mac_pairs << m.topo_event;
    return out;
}
QDataStream& operator>>(QDataStream& in, MsduInfo& m) {
    in >> m.present >> m.simple_head >> m.msdu_seq
       >> m.msdu_src_tei >> m.msdu_dst_tei >> m.msdu_send_type
       >> m.msdu_src_mac >> m.msdu_dst_mac >> m.sta_mac
       >> m.vlan_tag >> m.msdu_type >> m.restart_count
       >> m.broadcast_direction >> m.business_id >> m.app_packet_type
       >> m.mme_type >> m.total_len >> m.summary >> m.tree
       >> m.tei_mac_pairs >> m.topo_event;
    return in;
}

// ---- ParseResult ----
QDataStream& operator<<(QDataStream& out, const ParseResult& r) {
    out << r.meta << r.mpdu << r.msdu_body << r.msdu
        << r.msdu_raw_base << r.beacon << r.arrival_us
        << r.raw_wire << r.accept << r.reject_reason << r.payload_for_log;
    return out;
}
QDataStream& operator>>(QDataStream& in, ParseResult& r) {
    in >> r.meta >> r.mpdu >> r.msdu_body >> r.msdu
       >> r.msdu_raw_base >> r.beacon >> r.arrival_us
       >> r.raw_wire >> r.accept >> r.reject_reason >> r.payload_for_log;
    return in;
}

// ---- GraphicsEvent (Phase3) ----
QDataStream& operator<<(QDataStream& out, const GraphicsEvent& e) {
    out << static_cast<quint8>(e.type) << e.x << e.y << e.button
        << e.modifiers << e.delta_y << e.width << e.height;
    return out;
}
QDataStream& operator>>(QDataStream& in, GraphicsEvent& e) {
    quint8 t = 0;
    in >> t >> e.x >> e.y >> e.button >> e.modifiers >> e.delta_y
       >> e.width >> e.height;
    e.type = static_cast<GraphicsEventType>(t);
    return in;
}

// ---- QImage (Phase3): PNG 压缩,限 8MB ----
namespace {
constexpr int kMaxImageBytes = 8 * 1024 * 1024;
}
QDataStream& operator<<(QDataStream& out, const QImage& img) {
    QByteArray png;
    if (!img.isNull()) {
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
    }
    out << png;
    return out;
}
QDataStream& operator>>(QDataStream& in, QImage& img) {
    QByteArray png;
    in >> png;
    img = QImage();
    if (!png.isEmpty() && png.size() <= kMaxImageBytes)
        img.loadFromData(png, "PNG");
    return in;
}
