/// @file dissect_compare_test.cpp
/// @brief 解剖器一致性对比测试:同一批真实帧分别走内置解析器与参考解剖器
///        插件,逐帧比对标量结果,并把两侧经 ProtocolTree 渲染的字段树
///        拍平逐行比对(名/值/高亮区间),验证新架构"效果一致"。
/// @details 用法:
///   dissect_compare_test --bin <capture.bin> --plugin-so <dissector.so>
///                        --variant <gw|nw> [--max N] [--tree-max N]
/// 帧来源与主程序相同(SerialReader 文件回放),保证 BplcFrame 与线上一致。
/// 退出码:0=全一致;1=有差异;2=参数/加载错误。
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPluginLoader>
#include <QTextStream>
#include <QTimer>

#include "gw_2022_parser.h"
#include "idissectorplugin.h"
#include "iplugin.h"
#include "nw_2021_parser.h"
#include "protocoltree.h"
#include "protocol_tree_builder.h"
#include "serialreader.h"

namespace {

QTextStream out(stdout);

QStringList flatten(QTreeWidgetItem* it, int depth) {
    QStringList lines;
    int start = it->data(0, kRoleStart).toInt();
    const int len = it->data(0, kRoleLen).toInt();
    // "无区间"表示归一:内置构建器写 0+0、通用字段写 -1+0,len=0 时
    // 高亮效果相同(不亮任何字节),视为等价
    if (len == 0 && start < 0) start = 0;
    lines << QStringLiteral("%1%2 | %3 | @%4+%5")
                 .arg(QString(depth * 2, QLatin1Char(' ')), it->text(0),
                      it->text(1))
                 .arg(start)
                 .arg(len);
    for (int i = 0; i < it->childCount(); ++i)
        lines += flatten(it->child(i), depth + 1);
    return lines;
}

PacketEntry entry_of(const ParseResult& r, const BplcFrame& f) {
    PacketEntry e;
    e.accepted = r.accept;
    e.reason = r.reject_reason;
    e.meta = r.meta;
    e.mpdu = r.mpdu;
    e.msdu_body = r.msdu_body;
    e.msdu = r.msdu;
    e.beacon = r.beacon;
    e.msdu_raw_base = r.msdu_raw_base;
    e.raw_bytes = r.payload_for_log;
    e.raw_wire = f.raw_wire;
    return e;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCommandLineParser cli;
    const QCommandLineOption opt_bin(QStringLiteral("bin"),
        QStringLiteral("capture .bin"), QStringLiteral("path"));
    const QCommandLineOption opt_plugin(QStringLiteral("plugin-so"),
        QStringLiteral("dissector .so"), QStringLiteral("path"));
    const QCommandLineOption opt_variant(QStringLiteral("variant"),
        QStringLiteral("gw|nw"), QStringLiteral("v"), QStringLiteral("nw"));
    const QCommandLineOption opt_max(QStringLiteral("max"),
        QStringLiteral("max frames"), QStringLiteral("n"), QStringLiteral("0"));
    const QCommandLineOption opt_tree(QStringLiteral("tree-max"),
        QStringLiteral("frames to tree-compare"), QStringLiteral("n"),
        QStringLiteral("400"));
    cli.addOption(opt_bin);
    cli.addOption(opt_plugin);
    cli.addOption(opt_variant);
    cli.addOption(opt_max);
    cli.addOption(opt_tree);
    cli.process(app);
    const QString bin_path = cli.value(QStringLiteral("bin"));
    const QString plugin_path = cli.value(QStringLiteral("plugin-so"));
    const bool is_gw = cli.value(QStringLiteral("variant")) == QLatin1String("gw");
    const int max_frames = cli.value(QStringLiteral("max")).toInt();
    const int tree_max = cli.value(QStringLiteral("tree-max")).toInt();
    if (bin_path.isEmpty() || plugin_path.isEmpty()) {
        out << "usage: --bin <file> --plugin-so <so> --variant <gw|nw>\n";
        return 2;
    }

    // 载插件(与 NativeBackend 相同的 qobject_cast 路径)
    QPluginLoader loader(plugin_path);
    QObject* inst = loader.instance();
    if (!inst) {
        out << "plugin load failed: " << loader.errorString() << "\n";
        return 2;
    }
    auto* plugin_iface = qobject_cast<IPlugin*>(inst);
    auto* dissector = qobject_cast<IDissectorPlugin*>(inst);
    if (!dissector || !plugin_iface || !plugin_iface->initialize()) {
        out << "plugin does not implement IDissectorPlugin or init failed\n";
        return 2;
    }
    out << "dissector: " << dissector->dissector_protocol_id() << "\n";

    // 读帧(SerialReader 文件回放,与主程序同一帧来源)
    QList<BplcFrame> frames;
    {
        SerialReader reader;
        QEventLoop loop;
        QObject::connect(&reader, &SerialReader::frame_ready,
                         [&frames](const BplcFrame& f) { frames.append(f); });
        QObject::connect(&reader, &SerialReader::finished, &loop,
                         &QEventLoop::quit);
        QTimer::singleShot(120000, &loop, &QEventLoop::quit);
        ReaderConfig cfg;
        cfg.mode = ReaderMode::FilePlayback;
        cfg.file_path = bin_path;
        if (!reader.start(cfg)) {
            out << "reader start failed\n";
            return 2;
        }
        loop.exec();
        reader.stop();
    }
    if (max_frames > 0 && frames.size() > max_frames) frames.resize(max_frames);
    out << "frames: " << frames.size() << "\n";
    if (frames.isEmpty()) return 2;

    std::unique_ptr<IProtocolParser> builtin =
        is_gw ? std::unique_ptr<IProtocolParser>(new GW_2022_Parser())
              : std::unique_ptr<IProtocolParser>(new NW_2021_Parser());
    MsduState msdu_state;
    const ParseFilter filter;

    ProtocolTree tree_widget;
    tree_widget.set_variant(is_gw ? ProtocolVariant::GW_2022
                                  : ProtocolVariant::NW_2021);

    int scalar_bad = 0, tree_bad = 0, tree_done = 0, accepted = 0;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < frames.size(); ++i) {
        const BplcFrame& f = frames[i];
        const ParseResult rb = builtin->parse(f, msdu_state, filter);

        FrameContext ctx;
        ctx.epoch_ms = f.meta.epoch_ms;
        ctx.arrival_us = f.arrival_us;
        ctx.ntb = f.meta.timestamp;
        ctx.is_rf = f.meta.is_rf;
        ctx.channel = f.meta.channel;
        ctx.frame_index = quint64(i + 1);
        ctx.seg_start = f.meta.seg_start;
        const DissectResult rd = dissector->dissect(f.data, ctx);

        // ---- 标量比对 ----
        QStringList diffs;
        if (rb.accept != rd.accept)
            diffs << QStringLiteral("accept builtin=%1 plugin=%2")
                         .arg(rb.accept).arg(rd.accept);
        if (rb.accept && rd.accept) {
            ++accepted;
            if (rb.mpdu.frame_type != rd.mpdu.frame_type)
                diffs << QStringLiteral("frame_type %1!=%2")
                             .arg(rb.mpdu.frame_type).arg(rd.mpdu.frame_type);
            if (rb.mpdu.src_tei != rd.mpdu.src_tei)
                diffs << QStringLiteral("src_tei %1!=%2")
                             .arg(rb.mpdu.src_tei).arg(rd.mpdu.src_tei);
            if (rb.mpdu.dst_tei != rd.mpdu.dst_tei)
                diffs << QStringLiteral("dst_tei %1!=%2")
                             .arg(rb.mpdu.dst_tei).arg(rd.mpdu.dst_tei);
            if (rb.mpdu.net_id != rd.mpdu.net_id)
                diffs << QStringLiteral("net_id %1!=%2")
                             .arg(rb.mpdu.net_id).arg(rd.mpdu.net_id);
            if (rb.msdu.present != rd.msdu_present)
                diffs << QStringLiteral("msdu_present %1!=%2")
                             .arg(rb.msdu.present).arg(rd.msdu_present);
            if (rb.msdu.summary != rd.msdu_summary)
                diffs << QStringLiteral("summary '%1'!='%2'")
                             .arg(rb.msdu.summary, rd.msdu_summary);
            if (rb.msdu_body != rd.msdu_body)
                diffs << QStringLiteral("msdu_body differs (%1 vs %2 B)")
                             .arg(rb.msdu_body.size()).arg(rd.msdu_body.size());
            if (int(rb.msdu.topo_event.kind) != int(rd.topo_event.kind))
                diffs << QStringLiteral("topo_kind %1!=%2")
                             .arg(int(rb.msdu.topo_event.kind))
                             .arg(int(rd.topo_event.kind));
            if (rb.msdu.tei_mac_pairs.size() != rd.tei_mac_pairs.size())
                diffs << QStringLiteral("tei_mac_pairs %1!=%2")
                             .arg(rb.msdu.tei_mac_pairs.size())
                             .arg(rd.tei_mac_pairs.size());
            if (rb.payload_for_log != rd.dissected_bytes)
                diffs << QStringLiteral("dissected_bytes differs (%1 vs %2 B)")
                             .arg(rb.payload_for_log.size())
                             .arg(rd.dissected_bytes.size());
        }
        if (!diffs.isEmpty()) {
            if (scalar_bad < 10)
                out << "frame " << (i + 1) << " scalar diffs: "
                    << diffs.join(QStringLiteral("; ")) << "\n";
            ++scalar_bad;
            continue;
        }

        // ---- 字段树比对(前 tree_max 帧全比,其后抽样) ----
        if (i < tree_max || (i % 97) == 0) {
            ++tree_done;
            PacketEntry eb = entry_of(rb, f);
            eb.dissect_fields.clear();
            tree_widget.show_packet(eb);
            QStringList lines_b;
            for (int t = 0; t < tree_widget.topLevelItemCount(); ++t)
                lines_b += flatten(tree_widget.topLevelItem(t), 0);

            PacketEntry ed;
            ed.accepted = rd.accept;
            ed.reason = rd.reject_reason;
            ed.meta = f.meta;
            if (rd.has_refined_ctx) {   // 与代理同规则:精化值覆盖粗值
                ed.meta.timestamp = rd.refined_ctx.ntb;
                ed.meta.channel = rd.refined_ctx.channel;
                ed.meta.is_rf = rd.refined_ctx.is_rf;
            }
            ed.mpdu = rd.mpdu;
            ed.msdu_body = rd.msdu_body;
            ed.msdu.present = rd.msdu_present;
            ed.msdu.summary = rd.msdu_summary;
            ed.msdu.topo_event = rd.topo_event;
            ed.msdu.tei_mac_pairs = rd.tei_mac_pairs;
            ed.msdu_raw_base = rd.msdu_raw_base;
            ed.raw_bytes = rd.dissected_bytes.isEmpty() ? f.data
                                                        : rd.dissected_bytes;
            ed.raw_wire = f.raw_wire;
            ed.dissect_fields = rd.fields;
            tree_widget.show_packet(ed);
            QStringList lines_d;
            for (int t = 0; t < tree_widget.topLevelItemCount(); ++t)
                lines_d += flatten(tree_widget.topLevelItem(t), 0);

            if (lines_b != lines_d) {
                if (tree_bad < 5) {
                    out << "frame " << (i + 1) << " tree diff (builtin "
                        << lines_b.size() << " lines vs plugin "
                        << lines_d.size() << " lines):\n";
                    const int n = qMax(lines_b.size(), lines_d.size());
                    for (int l = 0; l < n; ++l) {
                        const QString a = l < lines_b.size() ? lines_b[l] : QStringLiteral("<none>");
                        const QString b = l < lines_d.size() ? lines_d[l] : QStringLiteral("<none>");
                        if (a != b) {
                            out << "  line " << l << ":\n    builtin: " << a
                                << "\n    plugin:  " << b << "\n";
                            break;
                        }
                    }
                }
                ++tree_bad;
            }
        }
    }
    out << "accepted: " << accepted << "/" << frames.size()
        << ", scalar mismatches: " << scalar_bad
        << ", trees compared: " << tree_done
        << ", tree mismatches: " << tree_bad
        << ", elapsed: " << timer.elapsed() << " ms\n";
    out << ((scalar_bad == 0 && tree_bad == 0) ? "COMPARE PASS\n"
                                               : "COMPARE FAIL\n");
    return (scalar_bad == 0 && tree_bad == 0) ? 0 : 1;
}
