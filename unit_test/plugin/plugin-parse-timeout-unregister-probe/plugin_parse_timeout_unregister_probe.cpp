/// @file plugin_parse_timeout_unregister_probe.cpp
/// @brief A3 回归:解析超时后工作线程内同步注销注册表条目
///
/// @details 移植来源 parser 分支 A3(2026-10-12 1fc0a21):
/// 超时后注销必须在工作线程内同步完成,不能只排队到 GUI 线程。旧代码把
/// unregister_parser_for_owner 藏在 QueuedConnection 的 lambda 里,超时与
/// GUI 事件泵之间有一段窗口:注册表条目仍在,is_plugin_protocol 已 false,
/// 代理继续拒帧、任何按归属取解析器的调用仍拿到已死代理,下拉框仍展示该协议。
///
/// 探针机制:可执行文件自复制为 bplc-plugin-host(同 host-exit-uaf-probe 的
/// 做法),--socket 参数进入 fake-host 模式。fake host 用 Envelope 帧格式
/// (plugin_ipc::encode_frame,非 H2 的旧 [len][type][payload] 格式),Ping
/// 正常回 Pong(心跳不断),但对 ParseRequest 故意不回:工作线程调
/// parse_via_plugin 5 秒超时返回 false。主线程在等待期间只 msleep 轮询,
/// 不泵事件循环,排队到 GUI 的注销/状态回调跑不起来,以此区分"工作线程内
/// 同步注销"与"GUI 排队注销"。关键断言在 GUI 泵之前就做:注册表条目已空。
///
/// 预期输出 `RESULT: ALL PASS (0 failures)`, exit 0。旧代码跑本探针:
/// "registry entry already gone before GUI pump" 与 "find_protocol_for_owner
/// returns null" 断言 FAIL(条目仍在)。运行时约 8 秒(5 秒超时 + 收尾)。
/// 必须以 ASan+UBSan 构建运行(build.sh 已加)。

#include <QApplication>
#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <thread>

#include "plugin_manager.h"
#include "plugin_ipc.h"
#include "protocolfactory.h"

namespace {

int g_checks = 0;
int g_fails = 0;

void check(const char* name, bool ok) {
    ++g_checks;
    if (!ok) ++g_fails;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
}

void spin_for(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

template <typename Pred>
bool spin_until(Pred&& pred, int timeout_ms) {
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > timeout_ms) return pred();
        spin_for(20);
    }
    return true;
}

const QString kId = QStringLiteral("A3_SILENT");

