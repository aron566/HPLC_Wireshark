/// @file appconfig.h
/// @brief 应用配置文件(config.ini,exe 同目录,Qt IniFormat)
/// @details 首次启动自动生成带注释模板;后续由本模块统一读写。
///          承载:更新检查地址、语言、串口/回放参数、显示过滤器等。
///          兼容旧注册表:仅在 config.ini 不存在且注册表有值时迁移一次。
#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QFile>
#include <QByteArray>

namespace appcfg {

/// @brief config.ini 完整路径(exe 同目录)
inline QString ini_path() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/config.ini");
}

/// @brief 配置文件读写句柄(IniFormat,UTF-8)
inline QSettings settings() { return QSettings(ini_path(), QSettings::IniFormat); }

/// @brief 初次启动生成默认配置文件(带中文注释模板);已存在则不动
inline void ensure_default_file() {
    const QString path = ini_path();
    if (QFile::exists(path)) return;
    const QByteArray tmpl =
        "; BPLC STA Monitor 配置文件(首次启动自动生成,删除后下次启动重建)\n"
        "; BPLC STA Monitor configuration (auto-generated)\n"
        "\n"
        "[general]\n"
        "; 语言:auto=跟随系统 / zh=中文 / en=English\n"
        "lang=auto\n"
        "; 更新检查地址(update.json 清单)\n"
        "update_url=https://raw.githubusercontent.com/aron566/HPLC_Wireshark/main/update.json\n"
        "; 显示过滤器(启动时自动应用,留空=不过滤)\n"
        "filter=\n"
        "; 界面主题:auto=跟随系统(默认)/ dark=深色 / light=浅色\n"
        "theme=auto\n"
        "; 启动时自动检查更新(true=检查 / false=不检查)\n"
        "auto_check=true\n"
        "; 协议:gw_2022=国网双模标准2022 / nw_2021=南网双模2021报批版\n"
        "protocol=gw_2022\n"
        "\n"
        "[reader]\n"
        "; 输入源:0=串口 1=文件回放(bin) 2=裸hex文本\n"
        "mode=0\n"
        "; 串口参数\n"
        "com=COM3\n"
        "baud=460800\n"
        "; 回放/裸hex 文件路径\n"
        "file_path=\n"
        "\n"
        "[crash]\n"
        "; 崩溃捕获后端:sentry=上报Sentry服务 / crashpad=仅本地minidump / auto=自动(默认)\n"
        "; 编译时用 qmake CONFIG+=crash_sentry / CONFIG+=crash_crashpad 选择包含的后端\n"
        "backend=auto\n"
        "; Sentry DSN(backend=sentry 时上报用);留空则只本地落盘不上报\n"
        "dsn=\n"
        "; 本地 dump 目录(留空=exe 同目录 crashpad_db)\n"
        "db_path=\n";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(tmpl);
}

// ---- general ----
inline QString update_url() {
    ensure_default_file();
    return settings().value(QStringLiteral("general/update_url"),
        QStringLiteral("https://raw.githubusercontent.com/aron566/HPLC_Wireshark/main/update.json"))
        .toString();
}
inline void set_update_url(const QString& url) {
    settings().setValue(QStringLiteral("general/update_url"), url);
}

inline QString lang() {
    ensure_default_file();
    return settings().value(QStringLiteral("general/lang"), QStringLiteral("auto")).toString();
}
inline void set_lang(const QString& v) {
    settings().setValue(QStringLiteral("general/lang"), v);
}

inline QString filter() {
    ensure_default_file();
    return settings().value(QStringLiteral("general/filter"), QString()).toString();
}
inline void set_filter(const QString& f) {
    settings().setValue(QStringLiteral("general/filter"), f);
}

inline QString theme() {
    ensure_default_file();
    return settings().value(QStringLiteral("general/theme"), QStringLiteral("auto")).toString();
}
inline void set_theme(const QString& t) {
    settings().setValue(QStringLiteral("general/theme"), t);
}

inline bool auto_check() {
    ensure_default_file();
    return settings().value(QStringLiteral("general/auto_check"), true).toBool();
}
inline void set_auto_check(bool v) {
    settings().setValue(QStringLiteral("general/auto_check"), v);
}

/// 协议变体:gw_2022=国网双模标准2022 / nw_2021=南网双模2021报批版
inline QString protocol() {
    ensure_default_file();
    return settings().value(QStringLiteral("general/protocol"),
                            QStringLiteral("gw_2022")).toString();
}
inline void set_protocol(const QString& v) {
    settings().setValue(QStringLiteral("general/protocol"), v);
}

// ---- reader ----
inline int  reader_mode()      { return settings().value(QStringLiteral("reader/mode"), 0).toInt(); }
inline QString reader_com()    { return settings().value(QStringLiteral("reader/com"), QStringLiteral("COM3")).toString(); }
inline int  reader_baud()      { return settings().value(QStringLiteral("reader/baud"), 460800).toInt(); }
inline QString reader_file()   { return settings().value(QStringLiteral("reader/file_path")).toString(); }

// ---- crash ----
inline QString crash_backend() {
    const QString v = settings().value(QStringLiteral("crash/backend"),
                                       QStringLiteral("auto")).toString().trimmed().toLower();
    return (v == QLatin1String("sentry") || v == QLatin1String("crashpad"))
               ? v : QStringLiteral("auto");
}
inline QString crash_dsn() {
    // 环境变量 SENTRY_DSN 优先,便于 CI/测试覆盖
    const QByteArray env = qgetenv("SENTRY_DSN");
    if (!env.isEmpty()) return QString::fromUtf8(env).trimmed();
    return settings().value(QStringLiteral("crash/dsn")).toString().trimmed();
}
inline QString crash_db_path() {
    return settings().value(QStringLiteral("crash/db_path")).toString().trimmed();
}

inline void set_reader(int mode, const QString& com, int baud,
                       const QString& file) {
    QSettings s = settings();
    s.setValue(QStringLiteral("reader/mode"), mode);
    s.setValue(QStringLiteral("reader/com"), com);
    s.setValue(QStringLiteral("reader/baud"), baud);
    s.setValue(QStringLiteral("reader/file_path"), file);
}

}  // namespace appcfg

#endif // APPCONFIG_H
