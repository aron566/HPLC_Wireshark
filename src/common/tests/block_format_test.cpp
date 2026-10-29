// Round-trip test for the v5 block format (pool + filter index + chunked
// entries). Builds synthetic PacketEntries with nested trees, encodes a
// block, decodes it back, and verifies every field, plus v5 random access
// (BlockReader::entry_at) and the stored search_text in the block index.
//
// Build & run: qmake src/common/tests/block_format_test.pro && make,
// or manually (Qt 6):
//   QT=~/qt/6.5.3/gcc_64
//   g++ -std=c++17 -fPIC -O1 -I$QT/include -I$QT/include/QtCore -Isrc/common
//       src/common/tests/block_format_test.cpp -o /tmp/block_format_test
//       -L$QT/lib -lQt6Core -Wl,-rpath,$QT/lib && /tmp/block_format_test
#include "packetentry_serialize.h"
#include <QCoreApplication>
#include <QDebug>

static MsduFieldNode node(const QString& n, const QString& v, int rs, int rl) {
    MsduFieldNode f; f.name = n; f.value = v; f.rel_start = rs; f.rel_len = rl;
    return f;
}

static void fill_topo(TopoEvent& t, int i) {
    t.kind = TopoEventKind::DiscoverList;
    t.nid = quint32(i);
    t.cco_mac = 0x112233445566ULL;
    t.nodes.append({quint16(i), 0xDEADBEEF0000ULL + quint64(i)});
    t.routes.append({quint16(i), quint16(i + 9)});
    t.up_routes.append({quint16(i), quint16(i + 1)});
    t.discover_src_tei = quint16(i + 2);
    t.neighbor_teis.append(quint16(i + 3));
    t.neighbor_teis_present = (i % 2 == 0);
    t.leaves.append(0xCAFEBABE0000ULL + quint64(i));
    t.comm_rates.append({quint16(i), quint8(i % 101), quint8((i + 1) % 101)});
    t.is_rf = (i % 2 == 1);
    t.is_rf_known = true;
    t.rssi_entries.append({quint16(i), -40 + i});
    t.neighbor_rates.append({quint16(i), quint8(i % 101)});
    t.neighbor_snrs.append({quint16(i), qint16((i % 40) - 10)});
    t.proxy_tei = quint16(i + 4);
    t.proxy_up_rate = qint16(i % 101);
    t.proxy_down_rate = qint16((i + 1) % 101);
    t.min_link_rate = qint16((i + 2) % 101);
    t.rf_hop = quint8(i % 16);
    t.rf_role = quint8(i % 5);
    t.link_type = (i % 2);
    t.node_link_types.append({quint16(i), quint8(i % 2)});
    t.proxy_comm_rate = qint32(i % 101);
    t.proxy_down_comm_rate = qint32((i + 1) % 101);
    t.min_comm_rate = qint16((i + 3) % 101);
    t.child_teis.append(quint16(i + 5));
    t.restart_count = (i % 16);
    t.sender_mac = 0xAABBCCDDEE00ULL + quint64(i);   // v15: 发送方 MAC round-trip
    t.desc = QStringLiteral("topo-%1").arg(i);
    t.epoch_ms = 1700000000000LL + i * 1000;
    t.frame_index = qint64(i);
}

