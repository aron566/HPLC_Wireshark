/// @file main.cpp
/// @brief bplc-plugin-host 入口:用法 bplc-plugin-host --socket <name> --plugin <dir> [--parent-pid <pid>]
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>

#include "plugin_host.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace {
/// @brief 主进程是否存活(父死子亡看守用;socket 断开自杀之外的第二道保险,
///        防主进程假死/被挂起时 host 残留。pid 复用属理论竞态,可接受)
bool parent_alive(qint64 pid) {
    if (pid <= 0) return true;   // 未提供 pid:不看守
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                             static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return ok && code == STILL_ACTIVE;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;   // 存在但无权限发信号 = 还活着
#endif
}
}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("BPLC plugin host process"));
    cli.addHelpOption();
    QCommandLineOption socket_opt(QStringList{QStringLiteral("socket")},
        QStringLiteral("local socket name"), QStringLiteral("name"));
    QCommandLineOption plugin_opt(QStringList{QStringLiteral("plugin")},
        QStringLiteral("plugin directory"), QStringLiteral("dir"));
    QCommandLineOption parent_opt(QStringList{QStringLiteral("parent-pid")},
        QStringLiteral("main app pid (parent-death watchdog)"),
        QStringLiteral("pid"));
    cli.addOption(socket_opt);
    cli.addOption(plugin_opt);
    cli.addOption(parent_opt);
    cli.process(app);

    const QString socket_name = cli.value(socket_opt);
    const QString plugin_dir  = cli.value(plugin_opt);
    if (socket_name.isEmpty() || plugin_dir.isEmpty()) {
        QTextStream(stderr) << "usage: bplc-plugin-host --socket <name> --plugin <dir>\n";
        return 1;
    }

    // 父死子亡看守:主进程被强杀/崩溃后 1s 内自动退出,不留孤儿进程
    const qint64 parent_pid = cli.value(parent_opt).toLongLong();
    QTimer watchdog;
    if (parent_pid > 0 && parent_pid != QCoreApplication::applicationPid()) {
        watchdog.setInterval(1000);
        QObject::connect(&watchdog, &QTimer::timeout, &app, [parent_pid] {
            if (!parent_alive(parent_pid)) QCoreApplication::exit(4);
        });
        watchdog.start();
    }

    PluginHost host(socket_name, plugin_dir);
    QString err;
    if (!host.start(&err)) {
        QTextStream(stderr) << "plugin-host start failed: " << err << "\n";
        return 2;
    }
    return host.exec();
}
