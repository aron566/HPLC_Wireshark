/// @file dissect_ipc_test.cpp
/// @brief 解剖器端到端 IPC 测试:经 PluginManager 拉起真实 bplc-plugin-host
///        子进程,握手注册 → dissect_via_plugin 解剖一帧真实净荷,结果与
///        进程内内置解析器交叉核对(dissected_bytes/精化 NTB/通道/字段数),
///        再验总线投递与 stop_all 停机注销。
/// @details 运行布局(见同目录 dissect_ipc_test.pro 注释):
///   <stage>/dissect_ipc_test  <stage>/bplc-plugin-host
///   <stage>/plugins/gw-dissector/{plugin.json, libgw_dissector.so}
///   dissect_ipc_test --bin <gw capture.bin> --plugins <stage>/plugins
/// 退出码:0=通过;1=断言失败;2=参数/环境错误。
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <thread>

#include "gw_2022_parser.h"
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
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCommandLineParser cli;
    const QCommandLineOption opt_bin(QStringLiteral("bin"),
        QStringLiteral("GW capture .bin"), QStringLiteral("path"));
    const QCommandLineOption opt_plugins(QStringLiteral("plugins"),
        QStringLiteral("plugins dir"), QStringLiteral("dir"));
    cli.addOption(opt_bin);
    cli.addOption(opt_plugins);
    cli.process(app);
    const QString bin_path = cli.value(QStringLiteral("bin"));
    const QString plugins_dir = cli.value(QStringLiteral("plugins"));
    if (bin_path.isEmpty() || plugins_dir.isEmpty()) {
        out << "usage: --bin <file> --plugins <dir>\n";
        return 2;
    }

    // 取第一帧真实净荷(与主程序同一帧来源)
    BplcFrame frame;
    bool have_frame = false;
    {
        SerialReader reader;
        QEventLoop loop;
        QObject::connect(&reader, &SerialReader::frame_ready,
                         [&](const BplcFrame& f) {
                             if (!have_frame) {
                                 frame = f;
                                 have_frame = true;
                                 loop.quit();
                             }
                         });
        QObject::connect(&reader, &SerialReader::finished, &loop,
                         &QEventLoop::quit);
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
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
    if (!have_frame) {
        out << "no frame read\n";
        return 2;
    }
    out << "frame payload: " << frame.data.size() << " B\n";

    // 工厂内置协议保护(P2a 回归):插件注册不得覆盖内置条目,
    // 注销不得删除内置条目
    check(!register_parser(QStringLiteral("GW_2022"),
                           []() { return std::unique_ptr<IProtocolParser>(); }),
          "register_parser refuses to overwrite builtin GW_2022");
    check(is_builtin_protocol(QStringLiteral("GW_2022")), "GW_2022 is builtin");
    unregister_parser(QStringLiteral("GW_2022"));   // 必须为无操作
    {
        auto p = make_parser_by_id(QStringLiteral("GW_2022"));
        check(p && dynamic_cast<GW_2022_Parser*>(p.get()) != nullptr,
              "builtin GW_2022 survives plugin unregister attempt");
    }

    // 进程内内置解析(交叉核对基准)
    GW_2022_Parser builtin;
    MsduState msdu_state;
    const ParseResult rb = builtin.parse(frame, msdu_state, ParseFilter());
    check(rb.accept, "builtin accepts frame");

    // 拉起 host 并等握手注册
    PluginManager& pm = PluginManager::instance();
    pm.loadAll(plugins_dir);
    QElapsedTimer timer;
    timer.start();
    while (!pm.is_dissector_protocol(QStringLiteral("GW_2022_PLUGIN"))
           && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(20);
    }
    check(pm.is_dissector_protocol(QStringLiteral("GW_2022_PLUGIN")),
          "dissector registered after handshake");
    check(pm.dissector_protocol_ids().contains(
              QStringLiteral("GW_2022_PLUGIN")),
          "protocol id listed for dropdown");

    // 经 IPC 解剖。注意契约:dissect_via_plugin 必须从 worker 线程调用
    // (与生产中 FrameDispatcher 所在线程一致);主线程直调会因 readyRead
    // 槽抢先消费响应而假超时——本测试特此在子线程发请求,主线程泵事件。
    FrameContext ctx;
    ctx.epoch_ms = frame.meta.epoch_ms;
    ctx.arrival_us = frame.arrival_us;
    ctx.ntb = frame.meta.timestamp;
    ctx.is_rf = frame.meta.is_rf;
    ctx.channel = frame.meta.channel;
    ctx.frame_index = 1;
    ctx.seg_start = frame.meta.seg_start;
    DissectResult dr;
    bool sent = false;
    std::atomic<bool> done{false};
    std::thread worker([&] {
        sent = pm.dissect_via_plugin(QStringLiteral("GW_2022_PLUGIN"),
                                      frame.data, ctx, &dr);
        done.store(true);
    });
    while (!done.load()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    worker.join();
    check(sent, "dissect_via_plugin round-trip");
    check(dr.accept == rb.accept, "accept matches builtin");
    check(dr.dissected_bytes == rb.payload_for_log,
          "dissected_bytes over IPC == builtin payload_for_log");
    check(dr.has_refined_ctx && dr.refined_ctx.ntb == rb.meta.timestamp,
          "refined NTB over IPC matches builtin meta");
    check(dr.has_refined_ctx && dr.refined_ctx.channel == rb.meta.channel,
          "refined channel over IPC matches builtin meta");
    check(!dr.fields.isEmpty(), "structured fields returned");
    out << "  fields: " << dr.fields.size() << " top-level sections\n";

    // 总线投递(host 目标存在 → true;不存在 → false)
    check(pm.send_plugin_message(QStringLiteral("gw-dissector"),
                                 QStringLiteral("ipc-test"),
                                 QStringLiteral("ping"), QByteArray("hello")),
          "bus deliver to running host plugin");
    check(!pm.send_plugin_message(QStringLiteral("no-such-plugin"),
                                  QStringLiteral("ipc-test"),
                                  QStringLiteral("ping"), QByteArray()),
          "bus deliver to unknown plugin returns false");

    // 停机:注销 + host 进程退出
    pm.stop_all();
    check(!pm.is_dissector_protocol(QStringLiteral("GW_2022_PLUGIN")),
          "dissector unregistered after stop_all");

    // stop_all 与在途 dissect 并发(P1 回归):重新拉起插件,worker 线程
    // 持续 dissect,主线程中途 stop_all——全程不得崩溃,worker 在插件
    // 停机后应收到失败并退出循环(进程存活到此处即为通过的核心证据)
    pm.loadAll(plugins_dir);
    timer.restart();
    while (!pm.is_dissector_protocol(QStringLiteral("GW_2022_PLUGIN"))
           && timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(20);
    }
    check(pm.is_dissector_protocol(QStringLiteral("GW_2022_PLUGIN")),
          "dissector re-registered for stop_all-under-load phase");
    std::atomic<bool> stop_flag{false};
    std::atomic<int> dissect_calls{0};
    std::thread load_worker([&]() {
        while (!stop_flag.load()) {
            DissectResult tmp;
            if (!pm.dissect_via_plugin(QStringLiteral("GW_2022_PLUGIN"),
                                       frame.data, ctx, &tmp))
                break;   // 插件已停机/禁用 → 退出
            dissect_calls.fetch_add(1);
        }
    });
    QThread::msleep(200);
    pm.stop_all();
    stop_flag.store(true);
    load_worker.join();
    check(dissect_calls.load() > 0, "dissect loop ran across stop_all");
    check(!pm.is_dissector_protocol(QStringLiteral("GW_2022_PLUGIN")),
          "unregistered after stop_all under load");

    out << (failures == 0 ? "IPC TEST PASS\n" : "IPC TEST FAIL\n");
    return failures == 0 ? 0 : 1;
}