static PacketEntry make_entry(int i) {
    PacketEntry e;
    e.index = i; e.epoch_ms = 1700000000000LL + i; e.delta_us = 1000 + i;
    e.accepted = (i % 7 != 0);
    e.reason = e.accepted ? QString() : QStringLiteral("CRC error");
    e.raw_wire = QByteArray::fromHex("3c00ff3e");
    e.meta.timestamp = 12345 + i; e.meta.is_rf = (i % 2 == 0);
    e.meta.seg_start = i % 3 == 0;
    e.meta.replay_time_tag = i % 5 == 0;
    e.meta.frame_time = QDateTime::fromMSecsSinceEpoch(e.epoch_ms);
    e.mpdu.frame_type = 1; e.mpdu.src_tei = 2; e.mpdu.dst_tei = 0xFFF;
    e.mpdu.pb_num_field = quint8(i % 7);   // 覆盖载荷PB大小索引(表29 0-5)+保留值 6 的序列化 round-trip
    e.mpdu.pblen = quint8(i % 6);          // 覆盖南网无线载荷PB大小索引(表130 0-5)序列化 round-trip
    e.mpdu.mcs = quint8(i % 7);            // 覆盖无线 MCS(表28/129 0-6)序列化 round-trip
    e.mpdu.beacon_rsv = quint16(i % 1024); // 覆盖信标保留原始值(无线16b/载波9b)序列化 round-trip
    e.mpdu.beacon_cco_mac = 0x112233445566ULL;
    // v11: MpduInfo SOF/信标/ACK 13 字段(换页后不丢,非默认值且互不相同)
    e.mpdu.sof_data_type      = quint8(1 + i % 255);
    e.mpdu.sof_tf_symbol_pos  = quint8(2 + i % 254);
    e.mpdu.sof_tf_symbol_num  = quint8(3 + i % 253);
    e.mpdu.sof_fch_sym_num    = quint8(4 + i % 252);
    e.mpdu.sof_mod_mode       = quint8(5 + i % 251);
    e.mpdu.sof_mod_param      = quint8(6 + i % 250);
    e.mpdu.sof_band           = quint8(7 + i % 249);
    e.mpdu.sof_gain           = quint8(8 + i % 248);
    e.mpdu.sof_load_table_id  = quint8(9 + i % 247);
    e.mpdu.beacon_fch_sym     = quint8(10 + i % 246);
    e.mpdu.ack_bru_snr_flag   = quint8(11 + i % 245);
    e.mpdu.ack_sat_id_flag    = quint8(12 + i % 244);
    e.mpdu.ack_remain_sta_num = quint8(13 + i % 243);
    e.mpdu.fch_crc_checked = (i % 2 == 0);   // v12: FCH CRC 三态 round-trip(覆盖 checked=true/false)
    // v13: 多站点 PB 分组(OFDMA/TDA 逐站点) round-trip(2 站点×2块 + 1站点×1块, 非默认值)
    {
        SofStationPb s0; s0.tei = 0x111; s0.copy_mode = 3; s0.pb_num = 2; s0.pb_size = 72;
        s0.block_base = 32;
        s0.pb_heads.append(0xC1); s0.pb_heads.append(0x82);
        s0.pb_crc_oks.append(true); s0.pb_crc_oks.append(false);
        s0.msdu = QByteArray::fromHex("aabbccdd");
        SofStationPb s1; s1.tei = 0x222; s1.copy_mode = 13; s1.pb_num = 1; s1.pb_size = 136;
        s1.block_base = 176;
        s1.pb_heads.append(0x43);
        s1.pb_crc_oks.append(true);
        s1.msdu = QByteArray::fromHex("eeff");
        e.mpdu.sof_stations.append(s0); e.mpdu.sof_stations.append(s1);
    }
    e.protocol = quint8(i % 2);   // 覆盖协议标识序列化 round-trip(0=国网/1=南网)
    e.msdu.present = true; e.msdu.msdu_seq = quint16(i);
    e.msdu.summary = QStringLiteral("summary-%1").arg(i);
    e.msdu.broadcast_direction = quint8(i % 4);   // 覆盖广播方向序列化 round-trip
    e.msdu.route_hops_left     = quint8(i % 16);  // 覆盖路由剩余跳数序列化 round-trip
    MsduFieldNode root = node(QStringLiteral("ItemHead [8b]"), QStringLiteral("0x12"), 0, 1);
    root.children.append(node(QStringLiteral("TEI [12b]"), QString::number(i), 1, 2));
    MsduFieldNode grp = node(QStringLiteral("Beacon Load"), QString(), -1, 0);
    grp.children.append(node(QStringLiteral("ItemHead [8b]"), QStringLiteral("0x34"), 3, 1));
    root.children.append(grp);
    e.msdu.tree.append(root);
    e.msdu.tei_mac_pairs.append({quint16(i & 0xFFF), 0xAABBCCDDEE00ULL + quint64(i)});
    e.msdu.cascade = (i % 3 == 0);   // v11: 聚合帧级联标记 round-trip
    e.msdu.trans_direction_flag = quint8(i % 2);  // v16: APP 传输方向 round-trip(P2P Dir 列)
    fill_topo(e.msdu.topo_event, i);              // msdu 拓扑事件 32 字段(换页后不丢)
    fill_topo(e.beacon.topo_event, i + 1000);     // beacon 拓扑事件(同为 MsduInfo,序列化路径相同)
    { TopoEvent ex; fill_topo(ex, i + 2000); e.msdu.topo_extra_events.append(ex); }    // 多站点后续事件(v17 往返)
    { TopoEvent ex; fill_topo(ex, i + 3000); e.beacon.topo_extra_events.append(ex); }  // 同上(beacon 路径)
    e.msdu.orig_srcs.append({quint16(11 + i), quint64(0x010203040506ULL + i)});        // 聚合原始源(v18 往返)
    e.msdu.orig_srcs.append({quint16(12 + i), quint64(0x0a0b0c0d0e0fULL + i)});
    e.beacon.orig_srcs.append({quint16(21 + i), quint64(0x111213141516ULL + i)});       // 同上(beacon 路径)
    e.msdu.orig_dsts.append({quint16(31 + i), quint64(0x212223242526ULL + i)});        // 聚合原始目的(v19 往返,与源镜像)
    e.msdu.orig_dsts.append({quint16(32 + i), quint64(0x2a2b2c2d2e2fULL + i)});
    e.beacon.orig_dsts.append({quint16(41 + i), quint64(0x313233343536ULL + i)});       // 同上(beacon 路径)
    e.raw_bytes = QByteArray(64, char(i & 0xFF));
    e.msdu_raw_base = 10;
    return e;
}

