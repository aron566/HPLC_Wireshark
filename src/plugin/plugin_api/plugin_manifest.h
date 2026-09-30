/// @file plugin_manifest.h
/// @brief plugin.json 清单解析(主程序 PluginManager 与 plugin-host 共用)
/// @details 清单格式:
///   {
///     "name": "my-proto",            // 唯一标识(小写,目录名一致)
///     "version": "1.0.0",            // 语义化版本
///     "runtime": "native",           // native | lua | js (Phase1 仅 native)
///     "entry": "libmyproto.so",      // 入口文件(相对插件目录)
///     "api_version": 1,              // 插件 API 版本(须与主程序一致)
///     "protocol_id": "MYPROTO_2024", // 协议唯一标识(解析器插件必填)
///     "display_name": "My Protocol", // 显示名(中文界面用)
///     "display_name_en": "My Protocol", // 显示名英文(可选,缺省回退 display_name)
///     "description": "...",          // 描述(可选,中文界面用)
///     "description_en": "...",       // 描述英文(可选,缺省回退 description)
///     "author": "...",               // 作者(可选)
///     "graphics": false              // 是否提供图形能力(Phase3,可选)
///   }
#ifndef BPLC_PLUGIN_MANIFEST_H
#define BPLC_PLUGIN_MANIFEST_H

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

/// @brief 插件清单
struct PluginManifest {
    QString name;
    QString version;
    QString runtime;       ///< native | lua | js
    QString entry;         ///< 入口文件名(相对插件目录)
    int     api_version = 0;
    QString protocol_id;   ///< 解析器插件的协议标识
    QString display_name;
    QString display_name_en;  ///< 英文显示名(可选,缺省回退 display_name)
    QString description;
    QString description_en;   ///< 英文描述(可选,缺省回退 description)
    QString author;
    bool    graphics = false;  ///< 是否提供图形能力(Phase3)
    QString dir_path;      ///< 插件目录绝对路径(解析时填充)
    bool    valid = false;
    QString error;         ///< valid=false 时的原因
};

/// @brief 从插件目录读取并校验 plugin.json
inline PluginManifest read_plugin_manifest(const QString& plugin_dir) {
    PluginManifest m;
    m.dir_path = QDir(plugin_dir).absolutePath();

    QFile f(QDir(plugin_dir).filePath(QStringLiteral("plugin.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        m.error = QStringLiteral("cannot open plugin.json");
        return m;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        m.error = QStringLiteral("plugin.json parse error: %1").arg(pe.errorString());
        return m;
    }
    const QJsonObject o = doc.object();
    m.name         = o.value(QStringLiteral("name")).toString();
    m.version      = o.value(QStringLiteral("version")).toString();
    m.runtime      = o.value(QStringLiteral("runtime")).toString();
    m.entry        = o.value(QStringLiteral("entry")).toString();
    m.api_version  = o.value(QStringLiteral("api_version")).toInt(0);
    m.protocol_id  = o.value(QStringLiteral("protocol_id")).toString();
    m.display_name = o.value(QStringLiteral("display_name")).toString(m.name);
    m.display_name_en = o.value(QStringLiteral("display_name_en")).toString(m.display_name);
    m.description  = o.value(QStringLiteral("description")).toString();
    m.description_en = o.value(QStringLiteral("description_en")).toString(m.description);
    m.author       = o.value(QStringLiteral("author")).toString();
    m.graphics     = o.value(QStringLiteral("graphics")).toBool(false);

    if (m.name.isEmpty())         { m.error = QStringLiteral("missing 'name'"); return m; }
    if (m.runtime.isEmpty())      { m.error = QStringLiteral("missing 'runtime'"); return m; }
    if (m.entry.isEmpty())        { m.error = QStringLiteral("missing 'entry'"); return m; }
    if (m.api_version <= 0)       { m.error = QStringLiteral("missing/invalid 'api_version'"); return m; }
    if (m.protocol_id.isEmpty())  { m.error = QStringLiteral("missing 'protocol_id'"); return m; }
    if (!QFile::exists(QDir(plugin_dir).filePath(m.entry))) {
        m.error = QStringLiteral("entry file not found: %1").arg(m.entry);
        return m;
    }
    m.valid = true;
    return m;
}

/// @brief 按是否英文取插件显示名(缺省回退中文名)
inline QString plugin_display_name(const PluginManifest& m, bool english) {
    if (english && !m.display_name_en.isEmpty()) return m.display_name_en;
    return m.display_name;
}

/// @brief 按是否英文取插件描述(缺省回退中文描述)
inline QString plugin_description(const PluginManifest& m, bool english) {
    if (english && !m.description_en.isEmpty()) return m.description_en;
    return m.description;
}

#endif // BPLC_PLUGIN_MANIFEST_H
