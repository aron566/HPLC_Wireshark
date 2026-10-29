// Audit probe: classify every accepted frame in a capture and dump the full
// protocol tree of the first frame of each class, for spec comparison.
// Usage: probe_audit <file.bin> [...]
#include <QApplication>
#include <QTreeWidget>
#include <QEventLoop>
#include <QTimer>
#include <QMap>
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

    for (int ai = 1; ai < argc; ++ai) {
        const QString path = QString::fromLocal8Bit(argv[ai]);
        SerialReader reader;
        NW_2021_Parser parser;
        MsduState state;
        NW_2021_Parser::Filter filt;
        QMap<QString, int> counts;
        QMap<QString, bool> dumped;
        int total = 0, accepted = 0;

        QObject::connect(&reader, &SerialReader::frame_ready, [&](const BplcFrame& f) {
            auto r = parser.parse(f, state, filt);
            ++total;
            if (!r.accept) return;
            ++accepted;
            QString cls = QStringLiteral("ft%1").arg(r.mpdu.frame_type);
            cls += r.meta.is_rf ? QStringLiteral("/RF") : QStringLiteral("/PLC");
            if (r.beacon.present) cls += QStringLiteral("/beacon");
            else if (r.msdu.present) {
                cls += QStringLiteral("/msdu");
                if (r.msdu.simple_head) cls += QStringLiteral("-baseS");
                else if (r.msdu.mme_type != 0xFFFF)
                    cls += QStringLiteral("-mme%1").arg(r.msdu.mme_type, 4, 16, QChar('0'));
                else cls += QStringLiteral("-app");
            }
            counts[cls]++;
            if (!dumped.value(cls)) {
                dumped[cls] = true;
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
                printf("########## class %s (first frame, %s) ##########\n",
                       qPrintable(cls), qPrintable(path));
                dump_item(root, 0);
            }
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
        printf("=== %s: total %d accepted %d; classes:", qPrintable(path), total, accepted);
        for (auto it = counts.begin(); it != counts.end(); ++it)
            printf(" %s=%d", qPrintable(it.key()), it.value());
        printf("\n");
    }
    return 0;
}