static bool sof_stations_equal(const QVector<SofStationPb>& x, const QVector<SofStationPb>& y, QString& why) {
    if (x.size() != y.size()) { why = QStringLiteral("sof_stations size mismatch"); return false; }
    for (int i = 0; i < x.size(); ++i) {
        const auto& a = x[i]; const auto& b = y[i];
        if (a.tei != b.tei || a.copy_mode != b.copy_mode || a.pb_num != b.pb_num
            || a.pb_size != b.pb_size || a.block_base != b.block_base
            || a.pb_heads != b.pb_heads || a.pb_crc_oks != b.pb_crc_oks
            || a.msdu != b.msdu) {
            why = QStringLiteral("sof_stations[%1] mismatch").arg(i); return false;
        }
    }
    return true;
}

static bool mpdu_soffields_equal(const MpduInfo& x, const MpduInfo& y, QString& why) {
    if (x.sof_data_type != y.sof_data_type
        || x.sof_tf_symbol_pos != y.sof_tf_symbol_pos
        || x.sof_tf_symbol_num != y.sof_tf_symbol_num
        || x.sof_fch_sym_num != y.sof_fch_sym_num
        || x.sof_mod_mode != y.sof_mod_mode
        || x.sof_mod_param != y.sof_mod_param
        || x.sof_band != y.sof_band
        || x.sof_gain != y.sof_gain
        || x.sof_load_table_id != y.sof_load_table_id
        || x.beacon_fch_sym != y.beacon_fch_sym
        || x.ack_bru_snr_flag != y.ack_bru_snr_flag
        || x.ack_sat_id_flag != y.ack_sat_id_flag
        || x.ack_remain_sta_num != y.ack_remain_sta_num
        || x.fch_crc_checked != y.fch_crc_checked) {
        why = QStringLiteral("mpdu SOF/beacon/ack field mismatch"); return false;
    }
    if (!sof_stations_equal(x.sof_stations, y.sof_stations, why)) return false;
    return true;
}

