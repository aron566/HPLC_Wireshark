// NW_2026 single-frame test probe (4396cd6 tree): production parser + display
// tree builder (SOF section incl. Link ID gating) on one user-supplied frame.
#include <QApplication>
#include <QTreeWidgetItem>
#include <QFile>
#include <QDataStream>
#include <cstdio>
#include "bplcframe.h"
#include "nw_2026_parser.h"
#include "nw_2026_tree.h"

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
static void dump_fields(const QVector<MsduFieldNode>& nodes, int depth, FILE* f) {
    for (const auto& nd : nodes) {
        for (int i = 0; i < depth; ++i) std::fprintf(f, "  ");
        std::fprintf(f, "%s = %s\n", qPrintable(nd.name), qPrintable(nd.value));
        if (!nd.children.isEmpty()) dump_fields(nd.children, depth + 1, f);
    }
}
static void dump_items(QTreeWidgetItem* it, int depth, FILE* f) {
    for (int i = 0; i < it->childCount(); ++i) {
        QTreeWidgetItem* c = it->child(i);
        for (int d = 0; d < depth; ++d) std::fprintf(f, "  ");
        std::fprintf(f, "[%s]", qPrintable(c->text(0)));
        for (int col = 1; col < c->columnCount(); ++col) {
            QString t = c->text(col);
            if (!t.isEmpty()) std::fprintf(f, " | %s", qPrintable(t));
        }
        std::fprintf(f, "\n");
        dump_items(c, depth + 1, f);
    }
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 3) { std::fprintf(stderr, "usage: frame_test <container> <isRF 0|1>\n"); return 2; }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    const int isrf = QString(argv[2]).toInt();
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
        in.data = QByteArray(1, char(isrf ? 1 : 0)) + mpdu;
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
        std::printf("frame %lld accept=%d fch_ok=%d ver=%d ftype=%d nid=0x%x src=%d dst=%d link_id=%d frame_len=%d pb_num=%d pb_size=%d pb_crc_ok=%d msdu_present=%d msdu_src=%d msdu_dst=%d restart=%d hops=%d send_type=%d msdu_type=0x%04x mme_type=0x%04x is_rf=%d topo=%s nodes=[%s] topo_restart=%d desc='%s' summary='%s' reject='%s'\n",
            (long long)idx, (int)r.accept, (int)m.fch_crc_ok, m.version, m.frame_type, m.net_id,
            m.src_tei, m.dst_tei, m.link_id, m.frame_len, m.pb_num, m.pb_size, (int)m.pb_crc_ok,
            (int)s.present, s.msdu_src_tei, s.msdu_dst_tei, (int)s.restart_count,
            (int)s.route_hops_left, (int)s.msdu_send_type, s.msdu_type, s.mme_type,
            (int)r.meta.is_rf, kind_name((int)t.kind), qPrintable(nodes), t.restart_count,
            qPrintable(t.desc), qPrintable(s.summary), qPrintable(r.reject_reason));
        std::fprintf(stderr, "=== parser field tree (frame %lld) ===\n", (long long)idx);
        dump_fields(s.tree, 0, stderr);
        // Display tree (what the UI field pane shows), SOF section included.
        PacketEntry e;
        e.mpdu = r.mpdu;
        e.msdu = r.msdu;
        e.msdu_body = r.msdu_body;
        e.msdu_raw_base = r.msdu_raw_base;
        e.raw_bytes = r.payload_for_log;
        e.meta.is_rf = r.meta.is_rf;
        e.protocol = kProtocolNw2026;
        QTreeWidgetItem* root = new QTreeWidgetItem();
        NW_2026_TreeBuilder builder;
        builder.build(root, e);
        std::fprintf(stderr, "=== display tree (frame %lld) ===\n", (long long)idx);
        dump_items(root, 0, stderr);
        delete root;
    }
    return 0;
}
