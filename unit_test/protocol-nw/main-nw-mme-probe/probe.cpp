// Probe: main-branch NW parser + tree builder over real captures via SerialReader.
// Usage: probe <file.bin> [...]  (per file: counts + first RF SOF / first PLC SOF tree dump)
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

static QTreeWidgetItem* find_item(QTreeWidgetItem* it, const QString& name) {
    if (it->text(0) == name) return it;
    for (int i = 0; i < it->childCount(); ++i)
        if (auto* r = find_item(it->child(i), name)) return r;
    return nullptr;
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
        int total = 0, accepted = 0, rf = 0, rf_sof = 0;
        bool dumped_rf = false, dumped_plc = false;

        QObject::connect(&reader, &SerialReader::status_message,
                         [](const QString& s) { fprintf(stderr, "[status] %s\n", qPrintable(s)); });
        QObject::connect(&reader, &SerialReader::error_occurred,
                         [](const QString& s) { fprintf(stderr, "[error] %s\n", qPrintable(s)); });
        QObject::connect(&reader, &SerialReader::frame_ready, [&](const BplcFrame& f) {
            auto r = parser.parse(f, state, filt);
            ++total;
            if (!r.accept) return;
            ++accepted;
            if (r.meta.is_rf) ++rf;
            const bool is_sof = (r.mpdu.frame_type == 1);
            if (!is_sof) return;
            if (r.meta.is_rf) ++rf_sof;
            if ((r.meta.is_rf && !dumped_rf) || (!r.meta.is_rf && !dumped_plc)) {
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
                printf("--- %s SOF tree (%s) ---\n", r.meta.is_rf ? "RF" : "PLC",
                       qPrintable(path));
                printf("scalars: frame_len=%d tmi=%d pb_num=%d pb_size=%d re_send=%d "
                       "symbol_num=%d tmi_ext=%d msdu_present=%d\n",
                       r.mpdu.frame_len, r.mpdu.tmi, r.mpdu.pb_num, r.mpdu.pb_size,
                       r.mpdu.re_send_flag ? 1 : 0, r.mpdu.symbol_num, r.mpdu.tmi_ext,
                       r.msdu.present ? 1 : 0);
                if (auto* sof = find_item(root, QStringLiteral("SOF"))) {
                    printf("SOF | %s\n", qPrintable(sof->text(1)));
                    for (int i = 0; i < sof->childCount(); ++i) dump_item(sof->child(i), 1);
                } else {
                    printf("(no SOF item found)\n");
                }
                if (r.meta.is_rf) dumped_rf = true; else dumped_plc = true;
            }
        });
        QEventLoop loop;
        QObject::connect(&reader, &SerialReader::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(120000, &loop, &QEventLoop::quit);
        ReaderConfig cfg;
        cfg.mode = ReaderMode::FilePlayback;
        cfg.file_path = path;
        fprintf(stderr, "[probe] starting %s\n", qPrintable(path));
        reader.start(cfg);
        loop.exec();
        fprintf(stderr, "[probe] loop done for %s\n", qPrintable(path));
        reader.stop();
        printf("=== %s: total %d, accepted %d, rf accepted %d, rf SOF %d\n",
               qPrintable(path), total, accepted, rf, rf_sof);
    }
    return 0;
}
