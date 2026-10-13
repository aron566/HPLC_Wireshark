/// @file export_consistency_test.cpp
/// @brief 导出层一致性实测:同一抓包分别以内置解析与插件解剖生成全部
///        PacketEntry,验证导出产物在两种模式下逐字节一致。
/// @details 两条生成链与生产完全同款:
///   - 帧源:SerialReader 文件回放(与主程序同一帧来源);
///   - 解析:make_parser_by_id 取解析器 + parser->parse(frame, msdu, filter)
///     (与 FrameDispatcher::on_frame 同款调用);插件侧解析器为
///     PluginDissectorProxy,经 PluginManager + 真实 bplc-plugin-host
///     子进程 IPC 解剖,且按契约从 worker 线程调用(主线程泵事件);
///   - 条目:按 MainWindow::make_entry 的字段赋值与 NTB Delta 规则生成
///     (导出相关字段逐项同构;GUI/topo/插件喂帧等副作用不影响导出)。
/// 导出验证:
///   1) bin/txt:用生产导出 writer(playbackwriter.h 的 PlaybackBinWriter /
///      RawHexWriter 本体)对两组条目流式写出,整文件逐字节比对 + sha256;
///   2) csv:csv_row 是 MainWindow 内部纯函数、无独立 writer 可调用,改以
///      pser::serialize_entry 对两组条目逐条字节比对钉死其全部输入——
///      csv_row 读取的每个字段(index/epoch/delta/accepted/reason/raw_wire/
///      meta/mpdu/msdu/msdu_body/raw_bytes)都在序列化覆盖内。唯一设计内
///      差异是 dissect_fields:任何导出路径都不读它(仅协议树视图消费),
///      故比对时插件侧清零该字段,另行单独校验其存在性(内置恒空、
///      插件在 accepted 帧非空)。
/// 运行布局(与 dissect_ipc_test 相同):
///   <stage>/export_consistency_test  <stage>/bplc-plugin-host
///   <stage>/plugins/{gw,nw}-dissector/{plugin.json, lib*.so}
///   export_consistency_test --bin <capture> --variant <gw|nw> --plugins <dir>
/// 退出码:0=通过;1=断言失败;2=参数/环境错误。
#include <QApplication>
#include <QBuffer>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <thread>

#include "packetentry_serialize.h"
#include "playbackwriter.h"
#include "plugin_dissector_proxy.h"
#include "plugin_manager.h"
#include "protocolfactory.h"
#include "serialreader.h"