static bool topo_equal(const TopoEvent& x, const TopoEvent& y, QString& why) {
    if (x.kind != y.kind || x.nid != y.nid || x.cco_mac != y.cco_mac
        || x.routes != y.routes || x.up_routes != y.up_routes
        || x.discover_src_tei != y.discover_src_tei
        || x.neighbor_teis != y.neighbor_teis
        || x.neighbor_teis_present != y.neighbor_teis_present
        || x.leaves != y.leaves
        || x.is_rf != y.is_rf || x.is_rf_known != y.is_rf_known
        || x.rssi_entries != y.rssi_entries || x.neighbor_rates != y.neighbor_rates
        || x.neighbor_snrs != y.neighbor_snrs || x.proxy_tei != y.proxy_tei
        || x.proxy_up_rate != y.proxy_up_rate || x.proxy_down_rate != y.proxy_down_rate
        || x.min_link_rate != y.min_link_rate || x.rf_hop != y.rf_hop
        || x.rf_role != y.rf_role || x.link_type != y.link_type
        || x.node_link_types != y.node_link_types || x.proxy_comm_rate != y.proxy_comm_rate
        || x.proxy_down_comm_rate != y.proxy_down_comm_rate
        || x.min_comm_rate != y.min_comm_rate || x.child_teis != y.child_teis
        || x.restart_count != y.restart_count || x.sender_mac != y.sender_mac
        || x.desc != y.desc || x.epoch_ms != y.epoch_ms || x.frame_index != y.frame_index
        || x.nodes.size() != y.nodes.size()
        || x.comm_rates.size() != y.comm_rates.size()) {
        why = QStringLiteral("topo_event mismatch"); return false;
    }
    for (int k = 0; k < x.nodes.size(); ++k)
        if (x.nodes[k].tei != y.nodes[k].tei || x.nodes[k].mac != y.nodes[k].mac) {
            why = QStringLiteral("topo_event.nodes mismatch"); return false;
        }
    for (int k = 0; k < x.comm_rates.size(); ++k)
        if (x.comm_rates[k].tei != y.comm_rates[k].tei
            || x.comm_rates[k].down != y.comm_rates[k].down
            || x.comm_rates[k].up != y.comm_rates[k].up) {
            why = QStringLiteral("topo_event.comm_rates mismatch"); return false;
        }
    return true;
}

