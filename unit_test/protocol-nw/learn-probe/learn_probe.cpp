// learn_probe: NW_2026 TEI-MAC learning audit probe.
// Feeds frames through the production NW_2026_Parser and reports, per frame,
// every TEI-MAC pair that PacketListModel::update_tei_mac would learn
// (same guards: pair tei!=0 && mac; header tei>0 && mac), plus frame class
// signals (beacon/cascade/sof_stations/mme_type/topo kind).
// Modes:
//   scan <mpdus.bin>   : [u32 LE len][mpdu] records (TOPO capture layout)
//   files <mpdu...>    : bare MPDU files in order, one parser (trigger ctx kept)
#include <QCoreApplication>
#include <QFile>
#include <QDataStream>
#include <cstdio>
#include <cstring>
#include "bplcframe.h"
#include "nw_2026_parser.h"

static QString mac_hex(quint64 mac) {
    // frame byte order: low byte first (same u64 convention as the app)
    QString s;
    for (int i = 0; i < 6; ++i) {
        if (i) s += QLatin1Char(':');
        s += QStringLiteral("%1").arg((mac >> (8 * i)) & 0xFF, 2, 16, QLatin1Char('0'));
    }
    return s;
}

template <typename R>
static void report_frame(long long idx, const R& r) {
    const MpduInfo& m = r.mpdu;
    const MsduInfo& s = r.msdu;
    const MsduInfo& b = r.beacon;
    // Learned pairs, mirroring update_tei_mac guards exactly.
    int n_learn = 0;
    auto emit_pair = [&](const char* cat, quint16 tei, quint64 mac) {
        std::printf("PAIR idx=%lld cat=%s tei=%u mac=%s\n", idx, cat, (unsigned)tei,
                    qPrintable(mac_hex(mac)));
        ++n_learn;
    };
    for (const TeiMacPair& p : b.tei_mac_pairs)
        if (p.tei != 0 && p.mac) emit_pair("beacon", p.tei, p.mac);
    for (const TeiMacPair& p : s.tei_mac_pairs)
        if (p.tei != 0 && p.mac) emit_pair("msdu", p.tei, p.mac);
    if (s.present && !s.simple_head) {
        if (s.msdu_src_tei > 0 && s.msdu_src_mac)
            emit_pair("src", (quint16)s.msdu_src_tei, s.msdu_src_mac);
        if (s.msdu_dst_tei > 0 && s.msdu_dst_mac)
            emit_pair("dst", (quint16)s.msdu_dst_tei, s.msdu_dst_mac);
    }
    // Class line for interesting frames only (keeps full-scan output small).
    const bool interesting = b.present || s.cascade || !m.sof_stations.isEmpty()
        || s.mme_type == 0x0084 || s.mme_type == 0x003A
        || s.topo_event.kind != TopoEventKind::Other || n_learn > 0;
    if (interesting) {
        std::printf("FRAME idx=%lld accept=%d ftype=%d ver=%d beacon=%d b_pairs=%d "
                    "msdu_pairs=%d src=%d dst=%d mme=0x%04x cascade=%d stations=%d "
                    "topo=%d learn=%d summary='%s'\n",
                    idx, (int)r.accept, m.frame_type, m.version, (int)b.present,
                    (int)b.tei_mac_pairs.size(), (int)s.tei_mac_pairs.size(),
                    s.msdu_src_tei, s.msdu_dst_tei, (unsigned)s.mme_type,
                    (int)s.cascade, (int)m.sof_stations.size(),
                    (int)s.topo_event.kind, n_learn, qPrintable(s.summary));
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: learn_probe scan <mpdus.bin> | files <mpdu...>\n");
        return 2;
    }
    NW_2026_Parser parser;
    MsduState st;
    ParseFilter pf;
    long long idx = 0;
    if (std::strcmp(argv[1], "scan") == 0) {
        QFile f(QString::fromLocal8Bit(argv[2]));
        if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot open\n"); return 1; }
        QDataStream ds(&f); ds.setByteOrder(QDataStream::LittleEndian);
        while (!ds.atEnd()) {
            quint32 len = 0; ds >> len;
            QByteArray mpdu = f.read(len);
            if (mpdu.size() != (int)len) break;
            ++idx;
            BplcFrame in;
            in.data = QByteArray(1, char(0)) + mpdu;
            in.meta.from_raw = true;
            report_frame(idx, parser.parse(in, st, pf));
        }
    } else {
        for (int a = 2; a < argc; ++a) {
            QFile f(QString::fromLocal8Bit(argv[a]));
            if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot open %s\n", argv[a]); return 1; }
            QByteArray mpdu = f.readAll();
            ++idx;
            BplcFrame in;
            in.data = QByteArray(1, char(0)) + mpdu;
            in.meta.from_raw = true;
            report_frame(idx, parser.parse(in, st, pf));
        }
    }
    std::fprintf(stderr, "scanned %lld frames\n", idx);
    return 0;
}
