/// @file replay_gui_main.cpp
/// @brief 真实帧回灌 GUI 工具入口
#include <QApplication>
#include <QCommandLineParser>
#include <QLocale>

#include "replay_gui.h"
#include "i18n.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QCommandLineParser p;
    p.addOption(QCommandLineOption("en", "English UI"));
    p.addOption(QCommandLineOption("zh", "Chinese UI"));
    p.process(app);

    replay_gui_register_en();
    // 默认跟随系统语言:非中文环境自动切英文;--en/--zh 可强制
    const bool sys_zh = QLocale::system().language() == QLocale::Chinese;
    if (p.isSet("en"))
        trl::set_enabled(true);
    else if (p.isSet("zh"))
        trl::set_enabled(false);
    else
        trl::set_enabled(!sys_zh);

    ReplayMainWindow w;
    w.show();
    return app.exec();
}
