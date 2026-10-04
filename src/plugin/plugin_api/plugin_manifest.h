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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariant>

/// @brief 插件可配置项(插件独立设置,见 market 对话框"设置"页)
/// @details plugin.json 可选 "settings" 数组,每项:
///   { "key": "max_nodes",            // 设置键(唯一,写 settings.json 用)
///     "type": "integer",             // boolean | integer | number | string
///     "default": 200,                // 缺省值
///     "enum": ["a","b"],             // 可选:字符串枚举候选(type=string 时)
///     "minimum": 1, "maximum": 9999, // 可选:数值范围
///     "title": "最大节点数", "title_en": "Max nodes",
///     "description": "...", "description_en": "..." }
///   脚本侧经 host.getSetting(key, defaultValue) 读取(见 docs/HOST_API.md)。
struct PluginSetting {
    QString key;
    QString type;              ///< boolean | integer | number | string
    QVariant default_value;
    QStringList enum_options;  ///< type=string 时的候选项(可空)
    double minimum = 0;
    double maximum = 0;
    bool has_minimum = false;
    bool has_maximum = false;
    QString title;
    QString title_en;
    QString description;
    QString description_en;

    QString localized_title(bool english) const {
        if (english && !title_en.isEmpty()) return title_en;
        return title.isEmpty() ? key : title;
    }
    QString localized_desc(bool english) const {
        if (english && !description_en.isEmpty()) return description_en;
        return description;
    }
};

/// @brief 插件清单
struct PluginManifest {
    QString name;
    QString version;
    QString runtime;       ///< native | lua | js
    QString abi;           ///< native 插件的 ABI 标识(如 qt6.10.1-mingw-x64),须与主程序 plugin_host_abi() 一致
    QString entry;         ///< 入口文件名(相对插件目录)
    int     api_version = 0;
    QString protocol_id;   ///< 解析器插件的协议标识
    QString display_name;
    QString display_name_en;  ///< 英文显示名(可选,缺省回退 display_name)
    QString description;
    QString description_en;   ///< 英文描述(可选,缺省回退 description)
    QString author;
    bool    graphics = false;  ///< 是否提供图形能力(Phase3)
    QString panel;           ///< 主界面功能面板类型(可选): topo|replay|diag|report|stats
    QString panel_function;  ///< 文本类面板调用的脚本函数名(可选,如 get_replay_data)
    QList<PluginSetting> settings; ///< 插件独立可配置项(可选,见 PluginSetting)
    QString dir_path;      ///< 插件目录绝对路径(解析时填充)
    bool    valid = false;
    QString error;         ///< valid=false 时的原因
};

/// @brief 主程序 native 插件 ABI 标识(编译器 + Qt 版本 + 架构)
/// @details native 插件是编译产物,链接特定 Qt 版本与编译器工具链,其二进制
///   ABI 必须与主程序一致,否则 QMetaObject 布局/符号修饰可能不兼容,加载时
///   可能崩溃。插件 plugin.json 的 "abi" 字段须与此值一致(见 docs/MARKET.md)。
inline QString plugin_host_abi() {
    const char* comp =
#if defined(Q_CC_MSVC)
        "msvc";
#elif defined(Q_CC_GNU)
        "mingw";
#elif defined(Q_CC_CLANG)
        "clang";
#else
        "unknown";
#endif
    const char* arch =
#if defined(Q_PROCESSOR_X86_64)
        "x64";
#elif defined(Q_PROCESSOR_ARM_64)
        "arm64";
#else
        "unknown";
#endif
    return QStringLiteral("qt%1-%2-%3")
        .arg(QString::fromLatin1(QT_VERSION_STR),
             QString::fromLatin1(comp), QString::fromLatin1(arch));
}

/// @brief native 插件 ABI 是否与主程序匹配(脚本插件无需 ABI,恒 true)
inline bool plugin_abi_ok(const PluginManifest& m) {
    if (m.runtime != QStringLiteral("native")) return true;
    return !m.abi.isEmpty() && m.abi == plugin_host_abi();
}

