/// @file host_exit_uaf_probe.cpp
/// @brief 插件宿主进程退出/崩溃时 QProcess 释放后使用(UAF)回归探针
/// @details 修复前 PluginManager::shutdown_runtime() 在 proc->state()==NotRunning 时直接
///   `delete proc`。该路径正是从 QProcess 自己的 finished / errorOccurred 回调进来的
///   (on_process_finished / on_process_error → disable_plugin → shutdown_runtime),
///   即 QProcess 在自己的信号发射过程中被同步删除:信号返回后 Qt 的
///   QProcessPrivate::processFinished 还会继续 emit readChannelFinished / finished,
///   访问已释放对象。修复后改为 rt->process = nullptr + proc->deleteLater()。
///
///   本程序复制自身为 bplc-plugin-host 充当宿主(按插件目录 mode 文件决定行为):
///     crash  握手(Hello)后 300 ms 自杀(SIGKILL ⇒ CrashExit:errorOccurred(Crashed) 再 finished)
///     exit   握手后 300 ms 以退出码 3 正常退出(NormalExit:只有 finished)
///     early  不握手,启动即自杀(就绪前崩溃)
///     ok     正常宿主:回 Pong,收到 Shutdown / 断连即退出
///   用例:
///   ① load_all 后 crash/exit/early 三个宿主各自退出 ⇒ 插件被停用、主程序不崩(ASan 构建 +
///     free_fill_byte:未插桩的 Qt 代码读到已释放并被填充的 QProcess ⇒ 修复前必崩);
///     事件循环跑过后 PluginManager 名下残留的 QProcess 只剩仍在运行的宿主(deleteLater 确实回收)。
///   ② 原有语义不变:改 mode=ok、把 offlive 的 meta.json 置 enabled=false 后 restart_all ⇒
///     崩溃/退出停用的三个插件被重新拉起并就绪;meta.json 手动禁用的两个被跳过(skipped_disabled=2)、
///     不复活。
///   ③ 重启后的第二代宿主再次崩溃(restart_plugin 路径)同样不崩,QProcess 照常回收;stop_all 后为 0。
///   预期输出 `RESULT: ALL PASS (0 failures)` 且 `asan=1`,exit 0。
#include "ipc/plugin_ipc.h"
#include "manager/plugin_manager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <functional>
#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#define PROBE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PROBE_ASAN 1
#endif
#endif

static int g_fails = 0;
static void check(const QByteArray& name, bool ok, const QByteArray& detail = QByteArray()) {
    std::printf("%-70s %s%s%s\n", name.constData(), ok ? "PASS" : "FAIL",
                detail.isEmpty() ? "" : "  ", detail.constData());
    std::fflush(stdout);
    if (!ok) ++g_fails;
}

static void die_now(int code) {
#ifdef Q_OS_UNIX
    if (code < 0) ::kill(::getpid(), SIGKILL);   // CrashExit
    ::_exit(code);                                // 跳过 atexit/LSan,退出码确定
#else
    std::_Exit(code < 0 ? 99 : code);
#endif
}

// ---------------------------------------------------------------- 宿主
static int fake_host_main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = QCoreApplication::arguments();
    const int si = a.indexOf(QStringLiteral("--socket")), pi = a.indexOf(QStringLiteral("--plugin"));
    if (si < 0 || pi < 0 || si + 1 >= a.size() || pi + 1 >= a.size()) return 2;
    const QString dir = a.at(pi + 1);
    QFile mf(dir + QStringLiteral("/plugin.json")), modef(dir + QStringLiteral("/mode"));
    if (!mf.open(QIODevice::ReadOnly) || !modef.open(QIODevice::ReadOnly)) return 2;
    const QJsonObject m = QJsonDocument::fromJson(mf.readAll()).object();
    const QByteArray mode = modef.readAll().trimmed();
    if (mode == "early") die_now(-1);

    QLocalSocket s;
    QByteArray rx;
    auto send = [&](plugin_ipc::MsgType t, const QByteArray& p) {
        QByteArray f;
        QDataStream o(&f, QIODevice::WriteOnly);
        o.setVersion(plugin_ipc::kStreamVersion);
        o << quint32(1 + p.size()) << quint8(t);
        s.write(f + p);
        s.flush();
    };
    QObject::connect(&s, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&s, &QLocalSocket::readyRead, [&]() {
        rx.append(s.readAll());
        for (;;) {
            QByteArray fr;
            const auto t = plugin_ipc::try_take_frame(rx, fr);
            if (t != plugin_ipc::FrameTake::Ok) { if (t == plugin_ipc::FrameTake::Bad) app.exit(3); return; }
            QDataStream ds(fr);
            ds.setVersion(plugin_ipc::kStreamVersion);
            quint8 type = 0; quint64 seq = 0;
            ds >> type;
            const auto mt = plugin_ipc::MsgType(type);
            if (mt == plugin_ipc::MsgType::Ping) {
                ds >> seq;
                QByteArray p;
                QDataStream o(&p, QIODevice::WriteOnly);
                o.setVersion(plugin_ipc::kStreamVersion);
                o << seq;
                send(plugin_ipc::MsgType::Pong, p);
            } else if (mt == plugin_ipc::MsgType::Shutdown) {
                app.quit();
            }
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
    if (mode == "crash") QTimer::singleShot(300, [] { die_now(-1); });
    else if (mode == "exit") QTimer::singleShot(300, [] { die_now(3); });
    return app.exec();
}

// ---------------------------------------------------------------- 主程序侧
static void spin_until(const std::function<bool()>& cond, int timeout_ms) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeout_ms) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
}
static void spin_for(int ms) { spin_until([] { return false; }, ms); }

