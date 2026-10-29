// Probe: verify MMe header tree nodes (spec table 41) on real NW capture.
// Usage: probe_mme <file.bin>  (counts + full tree dump of first 2 MMe SOF frames)
#include <QApplication>
#include <QTreeWidget>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include "serialreader.h"
#include "nw_2021_parser.h"
#include "nw_2021_tree.h"
#include "bplcframe.h"

static void dump_item(QTreeWidgetItem* it, int depth) {
    printf("%*s%s | %s\n", depth * 2, "", qPrintable(it->text(0)), qPrintable(it->text(1)));
    for (int i = 0; i < it->childCount(); ++i) dump_item(it->child(i), depth + 1);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    qRegisterMetaType<BplcFrame>("BplcFrame");
    qRegisterMetaType<ReaderConfig>("ReaderConfig");

    const QString path = QString::fromLocal8Bit(argv[1]);
    SerialReader reader;
    NW_2021_Parser parser;
    MsduState state;
    NW_2021_Parser::Filter filt;
    int total = 0, accepted = 0, mme_frames = 0, dumped = 0;

    QObject::connect(&reader, &SerialReader::frame_ready, [&](const BplcFrame& f) {
        auto r = parser.parse(f, state, filt);
        ++total;
        if (!r.accept) return;
        ++accepted;
        if (!r.msdu.present || r.msdu.mme_type != 0x0031) return;
        ++mme_frames;
        if (dumped >= 3) return;
        ++dumped;
        PacketEntry e;
        e.accepted = true;
        e.meta = r.meta;
        e.mpdu = r.mpdu;
        e.raw_bytes = r.payload_for_log;
        e.msdu_body = r.msdu_body;
        e.msdu = r.msdu;
        e.beacon = r.beacon;
        e.msdu_raw_base = r.msdu_raw_base;
        QTreeWidget w;
        auto* root = new QTreeWidgetItem(&w);
        NW_2021_TreeBuilder tb;
        tb.build(root, e);
        printf("--- MMe frame #%d (mme_type=0x%04X, summary=%s) ---\n",
               mme_frames, r.msdu.mme_type, qPrintable(r.msdu.summary));
        for (int i = 0; i < root->childCount(); ++i) dump_item(root->child(i), 0);
    });
    QEventLoop loop;
    QObject::connect(&reader, &SerialReader::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    ReaderConfig cfg;
    cfg.mode = ReaderMode::FilePlayback;
    cfg.file_path = path;
    reader.start(cfg);
    loop.exec();
    reader.stop();
    printf("=== %s: total %d, accepted %d, mme_frames %d\n",
           qPrintable(path), total, accepted, mme_frames);
    return 0;
}