bool write_file(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

/// @brief fake-host 模式:应答 Ping,忽略 ParseRequest(制造超时),Shutdown 后退出。
int fake_host_main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = QCoreApplication::arguments();
    const int si = a.indexOf(QStringLiteral("--socket"));
    const int pi = a.indexOf(QStringLiteral("--plugin"));
    if (si < 0 || pi < 0 || si + 1 >= a.size() || pi + 1 >= a.size()) return 2;
    const QString dir = a.at(pi + 1);
    QFile mf(dir + QStringLiteral("/plugin.json"));
    if (!mf.open(QIODevice::ReadOnly)) return 2;
    const QJsonObject m = QJsonDocument::fromJson(mf.readAll()).object();

    QLocalSocket s;
    QByteArray rx;
    auto send = [&](plugin_ipc::MsgType t, const QByteArray& p) {
        const QByteArray f = plugin_ipc::encode_frame(static_cast<quint8>(t), p);
        if (f.isEmpty()) { app.exit(3); return; }
        s.write(f);
        s.flush();
    };
    QObject::connect(&s, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&s, &QLocalSocket::readyRead, [&]() {
        rx.append(s.readAll());
        for (;;) {
            QByteArray fr;
            const auto took = plugin_ipc::try_take_frame(rx, fr);
            if (took != plugin_ipc::FrameTake::Ok) {
                if (took == plugin_ipc::FrameTake::Bad) app.exit(3);
                return;
            }
            quint8 type = 0;
            QByteArray payload;
            QString err;
            if (!plugin_ipc::decode_frame(fr, &type, &payload, &err)) { app.exit(3); return; }
            QDataStream ds(payload);
            ds.setVersion(plugin_ipc::kStreamVersion);
            const auto mt = static_cast<plugin_ipc::MsgType>(type);
            if (mt == plugin_ipc::MsgType::Ping) {
                quint64 seq = 0;
                ds >> seq;
                QByteArray p;
                QDataStream o(&p, QIODevice::WriteOnly);
                o.setVersion(plugin_ipc::kStreamVersion);
                o << seq;
                send(plugin_ipc::MsgType::Pong, p);
            } else if (mt == plugin_ipc::MsgType::Shutdown) {
                app.quit();
            }
            // ParseRequest 故意不回:制造解析超时。HelloAck 等其余帧直接丢弃。
        }
    });
    s.connectToServer(a.at(si + 1));
    if (!s.waitForConnected(10000)) return 2;
    QByteArray hello;
    QDataStream o(&hello, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << plugin_ipc::kApiVersion << m.value(QStringLiteral("name")).toString()
      << m.value(QStringLiteral("protocol_id")).toString() << plugin_ipc::kWireFormatVersion;
    send(plugin_ipc::MsgType::Hello, hello);
    return app.exec();
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--socket") == 0) return fake_host_main(argc, argv);

    QApplication app(argc, argv);
    const QString host = QCoreApplication::applicationDirPath() + QStringLiteral("/bplc-plugin-host");
    QFile::remove(host);
    check("fake host installed as bplc-plugin-host",
          QFile::copy(QCoreApplication::applicationFilePath(), host));
    QFile(host).setPermissions(QFile(host).permissions() | QFileDevice::ExeOwner);

    QTemporaryDir tmp;
    check("temp dir valid", tmp.isValid());
    const QString plugins = tmp.path() + QStringLiteral("/plugins");
    const QString pdir = plugins + QStringLiteral("/a3-silent");
    QDir().mkpath(pdir);
    // 仅写有效 manifest 字段(name/api_version/protocol_id/entry/runtime),
    // 与 host-exit-uaf-probe 的 make_plugin 保持一致。
    const QByteArray manifest = R"({"api_version":1,"name":"a3-silent","protocol_id":"A3_SILENT","display_name":"A3 silent","display_name_en":"A3 silent","entry":"parser.lua","runtime":"lua","version":"1.0.0"})";
    check("plugin manifest written", write_file(pdir + QStringLiteral("/plugin.json"), manifest));
    check("parser stub written", write_file(pdir + QStringLiteral("/parser.lua"),
                                            QByteArrayLiteral("-- A3 probe stub: never executed\n")));

    auto& pm = PluginManager::instance();
    QStringList status;
    QObject::connect(&pm, &PluginManager::plugin_status_changed,
                     [&](const QString& id, const QString& st) {
                         if (id == kId) status << st;
                     });
    pm.load_all(plugins);
    check("silent plugin ready", spin_until([&] { return pm.is_plugin_protocol(kId); }, 20000));
    check("registry entry present before timeout",
          protocol_owner_plugin_id(kId) == kId);

    // 工作线程调 parse_via_plugin:fake host 不回 ParseRequest,5 秒超时返回 false。
    std::atomic<bool> done{false};
    std::atomic<bool> accepted{true};
    std::thread worker([&] {
        BplcFrame frame;
        MsduState msdu;
        ParseFilter filter;
        ParseResult r;
        const bool ok = pm.parse_via_plugin(kId, frame, msdu, filter, &r);
        accepted.store(ok);
        done.store(true);
    });
    // 主线程只 msleep 轮询,不泵事件循环:排队到 GUI 的注销/状态回调跑不起来。
    QElapsedTimer t;
    t.start();
    while (!done.load() && t.elapsed() < 15000) QThread::msleep(25);
    check("worker parse finished (timed out)", done.load());
    if (!done.load()) {
        worker.detach();
        std::printf("\nRESULT: FAILURES (%d failures)\n", g_fails);
        return 1;
    }
    check("worker parse returned false (timeout)", !accepted.load());
    check("A3: registry entry already gone before GUI pump",
          protocol_owner_plugin_id(kId).isEmpty());
    check("A3: find_protocol_for_owner returns null (no builtin for new id)",
          find_protocol_for_owner(kId, kId) == nullptr);
    check("A3: plugin no longer a plugin protocol", !pm.is_plugin_protocol(kId));
    worker.join();

    // 再泵事件循环,让 GUI 排队收尾跑完:状态上报 + 退役 host。
    check("GUI tail still reports timeout-disabled",
          spin_until([&] { return status.contains(QStringLiteral("timeout-disabled")); }, 10000));
    spin_for(500);
    check("fake host process reclaimed", pm.findChildren<QProcess*>().isEmpty());

    pm.stop_all();
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    app.exec();
    QFile::remove(host);
    std::printf("\nRESULT: %s (%d failures)\n", g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