static bool write_file(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

static bool make_plugin(const QString& root, const QString& tag, const QByteArray& mode, bool meta_enabled) {
    const QString d = root + QLatin1Char('/') + tag;
    QDir().mkpath(d);
    QJsonObject m;
    m.insert(QStringLiteral("api_version"), 1);
    m.insert(QStringLiteral("name"), QStringLiteral("uaf-") + tag);
    m.insert(QStringLiteral("protocol_id"), QStringLiteral("UAF_") + tag.toUpper());
    m.insert(QStringLiteral("display_name"), tag);
    m.insert(QStringLiteral("display_name_en"), tag);
    m.insert(QStringLiteral("entry"), QStringLiteral("parser.lua"));
    m.insert(QStringLiteral("runtime"), QStringLiteral("lua"));
    m.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    bool ok = write_file(d + QStringLiteral("/plugin.json"), QJsonDocument(m).toJson()) &&
              write_file(d + QStringLiteral("/parser.lua"), "-- served by host_exit_uaf_probe fake host\n") &&
              write_file(d + QStringLiteral("/mode"), mode);
    if (!meta_enabled) ok = write_file(d + QStringLiteral("/meta.json"), "{\"enabled\": false}\n") && ok;
    return ok;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--socket") == 0) return fake_host_main(argc, argv);

#ifdef PROBE_ASAN
    std::printf("asan=1\n");
#else
    std::printf("asan=0 (WARNING: non-ASan build cannot reliably detect the UAF)\n");
#endif
    QApplication app(argc, argv);
    const QString host = QCoreApplication::applicationDirPath() + QStringLiteral("/bplc-plugin-host");
    QFile::remove(host);
    check("fake host installed as bplc-plugin-host", QFile::copy(QCoreApplication::applicationFilePath(), host));
    QFile(host).setPermissions(QFile(host).permissions() | QFileDevice::ExeOwner);

    QTemporaryDir tmp;
    const QString plugins = tmp.path() + QStringLiteral("/plugins");
    bool made = make_plugin(plugins, QStringLiteral("crash"), "crash", true);
    made = make_plugin(plugins, QStringLiteral("exitc"), "exit", true) && made;
    made = make_plugin(plugins, QStringLiteral("early"), "early", true) && made;
    made = make_plugin(plugins, QStringLiteral("offlive"), "ok", true) && made;
    made = make_plugin(plugins, QStringLiteral("off"), "ok", false) && made;
    check("plugin dirs written", made);
    auto pid = [](const char* tag) { return QStringLiteral("UAF_") + QString::fromLatin1(tag).toUpper(); };
    auto dir = [&](const char* tag) { return plugins + QLatin1Char('/') + QString::fromLatin1(tag); };

    auto& pm = PluginManager::instance();
    QHash<QString, QStringList> status;
    QObject::connect(&pm, &PluginManager::plugin_status_changed,
                     [&](const QString& id, const QString& st) { status[id] << st; });
    auto st = [&](const char* tag) { return status.value(pid(tag)).join(QStringLiteral(" | ")); };
    // 停用原因可能是 "host process error"/"host process exited unexpectedly"(QProcess 信号先到,
    // 即修复前的 UAF 路径)或 "host disconnected"(IPC 线程先看到 socket 断开),两者都算
    auto ready_then_disabled = [&](const char* tag) {
        const QStringList l = status.value(pid(tag));
        const int r = l.indexOf(QStringLiteral("ready"));
        for (int i = r + 1; r >= 0 && i < l.size(); ++i)
            if (l.at(i).startsWith(QStringLiteral("disabled: host "))) return true;
        return false;
    };
    auto disabled_seen = [&](const char* tag) {
        for (const QString& s : status.value(pid(tag)))
            if (s.startsWith(QStringLiteral("disabled: host "))) return true;
        return false;
    };
    auto procs = [&] { return pm.findChildren<QProcess*>().size(); };

    // ① 第一代宿主:crash / exit / early 各自退出
    std::printf("== round 1: hosts exit / crash under PluginManager ==\n");
    pm.load_all(plugins);
    spin_until([&] {
        return ready_then_disabled("crash") && ready_then_disabled("exitc") && disabled_seen("early") &&
               pm.is_plugin_protocol(pid("offlive"));
    }, 20000);
    check("crash: ready, then disabled after host SIGKILL (CrashExit)", ready_then_disabled("crash"),
          st("crash").toUtf8());
    check("exit: ready, then disabled after host exit(3) (NormalExit)", ready_then_disabled("exitc"),
          st("exitc").toUtf8());
    check("early: host died before handshake ⇒ disabled", disabled_seen("early") &&
          !pm.is_plugin_protocol(pid("early")), st("early").toUtf8());
    // early 没连过 socket,只可能经 QProcess 信号停用 ⇒ 每次都确定走到修复前的 delete 路径
    check("early: disabled from inside QProcess signal (UAF path exercised)",
          st("early").contains(QStringLiteral("disabled: host process")), st("early").toUtf8());
    check("offlive: healthy host ready", pm.is_plugin_protocol(pid("offlive")), st("offlive").toUtf8());
    check("off: meta.json enabled=false ⇒ never started", !pm.is_plugin_protocol(pid("off")) &&
          status.value(pid("off")).isEmpty(), st("off").toUtf8());
    spin_for(500);   // 让 deleteLater 等排队删除执行
    check("dead hosts' QProcess objects reclaimed (only offlive left)", procs() == 1,
          "QProcess children=" + QByteArray::number(procs()));

    // ② restart_all:崩溃/退出停用的复活,meta.json 手动禁用的跳过
    std::printf("== round 2: restart_all revives crash/exit-disabled, skips manually disabled ==\n");
    bool w = write_file(dir("crash") + QStringLiteral("/mode"), "ok");
    w = write_file(dir("exitc") + QStringLiteral("/mode"), "ok") && w;
    w = write_file(dir("early") + QStringLiteral("/mode"), "ok") && w;
    w = write_file(dir("offlive") + QStringLiteral("/meta.json"), "{\"enabled\": false}\n") && w;
    check("modes rewritten / offlive disabled in meta.json", w);
    status.clear();
    const PluginRestartAllResult res = pm.restart_all();
    check("restart_all counts: attempted=3 ok=3 skipped_disabled=2",
          res.attempted == 3 && res.ok == 3 && res.skipped_disabled == 2,
          QByteArray("attempted=") + QByteArray::number(res.attempted) + " ok=" + QByteArray::number(res.ok) +
              " skipped_disabled=" + QByteArray::number(res.skipped_disabled));
    spin_until([&] {
        return pm.is_plugin_protocol(pid("crash")) && pm.is_plugin_protocol(pid("exitc")) &&
               pm.is_plugin_protocol(pid("early"));
    }, 20000);
    check("crash/exit/early plugins restarted and ready",
          pm.is_plugin_protocol(pid("crash")) && pm.is_plugin_protocol(pid("exitc")) &&
              pm.is_plugin_protocol(pid("early")),
          (st("crash") + " / " + st("exitc") + " / " + st("early")).toUtf8());
    check("offlive: stopped, not revived (disabled in meta.json)",
          !pm.is_plugin_protocol(pid("offlive")) &&
              st("offlive").contains(QStringLiteral("disabled: disabled in meta.json")),
          st("offlive").toUtf8());
    check("off: still not started", !pm.is_plugin_protocol(pid("off")) && status.value(pid("off")).isEmpty());

    // ③ 第二代宿主再崩溃(restart_plugin 路径)
    std::printf("== round 3: restarted host crashes again ==\n");
    check("crash mode re-armed", write_file(dir("crash") + QStringLiteral("/mode"), "crash"));
    status.clear();
    check("restart_plugin(crash) ok", pm.restart_plugin(pid("crash")));
    spin_until([&] { return ready_then_disabled("crash"); }, 20000);
    check("crash (2nd gen): ready, then disabled after crash", ready_then_disabled("crash"), st("crash").toUtf8());
    spin_for(2500);  // 覆盖 offlive 的 terminate 收尸与 2 s 兜底 kill 定时器
    check("exitc/early still ready", pm.is_plugin_protocol(pid("exitc")) && pm.is_plugin_protocol(pid("early")));
    check("QProcess objects reclaimed (only exitc + early left)", procs() == 2,
          "QProcess children=" + QByteArray::number(procs()));

    pm.stop_all();
    check("stop_all: no QProcess left", procs() == 0, "QProcess children=" + QByteArray::number(procs()));
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    app.exec();
    QFile::remove(host);
    std::printf("\nRESULT: %s (%d failures)\n", g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
