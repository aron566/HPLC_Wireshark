/// @file main.cpp
/// @brief 应用入口(Qt 6.10 在 win32 GUI app 会自动链接 libQt6EntryPoint)
/// @details libQt6EntryPoint 引用 qMain 符号。<QtGui/qwindowdefs.h> 中定义了
///          `#define main qMain`,所以这里写 `int main(...)` 会被预处理为 `qMain(...)`。
///          语言决定:QSettings "lang" = auto(默认,跟随系统)/zh/en。
#include "mainwindow.h"
#include "i18n.h"
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QLoggingCategory>
#include <QLocale>

static void decide_language() {
    QSettings s("ZbMonitor", "BPLC_STA_Monitor");
    const QString lang = s.value("lang", "auto").toString();
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
    QCoreApplication::setApplicationVersion("1.0.3");

    QApplication app(argc, argv);
    decide_language();

    MainWindow w;
    w.show();
    return app.exec();
}
