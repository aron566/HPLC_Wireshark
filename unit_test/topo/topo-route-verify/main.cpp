// TOPO route-log verification probe: feed every MPDU of the capture to the
// production NW_2026_Parser (48e180e tree) in from_raw mode, one MsduState
// carried across frames like the app, and dump one line per frame.
#include <QCoreApplication>
#include <QFile>
#include <QDataStream>
#include <cstdio>
#include "bplcframe.h"
#include "nw_2026_parser.h"

static QString mac_str(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        if (i) s += ':';
        s += QStringLiteral("%1").arg((v >> (8*i)) & 0xFF, 2, 16, QChar('0')).toUpper();
    }
    return s;
}
static const char* kind_name(int k) {
    static const char* n[] = {"DiscoverList","AssocReq","AssocCnf","AssocGatherInd","AssocInd",
        "ChangeProxyReq","ChangeProxyCnf","LeaveInd","SuccessRate","CcoRestart","StaRestart","Other"};
    return (k >= 0 && k < 12) ? n[k] : "?";
}
static void dump_tree(const QVector<MsduFieldNode>& nodes, int depth, FILE* f) {
    for (const auto& nd : nodes) {
        for (int i = 0; i < depth; ++i) std::fprintf(f, "  ");
        std::fprintf(f, "%s = %s\n", qPrintable(nd.name), qPrintable(nd.value));
        if (!nd.children.isEmpty()) dump_tree(nd.children, depth + 1, f);
    }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::fprintf(stderr, "usage: topo_scan <mpdus.bin> [verbose_idx]\n"); return 2; }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    const qint64 verbose = (argc >= 3) ? QString(argv[2]).toLongLong() : -1;
    QDataStream ds(&f); ds.setByteOrder(QDataStream::LittleEndian);
    NW_2026_Parser parser;
    MsduState st;
    ParseFilter pf;
    qint64 idx = 0;
    while (!ds.atEnd()) {
        quint32 len = 0; ds >> len;
        QByteArray mpdu = f.read(len);
        if (mpdu.size() != (int)len) break;
        ++idx;
        BplcFrame in;
        in.data = QByteArray(1, char(0)) + mpdu;  // isRF = 0 (carrier)
        in.meta.from_raw = true;
        auto r = parser.parse(in, st, pf);
        const MpduInfo& m = r.mpdu;
        const MsduInfo& s = r.msdu;
        const TopoEvent& t = s.topo_event;
        QString nodes;
        for (const auto& p : t.nodes) {
            if (!nodes.isEmpty()) nodes += ",";
            nodes += QStringLiteral("%1:%2").arg(p.tei).arg(mac_str(p.mac));
        }
        std::printf("%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t0x%04x\t0x%04x\t%s\t%s\t%d\t%s\n",
            (long long)idx, (int)r.accept, m.src_tei, m.dst_tei, m.frame_len, m.pb_num, m.pb_size,
            (int)m.pb_crc_ok, (int)s.present, s.msdu_src_tei, s.msdu_dst_tei, (int)s.restart_count,
            (int)s.route_hops_left, (int)s.msdu_send_type, s.msdu_type, s.mme_type,
            kind_name((int)t.kind), qPrintable(nodes), t.restart_count, qPrintable(t.desc));
        if (idx == verbose) {
            std::fprintf(stderr, "=== frame %lld mpdu_len=%d accept=%d reject='%s' fch_ok=%d summary='%s'\n",
                (long long)idx, (int)mpdu.size(), (int)r.accept, qPrintable(r.reject_reason),
                (int)m.fch_crc_ok, qPrintable(s.summary));
            dump_tree(s.tree, 0, stderr);
        }
    }
    std::fprintf(stderr, "scanned %lld frames\n", (long long)idx);
    return 0;
}
