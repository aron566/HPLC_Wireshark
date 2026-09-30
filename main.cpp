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
#include "plugin_manager.h"
#include <QApplication>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QLocale>
#include <QStyleHints>
#include <QGuiApplication>
#include <cstring>

// 隐藏自测钩子(仅 CI 崩溃可用性验证用,不对外文档):
// --self-crash-test 在崩溃处理器安装后,于本函数内触发确定性空指针崩溃,
// 使生成的 dump 能被符号化定位到 crash_selftest_trigger(main.cpp 行号),
// 证明发布包的崩溃 dump 是"可定位"的,而不只是结构有效。
//
// 注意:不能写成"局部 volatile int* p=nullptr; *p=..",
// -O2 能证明 p 为 null 并把这次解引用当 UB 整个删掉(2026-09-29 实测,
// 函数直接返回,进程不崩溃)。空指针必须经 volatile 全局做运行时 load,
// 编译器在编译期无法证明其为 null,不敢删除访存指令;
// 运行时该地址恒为 null,稳定触发 SIGSEGV。
#if defined(_MSC_VER)
#define CRASH_TEST_NOINLINE __declspec(noinline)
#else
#define CRASH_TEST_NOINLINE __attribute__((noinline))
#endif
static volatile void* volatile g_crash_test_addr = nullptr;
static CRASH_TEST_NOINLINE void crash_selftest_trigger() {
    *(volatile int*)g_crash_test_addr = 0xdead; // 必 SIGSEGV
}
static bool has_self_crash_flag(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--self-crash-test") == 0)
            return true;
    return false;
}

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
    QCoreApplication::setApplicationVersion("1.3.0");

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

    // CI 崩溃可用性验证:确定性崩溃,便于符号化定位到 crash_selftest_trigger
    if (has_self_crash_flag(argc, argv))
        crash_selftest_trigger();

    decide_language();
    theme::apply(appcfg::theme());   // 主题(config.ini [general] theme,auto 默认)
    // 跟随系统:auto 模式下 Windows 深浅色切换时即时重应用主题
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                     [] { if (appcfg::theme() == QLatin1String("auto")) theme::apply(QStringLiteral("auto")); });

    MainWindow w;
    w.show();

    // 插件系统:扫描 plugins/ 并启动插件宿主进程(失败不影响主程序)
    PluginManager::instance().loadAll(
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));

    const int rc = app.exec();
    CrashHandler::shutdown();
    return rc;
}
