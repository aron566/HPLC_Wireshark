#include "packetentry_serialize.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <cstdio>

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
    e.mpdu.frame_type = quint8(i % 4); e.mpdu.src_tei = quint16(100 + (i % 50));
    e.mpdu.dst_tei = (i % 5 == 0) ? quint16(0xFFF) : quint16(200 + (i % 30));
    e.mpdu.beacon_cco_mac = 0x112233445566ULL;
    e.msdu.present = true; e.msdu.msdu_seq = quint16(i);
    e.msdu.summary = QStringLiteral("summary-%1").arg(i);
    MsduFieldNode root = node(QStringLiteral("ItemHead [8b]"), QStringLiteral("0x12"), 0, 1);
    root.children.append(node(QStringLiteral("TEI [12b]"), QString::number(i), 1, 2));
    e.msdu.tree.append(root);
    e.msdu.tei_mac_pairs.append({quint16(i & 0xFFF), 0xAABBCCDDEE00ULL + quint64(i)});
    e.raw_bytes = QByteArray(64, char(i & 0xFF));
    e.msdu_raw_base = 10;
    e.search_text = make_search_text(e);
    return e;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    for (int n : {1, 57, 250, 1000}) {
        QVector<PacketEntry> es;
        for (int i = 0; i < n; ++i) es.append(make_entry(i));
        const QByteArray blob = pser::encode_block(es);
        const QString fn = QStringLiteral("v3_n%1.bin").arg(n);
        QFile f(fn); f.open(QIODevice::WriteOnly); f.write(blob); f.close();
        std::printf("n=%d bytes=%d sha256=%s\n", n, blob.size(),
                    QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex().constData());
    }
    return 0;
}