/// @brief native 插件 entry 平台解析
/// entry 为显式文件名(含 .so/.dll/.dylib 后缀)时直接使用(向后兼容);
/// 否则视为裸库名,按平台补全:Windows→<entry>.dll,Unix→lib<entry>.so
inline QString plugin_resolve_native_entry(const QString& plugin_dir,
                                           const QString& entry) {
    const QString e = entry.trimmed();
    if (e.endsWith(QStringLiteral(".so"), Qt::CaseInsensitive) ||
        e.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive) ||
        e.endsWith(QStringLiteral(".dylib"), Qt::CaseInsensitive))
        return QDir(plugin_dir).filePath(e);
#ifdef Q_OS_WIN
    return QDir(plugin_dir).filePath(e + QStringLiteral(".dll"));
#else
    const QString with_lib = QDir(plugin_dir).filePath(
        QStringLiteral("lib") + e + QStringLiteral(".so"));
    if (QFile::exists(with_lib)) return with_lib;
    return QDir(plugin_dir).filePath(e + QStringLiteral(".so"));
#endif
}

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
    m.abi          = o.value(QStringLiteral("abi")).toString();
    m.entry        = o.value(QStringLiteral("entry")).toString();
    m.api_version  = o.value(QStringLiteral("api_version")).toInt(0);
    m.protocol_id  = o.value(QStringLiteral("protocol_id")).toString();
    m.display_name = o.value(QStringLiteral("display_name")).toString(m.name);
    m.display_name_en = o.value(QStringLiteral("display_name_en")).toString(m.display_name);
    m.description  = o.value(QStringLiteral("description")).toString();
    m.description_en = o.value(QStringLiteral("description_en")).toString(m.description);
    m.author       = o.value(QStringLiteral("author")).toString();
    m.graphics     = o.value(QStringLiteral("graphics")).toBool(false);
    m.panel        = o.value(QStringLiteral("panel")).toString();
    m.panel_function = o.value(QStringLiteral("panel_function")).toString();
    for (const QJsonValue& sv : o.value(QStringLiteral("settings")).toArray()) {
        const QJsonObject so = sv.toObject();
        PluginSetting ps;
        ps.key = so.value(QStringLiteral("key")).toString();
        ps.type = so.value(QStringLiteral("type")).toString();
        if (ps.key.isEmpty()) continue;
        if (ps.type != QStringLiteral("boolean") &&
            ps.type != QStringLiteral("integer") &&
            ps.type != QStringLiteral("number") &&
            ps.type != QStringLiteral("string"))
            continue;  // 未知类型:跳过该项
        ps.default_value = so.value(QStringLiteral("default")).toVariant();
        for (const QJsonValue& ev : so.value(QStringLiteral("enum")).toArray())
            ps.enum_options.append(ev.toString());
        if (so.contains(QStringLiteral("minimum"))) {
            ps.minimum = so.value(QStringLiteral("minimum")).toDouble();
            ps.has_minimum = true;
        }
        if (so.contains(QStringLiteral("maximum"))) {
            ps.maximum = so.value(QStringLiteral("maximum")).toDouble();
            ps.has_maximum = true;
        }
        ps.title = so.value(QStringLiteral("title")).toString();
        ps.title_en = so.value(QStringLiteral("title_en")).toString();
        ps.description = so.value(QStringLiteral("description")).toString();
        ps.description_en = so.value(QStringLiteral("description_en")).toString();
        m.settings.append(ps);
    }

    if (m.name.isEmpty())         { m.error = QStringLiteral("missing 'name'"); return m; }
    if (m.runtime.isEmpty())      { m.error = QStringLiteral("missing 'runtime'"); return m; }
    if (m.entry.isEmpty())        { m.error = QStringLiteral("missing 'entry'"); return m; }
    if (m.api_version <= 0)       { m.error = QStringLiteral("missing/invalid 'api_version'"); return m; }
    if (m.protocol_id.isEmpty())  { m.error = QStringLiteral("missing 'protocol_id'"); return m; }
    // native 插件允许 entry 为裸库名(按平台解析);脚本插件 entry 必须为确切文件名
    const QString entry_path =
        (m.runtime == QStringLiteral("native"))
            ? plugin_resolve_native_entry(plugin_dir, m.entry)
            : QDir(plugin_dir).filePath(m.entry);
    if (!QFile::exists(entry_path)) {
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