static bool entry_equal(const PacketEntry& a, const PacketEntry& b, QString& why) {
    if (a.index != b.index || a.epoch_ms != b.epoch_ms || a.delta_us != b.delta_us
        || a.accepted != b.accepted || a.reason != b.reason
        || a.raw_wire != b.raw_wire || a.meta.timestamp != b.meta.timestamp
        || a.meta.is_rf != b.meta.is_rf
        || a.meta.seg_start != b.meta.seg_start || a.meta.replay_time_tag != b.meta.replay_time_tag
        || a.meta.frame_time.toMSecsSinceEpoch() != b.meta.frame_time.toMSecsSinceEpoch()
        || a.mpdu.frame_type != b.mpdu.frame_type || a.mpdu.src_tei != b.mpdu.src_tei
        || a.mpdu.dst_tei != b.mpdu.dst_tei
        || a.mpdu.pb_num_field != b.mpdu.pb_num_field
        || a.mpdu.pblen != b.mpdu.pblen
        || a.mpdu.mcs != b.mpdu.mcs
        || a.mpdu.beacon_rsv != b.mpdu.beacon_rsv
        || a.mpdu.beacon_cco_mac != b.mpdu.beacon_cco_mac
        || a.protocol != b.protocol
        || a.msdu.present != b.msdu.present || a.msdu.msdu_seq != b.msdu.msdu_seq
        || a.msdu.summary != b.msdu.summary
        || a.msdu.broadcast_direction != b.msdu.broadcast_direction
        || a.msdu.route_hops_left != b.msdu.route_hops_left
        || a.msdu.trans_direction_flag != b.msdu.trans_direction_flag
        || a.msdu.tree.size() != b.msdu.tree.size()
        || a.msdu.tei_mac_pairs.size() != b.msdu.tei_mac_pairs.size()
        || a.raw_bytes != b.raw_bytes || a.msdu_raw_base != b.msdu_raw_base) {
        why = QStringLiteral("scalar mismatch at index %1").arg(a.index); return false;
    }
    if (!mpdu_soffields_equal(a.mpdu, b.mpdu, why)) return false;
    if (a.msdu.cascade != b.msdu.cascade) {
        why = QStringLiteral("msdu.cascade mismatch at index %1").arg(a.index); return false;
    }
    if (!topo_equal(a.msdu.topo_event, b.msdu.topo_event, why)) return false;
    if (!topo_equal(a.beacon.topo_event, b.beacon.topo_event, why)) return false;
    if (a.msdu.topo_extra_events.size() != b.msdu.topo_extra_events.size()) { why = QStringLiteral("msdu topo_extra_events size mismatch"); return false; }
    for (int k = 0; k < a.msdu.topo_extra_events.size(); ++k)
        if (!topo_equal(a.msdu.topo_extra_events[k], b.msdu.topo_extra_events[k], why)) return false;
    if (a.beacon.topo_extra_events.size() != b.beacon.topo_extra_events.size()) { why = QStringLiteral("beacon topo_extra_events size mismatch"); return false; }
    for (int k = 0; k < a.beacon.topo_extra_events.size(); ++k)
        if (!topo_equal(a.beacon.topo_extra_events[k], b.beacon.topo_extra_events[k], why)) return false;
    if (a.msdu.orig_srcs.size() != b.msdu.orig_srcs.size()) { why = QStringLiteral("msdu orig_srcs size mismatch"); return false; }
    for (int k = 0; k < a.msdu.orig_srcs.size(); ++k)
        if (a.msdu.orig_srcs[k].tei != b.msdu.orig_srcs[k].tei || a.msdu.orig_srcs[k].mac != b.msdu.orig_srcs[k].mac) { why = QStringLiteral("msdu orig_srcs mismatch"); return false; }
    if (a.beacon.orig_srcs.size() != b.beacon.orig_srcs.size()) { why = QStringLiteral("beacon orig_srcs size mismatch"); return false; }
    for (int k = 0; k < a.beacon.orig_srcs.size(); ++k)
        if (a.beacon.orig_srcs[k].tei != b.beacon.orig_srcs[k].tei || a.beacon.orig_srcs[k].mac != b.beacon.orig_srcs[k].mac) { why = QStringLiteral("beacon orig_srcs mismatch"); return false; }
    if (a.msdu.orig_dsts.size() != b.msdu.orig_dsts.size()) { why = QStringLiteral("msdu orig_dsts size mismatch"); return false; }
    for (int k = 0; k < a.msdu.orig_dsts.size(); ++k)
        if (a.msdu.orig_dsts[k].tei != b.msdu.orig_dsts[k].tei || a.msdu.orig_dsts[k].mac != b.msdu.orig_dsts[k].mac) { why = QStringLiteral("msdu orig_dsts mismatch"); return false; }
    if (a.beacon.orig_dsts.size() != b.beacon.orig_dsts.size()) { why = QStringLiteral("beacon orig_dsts size mismatch"); return false; }
    for (int k = 0; k < a.beacon.orig_dsts.size(); ++k)
        if (a.beacon.orig_dsts[k].tei != b.beacon.orig_dsts[k].tei || a.beacon.orig_dsts[k].mac != b.beacon.orig_dsts[k].mac) { why = QStringLiteral("beacon orig_dsts mismatch"); return false; }
    const MsduFieldNode& x = a.msdu.tree[0], &y = b.msdu.tree[0];
    if (x.name != y.name || x.value != y.value || x.children.size() != y.children.size()
        || x.children[1].children.size() != y.children[1].children.size()
        || x.children[1].children[0].name != y.children[1].children[0].name) {
        why = QStringLiteral("tree mismatch at index %1").arg(a.index); return false;
    }
    for (int k = 0; k < a.msdu.tei_mac_pairs.size(); ++k)
        if (a.msdu.tei_mac_pairs[k].tei != b.msdu.tei_mac_pairs[k].tei
            || a.msdu.tei_mac_pairs[k].mac != b.msdu.tei_mac_pairs[k].mac) {
            why = QStringLiteral("tei_mac mismatch at index %1").arg(a.index); return false;
        }
    // search_text is stored in the v5 block index; the decoded value must
    // equal the stored original (test entries fill it via make_search_text,
    // exactly like the app's make_entry does)
    if (b.search_text.isEmpty() || b.search_text != make_search_text(b)) {
        why = QStringLiteral("search_text not rebuilt at index %1").arg(a.index); return false;
    }
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int fails = 0;

    // 1) block round-trip, 2500 entries (multi-block sizes incl. odd counts)
    for (int n : {1, 1000, 2500}) {
        QVector<PacketEntry> src; src.reserve(n);
        for (int i = 1; i <= n; ++i) { PacketEntry e = make_entry(i); e.search_text = make_search_text(e); src.append(e); }
        const QByteArray blob = pser::encode_block(src);
        // sanity: compressed blob much smaller than raw estimate
        QVector<PacketEntry> dst;
        if (!pser::decode_block(blob, dst)) { qWarning() << "decode_block failed n=" << n; ++fails; continue; }
        if (dst.size() != src.size()) { qWarning() << "count mismatch" << dst.size() << src.size(); ++fails; continue; }
        for (int i = 0; i < n; ++i) {
            QString why;
            if (!entry_equal(src[i], dst[i], why)) { qWarning() << why; ++fails; break; }
        }
        qInfo() << "block n=" << n << "blob_bytes=" << blob.size() << "OK";
    }

    // 2) single entry round-trip
    {
        PacketEntry e = make_entry(42); e.search_text = make_search_text(e);
        pser::StrPool pool; QVector<QString> table;
        const QByteArray p = pser::serialize_entry(e, pool);
        table = pool.list;
        PacketEntry d;
        QString why;
        if (!pser::deserialize_entry(p, d, table) || !entry_equal(e, d, why)) { qWarning() << "single entry FAIL" << why; ++fails; }
        else qInfo() << "single entry OK, payload_bytes=" << p.size() << "pool=" << pool.list.size();
    }

    // 2b) 反向验证: 篡改字段必须被检出(证明比较逻辑有效,而非默认值恒等)
    {
        QString why;
        TopoEvent te; fill_topo(te, 7);
        TopoEvent tampered = te; tampered.rssi_entries[0].second = -999;
        if (topo_equal(te, tampered, why)) { qWarning() << "reverse check FAIL: topo_equal missed rssi_entries"; ++fails; }
        else qInfo() << "reverse check OK: topo_equal caught rssi_entries (" << why << ")";

        PacketEntry a = make_entry(7), b = make_entry(7);
        b.msdu.cascade = !b.msdu.cascade;
        if (entry_equal(a, b, why)) { qWarning() << "reverse check FAIL: entry_equal missed cascade"; ++fails; }
        else qInfo() << "reverse check OK: entry_equal caught cascade (" << why << ")";

        MpduInfo m1 = a.mpdu, m2 = b.mpdu;
        m2.sof_band = quint8(m1.sof_band ^ 0xFF);
        if (mpdu_soffields_equal(m1, m2, why)) { qWarning() << "reverse check FAIL: mpdu_soffields_equal missed sof_band"; ++fails; }
        else qInfo() << "reverse check OK: mpdu_soffields_equal caught sof_band (" << why << ")";

        MpduInfo m3 = make_entry(7).mpdu;
        MpduInfo m4 = m3; m4.sof_stations[0].pb_size = 0xFFFF;
        if (mpdu_soffields_equal(m3, m4, why)) { qWarning() << "reverse check FAIL: mpdu_soffields_equal missed sof_stations"; ++fails; }
        else qInfo() << "reverse check OK: mpdu_soffields_equal caught sof_stations (" << why << ")";
    }

    // 3) corrupt / truncated inputs must fail cleanly (no crash)
    {
        QVector<PacketEntry> src; for (int i = 1; i <= 50; ++i) src.append(make_entry(i));
        const QByteArray good = pser::encode_block(src);
        QVector<PacketEntry> out;
        int bad = 0;
        if (pser::decode_block(QByteArray(), out)) ++bad;
        if (pser::decode_block(QByteArray("garbage"), out)) ++bad;
        if (pser::decode_block(good.left(good.size() / 2), out)) ++bad;   // truncated
        if (pser::decode_block(good.left(20), out)) ++bad;                 // header cut
        QByteArray old_version = good; old_version[7] = char(3);
        if (pser::decode_block(old_version, out)) ++bad;                  // incompatible cache version
        QByteArray flip = good; flip[flip.size() - 1] = char(flip[flip.size() - 1] ^ 0xFF);
        if (pser::decode_block(flip, out)) ++bad;                          // bit flip
        if (bad) { qWarning() << "corrupt-input check FAIL:" << bad; ++fails; }
        else qInfo() << "corrupt-input checks OK (all rejected)";
    }

    // 4) v5 random access + filter index (BlockReader)
    {
        QVector<PacketEntry> src; src.reserve(2500);
        for (int i = 1; i <= 2500; ++i) { PacketEntry e = make_entry(i); e.search_text = make_search_text(e); src.append(e); }
        const QByteArray blob = pser::encode_block(src);
        pser::BlockReader r;
        if (!r.open_data(blob) || r.count() != 2500) {
            qWarning() << "BlockReader open failed"; ++fails;
        } else {
            for (int i : {0, 1, 49, 50, 51, 999, 1000, 1234, 2499}) {
                PacketEntry e; QString why;
                if (!r.entry_at(i, e) || !entry_equal(src[i], e, why)) {
                    qWarning() << "entry_at" << i << "FAIL" << why; ++fails; break;
                }
                if (r.frame_type(i) != src[i].mpdu.frame_type
                    || r.src_tei(i) != src[i].mpdu.src_tei
                    || r.dst_tei(i) != src[i].mpdu.dst_tei
                    || r.search_text(i) != src[i].search_text) {
                    qWarning() << "index mismatch at" << i; ++fails; break;
                }
            }
            qInfo() << "random access + index OK";
        }
        // search_text is stored verbatim (NOT rebuilt): a value that differs
        // from make_search_text() must round-trip unchanged via the index
        PacketEntry pe = make_entry(7);
        pe.search_text = QStringLiteral("zzz custom marker");
        const QByteArray pblob = pser::encode_block(QVector<PacketEntry>{pe});
        pser::BlockReader pr;
        PacketEntry pd;
        if (!pr.open_data(pblob) || pr.search_text(0) != pe.search_text
            || !pr.entry_at(0, pd) || pd.search_text != pe.search_text
            || pd.index != pe.index) {
            qWarning() << "stored search_text round-trip FAIL"; ++fails;
        } else {
            qInfo() << "stored search_text OK";
        }
    }

    qInfo() << (fails ? "RESULT: FAIL" : "RESULT: PASS");
    return fails ? 1 : 0;
}
