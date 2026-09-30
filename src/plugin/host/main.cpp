/// @file main.cpp
/// @brief bplc-plugin-host 入口:用法 bplc-plugin-host --socket <name> --plugin <dir>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>

#include "plugin_host.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("BPLC plugin host process"));
    cli.addHelpOption();
    QCommandLineOption socket_opt(QStringList{QStringLiteral("socket")},
        QStringLiteral("local socket name"), QStringLiteral("name"));
    QCommandLineOption plugin_opt(QStringList{QStringLiteral("plugin")},
        QStringLiteral("plugin directory"), QStringLiteral("dir"));
    cli.addOption(socket_opt);
    cli.addOption(plugin_opt);
    cli.process(app);

    const QString socket_name = cli.value(socket_opt);
    const QString plugin_dir  = cli.value(plugin_opt);
    if (socket_name.isEmpty() || plugin_dir.isEmpty()) {
        QTextStream(stderr) << "usage: bplc-plugin-host --socket <name> --plugin <dir>\n";
        return 1;
    }

    PluginHost host(socket_name, plugin_dir);
    QString err;
    if (!host.start(&err)) {
        QTextStream(stderr) << "plugin-host start failed: " << err << "\n";
        return 2;
    }
    return host.exec();
}
