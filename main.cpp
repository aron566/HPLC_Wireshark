/// @file main.cpp
/// @brief 应用入口(Qt 6.10 在 win32 GUI app 会自动链接 libQt6EntryPoint)
/// @details libQt6EntryPoint 引用 qMain 符号。<QtGui/qwindowdefs.h> 中定义了
///          `#define main qMain`,所以这里写 `int main(...)` 会被预处理为 `qMain(...)`。
///          语言决定:QSettings "lang" = auto(默认,跟随系统)/zh/en。
#include "mainwindow.h"
#include "i18n.h"
#include "appconfig.h"
#include "theme.h"
#include "crash_handler.h"
#include <QApplication>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QLocale>
#include <QStyleHints>
#include <QGuiApplication>

static void decide_language() {
    // config.ini [general] lang = auto(默认,跟随系统)/zh/en;不存在则先生成
    appcfg::ensure_default_file();
    const QString lang = appcfg::lang();
    bool en = false;
    if (lang == QLatin1String("en")) {
        en = true;
    } else if (lang == QLatin1String("zh")) {
        en = false;
    } else {  // auto:跟随系统(系统为中文 → 中文,否则英文)
        en = QLocale::system().language() != QLocale::Chinese;
    }
    trl::set_enabled(en);
}

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("ZbMonitor");
    QCoreApplication::setApplicationName("BPLC_STA_Monitor");
    QCoreApplication::setApplicationVersion("1.2.2");

    QApplication app(argc, argv);

    // 崩溃捕获:与业务解耦,失败不影响启动。后端由 qmake CONFIG
    // (crash_sentry/crash_crashpad)决定编译进哪些,运行时按 config.ini [crash] 选择。
    {
        CrashHandler::Options co;
        co.backend = appcfg::crash_backend().toStdString();
        co.dsn = appcfg::crash_dsn().toStdString();
        co.database_path = appcfg::crash_db_path().toStdString();
        co.release = QCoreApplication::applicationVersion().toStdString();
        CrashHandler::install(co);
    }

    decide_language();
    theme::apply(appcfg::theme());   // 主题(config.ini [general] theme,auto 默认)
    // 跟随系统:auto 模式下 Windows 深浅色切换时即时重应用主题
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                     [] { if (appcfg::theme() == QLatin1String("auto")) theme::apply(QStringLiteral("auto")); });

    MainWindow w;
    w.show();
    const int rc = app.exec();
    CrashHandler::shutdown();
    return rc;
}
