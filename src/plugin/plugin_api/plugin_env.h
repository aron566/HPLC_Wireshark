/// @file plugin_env.h
/// @brief 插件公共环境变量 + 插件独立设置(settings.json)读写
/// @details 宿主向所有脚本插件提供一组公共环境变量,脚本经
///   host.getEnv(name) 读取(见 docs/HOST_API.md)。插件独立设置由
///   plugin.json "settings" 声明 schema,用户在市场对话框"设置"页
///   修改,落盘为插件目录下 settings.json,脚本经
///   host.getSetting(key, defaultValue) 读取。
#ifndef BPLC_PLUGIN_ENV_H
#define BPLC_PLUGIN_ENV_H

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QStandardPaths>
#include <QString>
#include <QVariant>

/// @brief 一条公共环境变量的描述
struct PluginEnvVar {
    QString name;
    QString title;         ///< 中文名
    QString title_en;
    QString description;   ///< 中文说明
    QString description_en;

    QString localized_title(bool english) const {
        if (english && !title_en.isEmpty()) return title_en;
        return title;
    }
    QString localized_desc(bool english) const {
        if (english && !description_en.isEmpty()) return description_en;
        return description;
    }
};

/// @brief 公共环境变量清单(描述部分;取值见 plugin_env_value)
inline QList<PluginEnvVar> plugin_common_env_vars() {
    return {
        {QStringLiteral("BPLC_APP_VERSION"),
         QStringLiteral("宿主程序版本"), QStringLiteral("Host app version"),
         QStringLiteral("当前运行的上位机版本号,如 1.3.0"),
         QStringLiteral("Version of the running host app, e.g. 1.3.0")},
        {QStringLiteral("BPLC_API_VERSION"),
         QStringLiteral("插件 API 版本"), QStringLiteral("Plugin API version"),
         QStringLiteral("插件接口版本号,当前为 1"),
         QStringLiteral("Plugin API version, currently 1")},
        {QStringLiteral("BPLC_PLUGIN_DIR"),
         QStringLiteral("本插件目录"), QStringLiteral("This plugin's directory"),
         QStringLiteral("当前插件的安装目录绝对路径"),
         QStringLiteral("Absolute path of the current plugin's directory")},
        {QStringLiteral("BPLC_DATA_DIR"),
         QStringLiteral("应用数据目录"), QStringLiteral("App data directory"),
         QStringLiteral("宿主应用数据根目录(QStandardPaths::AppDataLocation)"),
         QStringLiteral("Host app data root (QStandardPaths::AppDataLocation)")},
        {QStringLiteral("BPLC_LANG"),
         QStringLiteral("界面语言"), QStringLiteral("UI language"),
         QStringLiteral("当前界面语言: zh 或 en"),
         QStringLiteral("Current UI language: zh or en")},
        {QStringLiteral("BPLC_THEME"),
         QStringLiteral("界面主题"), QStringLiteral("UI theme"),
         QStringLiteral("当前界面主题: dark 或 light"),
         QStringLiteral("Current UI theme: dark or light")},
    };
}

/// @brief 取公共环境变量的值(未知变量返回空串)
/// @param plugin_dir 当前插件目录(BPLC_PLUGIN_DIR 用)
/// @param english 界面是否为英文(BPLC_LANG 用,调用方传 trl::enabled())
/// @param dark 界面是否深色(BPLC_THEME 用)
inline QString plugin_env_value(const QString& name,
                                const QString& plugin_dir, bool english,
                                bool dark = true) {
    if (name == QStringLiteral("BPLC_APP_VERSION"))
        return QCoreApplication::applicationVersion();
    if (name == QStringLiteral("BPLC_API_VERSION"))
        return QStringLiteral("1");
    if (name == QStringLiteral("BPLC_PLUGIN_DIR"))
        return QDir(plugin_dir).absolutePath();
    if (name == QStringLiteral("BPLC_DATA_DIR"))
        return QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
    if (name == QStringLiteral("BPLC_LANG"))
        return english ? QStringLiteral("en") : QStringLiteral("zh");
    if (name == QStringLiteral("BPLC_THEME"))
        return dark ? QStringLiteral("dark") : QStringLiteral("light");
    return QString();
}

/// @brief 读插件独立设置(settings.json 中的 key,不存在返回 defaultValue)
inline QVariant plugin_setting_value(const QString& plugin_dir,
                                     const QString& key,
                                     const QVariant& default_value = QVariant()) {
    if (plugin_dir.isEmpty() || key.isEmpty()) return default_value;
    QFile f(QDir(plugin_dir).filePath(QStringLiteral("settings.json")));
    if (!f.open(QIODevice::ReadOnly)) return default_value;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return default_value;
    const QJsonValue v = doc.object().value(key);
    if (v.isUndefined() || v.isNull()) return default_value;
    return v.toVariant();
}

/// @brief 写插件独立设置(整表落盘 settings.json)
inline bool plugin_write_settings(const QString& plugin_dir,
                                  const QVariantMap& values, QString* err) {
    if (plugin_dir.isEmpty()) {
        if (err) *err = QStringLiteral("empty plugin dir");
        return false;
    }
    QFile f(QDir(plugin_dir).filePath(QStringLiteral("settings.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err)
            *err = QStringLiteral("cannot write settings.json: ") +
                   f.errorString();
        return false;
    }
    f.write(QJsonDocument(QJsonObject::fromVariantMap(values)).toJson());
    return true;
}

#endif  // BPLC_PLUGIN_ENV_H
