// A/B stats probe: how does the devin truncation branch treat real NW frames?
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QMap>
#include <cstdio>
#include "serialreader.h"
#include "nw_2021_parser.h"
#include "bplcframe.h"

static bool tree_has_crc_value(const QVector<MsduFieldNode>& tree, const QString& suffix) {
    for (const auto& n : tree) {
        if (n.name == QStringLiteral("MSDU CRC32") && n.value.endsWith(suffix)) return true;
        if (tree_has_crc_value(n.children, suffix)) return true;
    }
    return false;
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
    long total = 0, accepted = 0, msdu_present = 0, mme_frames = 0;
    long trunc_marked = 0, unsup_ver = 0, crc_missing = 0, crc_fail = 0, crc_ok = 0;
    QMap<quint16, long> trunc_by_type, mme_by_type;

    QObject::connect(&reader, &SerialReader::frame_ready, [&](const BplcFrame& f) {
        auto r = parser.parse(f, state, filt);
        ++total;
        if (!r.accept) return;
        ++accepted;
        if (!r.msdu.present) return;
        ++msdu_present;
        if (tree_has_crc_value(r.msdu.tree, QStringLiteral("FAIL"))) ++crc_fail;
        if (tree_has_crc_value(r.msdu.tree, QStringLiteral("OK"))) ++crc_ok;
        if (r.msdu.summary.contains(QStringLiteral("CRC missing"))) ++crc_missing;
        if (r.msdu.mme_type == 0xFFFF) return;
        ++mme_frames;
        mme_by_type[r.msdu.mme_type]++;
        if (r.msdu.summary.contains(QStringLiteral("unsupported MMe version"))) ++unsup_ver;
        if (r.msdu.summary.contains(QStringLiteral("(truncated)"))) {
            ++trunc_marked;
            trunc_by_type[r.msdu.mme_type]++;
        }
    });
    QEventLoop loop;
    QObject::connect(&reader, &SerialReader::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(300000, &loop, &QEventLoop::quit);
    ReaderConfig cfg;
    cfg.mode = ReaderMode::FilePlayback;
    cfg.file_path = path;
    reader.start(cfg);
    loop.exec();
    reader.stop();
    printf("=== %s\ntotal %ld accepted %ld msdu_present %ld mme_frames %ld\n",
           qPrintable(path), total, accepted, msdu_present, mme_frames);
    printf("truncated_marked %ld  unsupported_version %ld  crc_missing %ld  crc_ok %ld crc_fail %ld\n",
           trunc_marked, unsup_ver, crc_missing, crc_ok, crc_fail);
    for (auto it = trunc_by_type.begin(); it != trunc_by_type.end(); ++it)
        printf("  truncated type 0x%04X: %ld of %ld\n", it.key(), it.value(), mme_by_type[it.key()]);
    return 0;
}
