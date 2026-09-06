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
        "\n"
        "[reader]\n"
        "; 输入源:0=串口 1=文件回放(bin) 2=裸hex文本\n"
        "mode=0\n"
        "; 串口参数\n"
        "com=COM3\n"
        "baud=460800\n"
        "; 回放/裸hex 文件路径\n"
        "file_path=\n"
        "; 回放文件是否带 8B BCD 时间标签(自动识别时无需勾选)\n"
        "time_tag=false\n";
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

// ---- reader ----
inline int  reader_mode()      { return settings().value(QStringLiteral("reader/mode"), 0).toInt(); }
inline QString reader_com()    { return settings().value(QStringLiteral("reader/com"), QStringLiteral("COM3")).toString(); }
inline int  reader_baud()      { return settings().value(QStringLiteral("reader/baud"), 460800).toInt(); }
inline QString reader_file()   { return settings().value(QStringLiteral("reader/file_path")).toString(); }
inline bool reader_time_tag()  { return settings().value(QStringLiteral("reader/time_tag"), false).toBool(); }

inline void set_reader(int mode, const QString& com, int baud,
                       const QString& file, bool time_tag) {
    QSettings s = settings();
    s.setValue(QStringLiteral("reader/mode"), mode);
    s.setValue(QStringLiteral("reader/com"), com);
    s.setValue(QStringLiteral("reader/baud"), baud);
    s.setValue(QStringLiteral("reader/file_path"), file);
    s.setValue(QStringLiteral("reader/time_tag"), time_tag);
}

}  // namespace appcfg

#endif // APPCONFIG_H