namespace {
QTextStream out(stdout);
int failures = 0;
void check(bool ok, const QString& what) {
    out << (ok ? "  ok: " : "  FAIL: ") << what << "\n";
    if (!ok) ++failures;
}

/// MainWindow::make_entry 的导出相关字段赋值(逐项同构)+ NTB Delta 规则
struct EntryMaker {
    int index_counter = 0;
    quint32 last_ntb = 0;
    PacketEntry make(const ParseResult& r, qint64 now) {
        PacketEntry e;
        e.index = ++index_counter;
        const qint64 t = r.meta.frame_time.isValid() ? r.meta.epoch_ms : now;
        e.epoch_ms = t;
        e.accepted = r.accept;
        e.reason = r.reject_reason;
        const quint32 ntb = r.meta.timestamp;
        if (r.meta.seg_start) {
            e.delta_us = 0;
        } else {
            const qint64 dn = qint32(ntb - last_ntb);
            e.delta_us = (dn > 0 && dn <= playback::kMaxNtbGapTicks)
                             ? playback::ntb_to_us(quint32(dn))
                             : 0;
        }
        last_ntb = ntb;
        e.meta = r.meta;
        e.mpdu = r.mpdu;
        e.msdu_body = r.msdu_body;
        e.raw_wire = r.raw_wire;
        e.msdu = r.msdu;
        e.beacon = r.beacon;
        e.msdu_raw_base = r.msdu_raw_base;
        e.raw_bytes = r.payload_for_log;
        e.dissect_fields = r.dissect_fields;
        return e;
    }
};

QString sha256_hex(const QByteArray& b) {
    return QString::fromLatin1(
        QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

/// 首个不一致条目的分组定位(用序列化头自身的 write_* 分段比对)
QStringList diff_groups(const PacketEntry& a, const PacketEntry& b) {
    QStringList g;
    if (a.index != b.index) g << QStringLiteral("index");
    if (a.epoch_ms != b.epoch_ms) g << QStringLiteral("epoch_ms");
    if (a.delta_us != b.delta_us) g << QStringLiteral("delta_us");
    if (a.accepted != b.accepted) g << QStringLiteral("accepted");
    if (a.reason != b.reason)
        g << QStringLiteral("reason('%1' vs '%2')").arg(a.reason, b.reason);
    if (a.raw_wire != b.raw_wire) g << QStringLiteral("raw_wire");
    if (a.msdu_body != b.msdu_body) g << QStringLiteral("msdu_body");
    if (a.raw_bytes != b.raw_bytes) g << QStringLiteral("raw_bytes");
    if (a.msdu_raw_base != b.msdu_raw_base) g << QStringLiteral("msdu_raw_base");
    const auto seg = [](auto&& fn) {
        QByteArray buf;
        QDataStream s(&buf, QIODevice::WriteOnly);
        fn(s);
        return buf;
    };
    if (seg([&](QDataStream& s) { pser::write_meta(s, a.meta); })
        != seg([&](QDataStream& s) { pser::write_meta(s, b.meta); }))
        g << QStringLiteral("meta");
    if (seg([&](QDataStream& s) { pser::write_mpdu(s, a.mpdu); })
        != seg([&](QDataStream& s) { pser::write_mpdu(s, b.mpdu); }))
        g << QStringLiteral("mpdu");
    if (seg([&](QDataStream& s) { pser::StrPool p; pser::write_msdu_info(s, a.msdu, p); })
        != seg([&](QDataStream& s) { pser::StrPool p; pser::write_msdu_info(s, b.msdu, p); }))
        g << QStringLiteral("msdu");
    if (seg([&](QDataStream& s) { pser::StrPool p; pser::write_msdu_info(s, a.beacon, p); })
        != seg([&](QDataStream& s) { pser::StrPool p; pser::write_msdu_info(s, b.beacon, p); }))
        g << QStringLiteral("beacon");
    return g;
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCommandLineParser cli;
    const QCommandLineOption opt_bin(QStringLiteral("bin"),
        QStringLiteral("capture .bin"), QStringLiteral("path"));
    const QCommandLineOption opt_plugins(QStringLiteral("plugins"),
        QStringLiteral("plugins dir"), QStringLiteral("dir"));
    const QCommandLineOption opt_variant(QStringLiteral("variant"),
        QStringLiteral("gw|nw"), QStringLiteral("v"), QStringLiteral("gw"));
    const QCommandLineOption opt_max(QStringLiteral("max"),
        QStringLiteral("max frames (0=all)"), QStringLiteral("n"),
        QStringLiteral("0"));
    cli.addOption(opt_bin);
    cli.addOption(opt_plugins);
    cli.addOption(opt_variant);
    cli.addOption(opt_max);
    cli.process(app);
    const QString bin_path = cli.value(QStringLiteral("bin"));
    const QString plugins_dir = cli.value(QStringLiteral("plugins"));
    const bool is_gw = cli.value(QStringLiteral("variant")) == QLatin1String("gw");
    const int max_frames = cli.value(QStringLiteral("max")).toInt();
    if (bin_path.isEmpty() || plugins_dir.isEmpty()) {
        out << "usage: --bin <file> --variant <gw|nw> --plugins <dir>\n";
        return 2;
    }
    const QString builtin_id = is_gw ? QStringLiteral("GW_2022")
                                     : QStringLiteral("NW_2021");
    const QString plugin_id = builtin_id + QStringLiteral("_PLUGIN");
    // 无时间标签帧的 epoch 兜底:两条链用同一常量,保证可比
    const qint64 kNow = 1700000000000LL;

    // ---- 读帧(SerialReader 文件回放,与主程序同一帧来源) ----
    QList<BplcFrame> frames;
    {
        SerialReader reader;
        QEventLoop loop;
        QObject::connect(&reader, &SerialReader::frame_ready,
                         [&frames](const BplcFrame& f) { frames.append(f); });
        QObject::connect(&reader, &SerialReader::finished, &loop,
                         &QEventLoop::quit);
        QTimer::singleShot(300000, &loop, &QEventLoop::quit);
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
    if (max_frames > 0 && frames.size() > max_frames)
        frames = frames.mid(0, max_frames);
    out << "frames: " << frames.size() << "\n";
    if (frames.isEmpty()) return 2;

    // ---- 内置链:工厂解析器 + parse(frame, msdu, filter) ----
    QVector<PacketEntry> builtin_entries;
    builtin_entries.reserve(frames.size());
    {
        auto parser = make_parser_by_id(builtin_id);
        if (!parser) {
            out << "builtin parser missing\n";
            return 2;
        }
        MsduState msdu;
        EntryMaker maker;
        for (const BplcFrame& f : frames)
            builtin_entries.append(
                maker.make(parser->parse(f, msdu, ParseFilter()), kNow));
    }
    out << "builtin entries: " << builtin_entries.size() << "\n";

    // ---- 插件链:host 握手注册 → 工厂取代理解剖器 → worker 线程喂帧 ----
    PluginManager& pm = PluginManager::instance();
    pm.loadAll(plugins_dir);
    {
        QElapsedTimer timer;
        timer.start();
        while (!pm.is_dissector_protocol(plugin_id) && timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(20);
        }
    }
    check(pm.is_dissector_protocol(plugin_id),
          "dissector registered after handshake");
    if (!pm.is_dissector_protocol(plugin_id)) return 1;
    auto plugin_parser = make_parser_by_id(plugin_id);
    check(plugin_parser
              && dynamic_cast<PluginDissectorProxy*>(plugin_parser.get())
                     != nullptr,
          "factory returns PluginDissectorProxy");
    if (!plugin_parser) return 1;

    QVector<ParseResult> plugin_results;
    plugin_results.resize(frames.size());
    std::atomic<bool> done{false};
    std::thread worker([&] {
        MsduState msdu;
        IProtocolParser* p = plugin_parser.get();
        for (int i = 0; i < frames.size(); ++i)
            plugin_results[i] = p->parse(frames[i], msdu, ParseFilter());
        done.store(true);
    });
    while (!done.load()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    worker.join();
    QVector<PacketEntry> plugin_entries;
    plugin_entries.reserve(frames.size());
    {
        EntryMaker maker;
        for (const ParseResult& r : plugin_results)
            plugin_entries.append(maker.make(r, kNow));
    }
    out << "plugin entries: " << plugin_entries.size() << "\n";
    pm.stop_all();

    check(builtin_entries.size() == plugin_entries.size(),
          "entry counts equal");

    // ---- 诊断计数(定位差异来源用,常驻输出) ----
    {
        const auto stats = [](const QVector<PacketEntry>& v) {
            int acc = 0, empty_raw = 0, empty_wire = 0, msdu_present = 0,
                msdu_seq_nonzero = 0, msdu_srctei_nonzero = 0;
            for (const PacketEntry& e : v) {
                if (e.accepted) ++acc;
                if (e.raw_bytes.isEmpty()) ++empty_raw;
                if (e.raw_wire.isEmpty()) ++empty_wire;
                if (e.msdu.present) ++msdu_present;
                if (e.msdu.msdu_seq != 0) ++msdu_seq_nonzero;
                if (e.msdu.msdu_src_tei > 0) ++msdu_srctei_nonzero;
            }
            return QStringLiteral(
                       "accepted %1, raw_bytes empty %2, raw_wire empty %3, "
                       "msdu present %4, msdu_seq!=0 %5, msdu_src_tei>0 %6")
                .arg(acc).arg(empty_raw).arg(empty_wire).arg(msdu_present)
                .arg(msdu_seq_nonzero).arg(msdu_srctei_nonzero);
        };
        out << "builtin stats: " << stats(builtin_entries) << "\n";
        out << "plugin  stats: " << stats(plugin_entries) << "\n";
        for (int i = 0; i < qMin(builtin_entries.size(), plugin_entries.size()); ++i) {
            if (builtin_entries[i].accepted && !plugin_entries[i].accepted) {
                out << "first plugin-only reject at entry " << i
                    << " builtin frame_type " << builtin_entries[i].mpdu.frame_type
                    << " reason '" << plugin_entries[i].reason << "'\n";
                break;
            }
        }
        for (int i = 0; i < qMin(3, builtin_entries.size()); ++i) {
            const PacketEntry& a = builtin_entries[i];
            const PacketEntry& b = plugin_entries[i];
            out << "entry " << i << " builtin: raw " << a.raw_bytes.size()
                << "B msdu_seq " << a.msdu.msdu_seq << " src_tei "
                << a.msdu.msdu_src_tei << " dst_tei " << a.msdu.msdu_dst_tei
                << " simple " << a.msdu.simple_head << "\n";
            out << "entry " << i << " plugin : raw " << b.raw_bytes.size()
                << "B msdu_seq " << b.msdu.msdu_seq << " src_tei "
                << b.msdu.msdu_src_tei << " dst_tei " << b.msdu.msdu_dst_tei
                << " simple " << b.msdu.simple_head << "\n";
        }
    }

    // ---- 1) bin 导出:生产 PlaybackBinWriter 本体,整文件逐字节比对 ----
    {
        QByteArray bin_b, bin_p;
        QBuffer bb(&bin_b), bp(&bin_p);
        bb.open(QIODevice::WriteOnly);
        bp.open(QIODevice::WriteOnly);
        {
            playback::PlaybackBinWriter w(&bb);
            for (const PacketEntry& e : builtin_entries) w.add(e);
        }
        {
            playback::PlaybackBinWriter w(&bp);
            for (const PacketEntry& e : plugin_entries) w.add(e);
        }
        out << "bin export: builtin " << bin_b.size() << " B sha256 "
            << sha256_hex(bin_b) << "\n";
        out << "bin export: plugin  " << bin_p.size() << " B sha256 "
            << sha256_hex(bin_p) << "\n";
        check(bin_b == bin_p, "bin export byte-identical");
    }
    // ---- 1b) txt 导出:生产 RawHexWriter 本体(附带一并验) ----
    {
        QByteArray txt_b, txt_p;
        QBuffer bb(&txt_b), bp(&txt_p);
        bb.open(QIODevice::WriteOnly);
        bp.open(QIODevice::WriteOnly);
        {
            playback::RawHexWriter w(&bb);
            for (const PacketEntry& e : builtin_entries) w.add(e);
        }
        {
            playback::RawHexWriter w(&bp);
            for (const PacketEntry& e : plugin_entries) w.add(e);
        }
        out << "txt export: builtin " << txt_b.size() << " B sha256 "
            << sha256_hex(txt_b) << "\n";
        out << "txt export: plugin  " << txt_p.size() << " B sha256 "
            << sha256_hex(txt_p) << "\n";
        check(txt_b == txt_p, "txt export byte-identical");
    }
    // ---- 2) CSV 输入等价:逐条 serialize_entry 字节比对 ----
    {
        int mismatches = 0;
        int mismatch_collapse = 0;   // 插件被禁用后的"unavailable"连锁拒绝
        int mismatch_genuine = 0;    // 插件正常解剖下仍存在的字段级差异
        QStringList genuine_groups;
        int first_bad = -1;
        int plugin_fields_nonempty = 0;
        int builtin_fields_nonempty = 0;
        const int n = qMin(builtin_entries.size(), plugin_entries.size());
        for (int i = 0; i < n; ++i) {
            const PacketEntry& a = builtin_entries[i];
            PacketEntry b = plugin_entries[i];   // 拷贝以清零设计内差异字段
            if (!a.dissect_fields.isEmpty()) ++builtin_fields_nonempty;
            if (!b.dissect_fields.isEmpty()) ++plugin_fields_nonempty;
            b.dissect_fields.clear();
            pser::StrPool pool_a, pool_b;
            const QByteArray sa = pser::serialize_entry(a, pool_a);
            const QByteArray sb = pser::serialize_entry(b, pool_b);
            if (sa != sb) {
                ++mismatches;
                if (first_bad < 0) first_bad = i;
                if (plugin_entries[i].reason
                    == QStringLiteral("plugin dissector unavailable")) {
                    ++mismatch_collapse;
                } else {
                    ++mismatch_genuine;
                    for (const QString& g :
                         diff_groups(a, plugin_entries[i]))
                        if (!genuine_groups.contains(g))
                            genuine_groups << g;
                }
            }
        }
        out << "entry serialize compare: " << n << " entries, mismatches "
            << mismatches << " (genuine " << mismatch_genuine << ", collapse "
            << mismatch_collapse << ")\n";
        if (mismatch_genuine > 0)
            out << "genuine mismatch groups: "
                << genuine_groups.join(QStringLiteral(", ")) << "\n";
        if (first_bad >= 0) {
            const QStringList g = diff_groups(builtin_entries[first_bad],
                                              plugin_entries[first_bad]);
            out << "first mismatch at entry " << first_bad
                << " groups: " << g.join(QStringLiteral(", ")) << "\n";
        }
        check(mismatches == 0,
              "all csv-input fields byte-identical per entry");
        check(builtin_fields_nonempty == 0,
              "builtin entries carry no dissect_fields (by design)");
        out << "plugin entries with dissect_fields: "
            << plugin_fields_nonempty << " (info only, export ignores them)\n";
    }

    out << (failures == 0 ? "EXPORT CONSISTENCY PASS\n"
                          : "EXPORT CONSISTENCY FAIL\n");
    return failures == 0 ? 0 : 1;
}
