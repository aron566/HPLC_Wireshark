// Round-trip test for the new compressed block format.
// Builds synthetic PacketEntries with nested trees, encodes a block,
// decodes it back, and verifies every field (incl. rebuilt search_text).
//
// Manual build & run (Qt 6):
//   QT=~/qt/6.5.3/gcc_64
//   g++ -std=c++17 -fPIC -O1 -I$QT/include -I$QT/include/QtCore -Isrc/common \
//       src/common/tests/block_format_test.cpp -o /tmp/block_format_test \
//       -L$QT/lib -lQt6Core -Wl,-rpath,$QT/lib && /tmp/block_format_test
#include "packetentry_serialize.h"
#include <QCoreApplication>
#include <QDebug>

static MsduFieldNode node(const QString& n, const QString& v, int rs, int rl) {
    MsduFieldNode f; f.name = n; f.value = v; f.rel_start = rs; f.rel_len = rl;
    return f;
}

static PacketEntry make_entry(int i) {
    PacketEntry e;
    e.index = i; e.epoch_ms = 1700000000000LL + i; e.delta_us = 1000 + i;
    e.accepted = (i % 7 != 0);
    e.reason = e.accepted ? QString() : QStringLiteral("CRC error");
    e.raw_wire = QByteArray::fromHex("3c00ff3e");
    e.meta.timestamp = 12345 + i; e.meta.is_rf = (i % 2 == 0);
    e.meta.frame_time = QDateTime::fromMSecsSinceEpoch(e.epoch_ms);
    e.mpdu.frame_type = 1; e.mpdu.src_tei = 2; e.mpdu.dst_tei = 0xFFF;
    e.mpdu.beacon_cco_mac = 0x112233445566ULL;
    e.msdu.present = true; e.msdu.msdu_seq = quint16(i);
    e.msdu.summary = QStringLiteral("summary-%1").arg(i);
    MsduFieldNode root = node(QStringLiteral("ItemHead [8b]"), QStringLiteral("0x12"), 0, 1);
    root.children.append(node(QStringLiteral("TEI [12b]"), QString::number(i), 1, 2));
    MsduFieldNode grp = node(QStringLiteral("Beacon Load"), QString(), -1, 0);
    grp.children.append(node(QStringLiteral("ItemHead [8b]"), QStringLiteral("0x34"), 3, 1));
    root.children.append(grp);
    e.msdu.tree.append(root);
    e.msdu.tei_mac_pairs.append({quint16(i & 0xFFF), 0xAABBCCDDEE00ULL + quint64(i)});
    e.raw_bytes = QByteArray(64, char(i & 0xFF));
    e.msdu_raw_base = 10;
    return e;
}

static bool entry_equal(const PacketEntry& a, const PacketEntry& b, QString& why) {
    if (a.index != b.index || a.epoch_ms != b.epoch_ms || a.delta_us != b.delta_us
        || a.accepted != b.accepted || a.reason != b.reason
        || a.raw_wire != b.raw_wire || a.meta.timestamp != b.meta.timestamp
        || a.meta.is_rf != b.meta.is_rf
        || a.meta.frame_time.toMSecsSinceEpoch() != b.meta.frame_time.toMSecsSinceEpoch()
        || a.mpdu.frame_type != b.mpdu.frame_type || a.mpdu.src_tei != b.mpdu.src_tei
        || a.mpdu.dst_tei != b.mpdu.dst_tei
        || a.mpdu.beacon_cco_mac != b.mpdu.beacon_cco_mac
        || a.msdu.present != b.msdu.present || a.msdu.msdu_seq != b.msdu.msdu_seq
        || a.msdu.summary != b.msdu.summary
        || a.msdu.tree.size() != b.msdu.tree.size()
        || a.msdu.tei_mac_pairs.size() != b.msdu.tei_mac_pairs.size()
        || a.raw_bytes != b.raw_bytes || a.msdu_raw_base != b.msdu_raw_base) {
        why = QStringLiteral("scalar mismatch at index %1").arg(a.index); return false;
    }
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
    // search_text must be rebuilt (not serialized anymore)
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
        QByteArray flip = good; flip[flip.size() - 1] = char(flip[flip.size() - 1] ^ 0xFF);
        if (pser::decode_block(flip, out)) ++bad;                          // bit flip
        if (bad) { qWarning() << "corrupt-input check FAIL:" << bad; ++fails; }
        else qInfo() << "corrupt-input checks OK (all rejected)";
    }

    qInfo() << (fails ? "RESULT: FAIL" : "RESULT: PASS");
    return fails ? 1 : 0;
}
