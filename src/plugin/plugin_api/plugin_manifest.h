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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QSysInfo>
#include <QVariant>
#include <cstdint>

// 结构体布局指纹:native 插件 parse 接口涉及的公共结构体(PhysicalMeta/
// BplcFrame/ParseResult/MsduInfo)若字段增删或类型变化,sizeof 随之改变,
// 纳入 plugin_host_abi() 以便主程序在加载前拦截 ABI 不匹配的旧 DLL。
// __has_include 兜底:无这些头文件的编译单元(纯 Qt 工具)退回基础 ABI。
#if defined(__has_include)
#  if __has_include("bplcframe.h") && __has_include("iprotocolparser.h")
#    include "bplcframe.h"
#    include "iprotocolparser.h"
#    define BPLC_ABI_HAS_LAYOUT 1
#  else
#    define BPLC_ABI_HAS_LAYOUT 0
#  endif
#else
#  define BPLC_ABI_HAS_LAYOUT 0
#endif

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
    /// 市场层启用开关:同目录 meta.json 的 enabled,缺失默认 true。
    /// 注意:这是用户主动禁用的开关,重启/加载时以它为准;不要与
    /// PluginRuntime::disabled(崩溃/断连后内部置位,重启应复活)混淆。
    bool    enabled = true;
    bool    valid = false;
    QString error;         ///< valid=false 时的原因
};

/// @brief 主程序运行平台标识 "<os>-<arch>"(市场 feed platforms / plugin.json 按平台 abi 的键)
/// @details os = windows | macos | linux;arch 取**编译目标**架构(不是宿主 CPU):
///   x86_64 | arm64,其它架构回落 QSysInfo::buildCpuArchitecture()(如 i386 / riscv64)。
///   按编译目标取值:Windows on ARM 上跑 x64 仿真版时仍需 x86_64 插件。
///   取值如 windows-x86_64 / windows-arm64 / linux-x86_64 / linux-arm64 /
///   macos-x86_64 / macos-arm64。
inline QString plugin_host_platform() {
    const QString os =
#if defined(Q_OS_WIN)
        QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
        QStringLiteral("macos");
#else
        QStringLiteral("linux");
#endif
    const QString arch =
#if defined(Q_PROCESSOR_X86_64)
        QStringLiteral("x86_64");
#elif defined(Q_PROCESSOR_ARM_64)
        QStringLiteral("arm64");
#else
        QSysInfo::buildCpuArchitecture();
#endif
    return os + QLatin1Char('-') + arch;
}

/// @brief 主程序 native 插件 ABI 标识(编译器 + Qt 大版本 + 架构)
/// @details native 插件是编译产物,其二进制 ABI 由「Qt 大版本 + 编译器 + 架构」决定。
///   Qt 6.x 系列保证二进制兼容(6.10 与 6.11 可互载),故只用大版本号;5↔6 或
///   MinGW↔MSVC 才真正不兼容。插件 plugin.json 的 "abi" 字段须与此值一致。
inline QString plugin_host_abi() {
    const char* comp =
#if defined(Q_CC_MSVC)
        "msvc";
#elif defined(Q_OS_WIN) && defined(Q_CC_GNU)
        "mingw";   // Windows MinGW (GCC-based, Q_CC_GNU also defined)
#elif defined(Q_CC_GNU)
        "gcc";     // Linux GCC
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
    QString abi = QStringLiteral("qt%1-%2-%3")
        .arg(QString::number(QT_VERSION_MAJOR),
             QString::fromLatin1(comp), QString::fromLatin1(arch));
#if BPLC_ABI_HAS_LAYOUT
    // 结构体布局指纹(sizeof 十六进制拼接):字段增删/类型变化 → sizeof 变 → ABI 变,
    // 主程序加载 native 插件前用 plugin_abi_ok() 比对,布局不一致的旧 DLL 直接拒绝。
    const quint64 layout =
        (static_cast<quint64>(sizeof(BplcFrame)) << 48) |
        (static_cast<quint64>(sizeof(ParseResult)) << 32) |
        (static_cast<quint64>(sizeof(PhysicalMeta)) << 16) |
        (static_cast<quint64>(sizeof(MsduInfo)));
    abi += QStringLiteral("-l%1").arg(QString::number(layout, 16));
#endif
    return abi;
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

/// @brief 插件入口路径解析 + 目录包含检查(三种 runtime 共用)
/// @details 先做词法检查(拒绝空、绝对路径、盘符/':'、'\\'、NUL、'..' 段),
///          再按 canonical 路径(解析符号链接)确认入口是插件目录内的普通文件。
///          任何一步不通过返回空串;调用方据此拒绝加载。
inline QString plugin_contained_entry_path(const QString& plugin_dir,
                                           const QString& runtime,
                                           const QString& entry) {
    const QString e = entry.trimmed();
    if (e.isEmpty() || e.contains(QChar(0)) || e.contains(QLatin1Char('\\')) ||
        e.contains(QLatin1Char(':')) || e.startsWith(QLatin1Char('/')) ||
        QDir::isAbsolutePath(e))
        return {};
    for (const QString& part : e.split(QLatin1Char('/')))
        if (part == QStringLiteral("..")) return {};
    const QString candidate = (runtime == QStringLiteral("native"))
                                  ? plugin_resolve_native_entry(plugin_dir, e)
                                  : QDir(plugin_dir).filePath(e);
    const QString dir_canon = QFileInfo(plugin_dir).canonicalFilePath();
    const QFileInfo fi(candidate);
    const QString file_canon = fi.canonicalFilePath();   // 不存在时为空
    if (dir_canon.isEmpty() || file_canon.isEmpty() || !fi.isFile()) return {};
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString prefix = dir_canon.endsWith(QLatin1Char('/')) ? dir_canon
                                                                : dir_canon + QLatin1Char('/');
    if (!file_canon.startsWith(prefix, cs)) return {};
    return file_canon;
}

/// @brief Windows 保留设备名(大小写不敏感):CON/PRN/AUX/NUL/COM1-9/LPT1-9。
/// @details 名字会成为安装目录名,Windows 上建同名目录失败;带后缀也一样
///          (如 CON.txt 的主名仍是保留名,故比对去后缀的主名,并去掉末尾的
///          '.'/' ')。COM10/LPT10 这类两位数不在保留名之列,不要误杀。
inline bool plugin_name_is_reserved(const QString& name) {
    QString stem = name;
    // 按第一个点拆分:Windows 保留名判定看的是主名,而主名是第一个点之前的部分。
    // 用 lastIndexOf 会把 "CON.foo.txt" 的主名当成 "CON.foo"、"com1.a.b" 当成
    // "com1.a",于是这些名字被误判为安全,在 Windows 上建目录/写文件时失败。
    const int dot = stem.indexOf(QLatin1Char('.'));
    if (dot > 0) stem.truncate(dot);
    while (!stem.isEmpty() &&
           (stem.endsWith(QLatin1Char('.')) || stem.endsWith(QLatin1Char(' '))))
        stem.chop(1);
    const QString upper = stem.toUpper();
    if (upper == QLatin1String("CON") || upper == QLatin1String("PRN") ||
        upper == QLatin1String("AUX") || upper == QLatin1String("NUL"))
        return true;
    if (upper.size() == 4 &&
        (upper.startsWith(QLatin1String("COM")) || upper.startsWith(QLatin1String("LPT")))) {
        const QChar d = upper.at(3);
        return d >= QLatin1Char('1') && d <= QLatin1Char('9');
    }
    return false;
}

/// @brief 插件名(= 安装目录名)白名单:[A-Za-z0-9][A-Za-z0-9._-]{0,63},
///        且不能是 Windows 保留设备名(plugin_name_is_reserved)。
/// @details 名字会被拼成安装目录并在覆盖安装/卸载时整目录删除,
///          必须是单级目录名:不含 '/' '\\' ':',不能是 "." / ".."(首字符须为字母数字)。
inline bool plugin_name_is_safe(const QString& name) {
    if (name.isEmpty() || name.size() > 64) return false;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        const bool alnum = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                           (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                           (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
        if (alnum) continue;
        if (i > 0 && (c == QLatin1Char('.') || c == QLatin1Char('_') ||
                      c == QLatin1Char('-')))
            continue;
        return false;
    }
    if (plugin_name_is_reserved(name)) return false;
    return true;
}

/// @brief 读插件目录下 meta.json 的 enabled(市场层启用开关)
/// @details 无 meta.json / 解析失败 / 无 enabled 键都按启用处理,
///          与 PluginMarket::plugin_dir_enabled 语义一致(后者直接调本函数)。
///          注意:这是用户主动禁用的市场层开关,不要与
///          PluginRuntime::disabled(崩溃/断连置位)混淆。
inline bool plugin_meta_enabled(const QString& plugin_dir) {
    QFile mf(QDir(plugin_dir).filePath(QStringLiteral("meta.json")));
    if (!mf.open(QIODevice::ReadOnly)) return true;  // 无 meta.json=启用
    const QJsonObject o = QJsonDocument::fromJson(mf.readAll()).object();
    return o.value(QStringLiteral("enabled")).toBool(true);
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
    // abi 支持两种写法:字符串(单平台包,向后兼容)或按平台的对象
    // {"linux-x86_64": "qt6.4.2-gcc-x64", "windows-x86_64": "qt6.10.1-mingw-x64"};
    // 按 plugin_host_platform()(如 linux-arm64)解析为当前平台的值,存入 m.abi。
    {
        const QJsonValue abi_v = o.value(QStringLiteral("abi"));
        if (abi_v.isObject()) {
            m.abi = abi_v.toObject().value(plugin_host_platform()).toString();
        } else {
            m.abi = abi_v.toString();
        }
    }
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
    if (!plugin_name_is_safe(m.name)) { m.error = QStringLiteral("invalid 'name' (allowed: [A-Za-z0-9][A-Za-z0-9._-], max 64)"); return m; }
    if (m.runtime.isEmpty())      { m.error = QStringLiteral("missing 'runtime'"); return m; }
    if (m.entry.isEmpty())        { m.error = QStringLiteral("missing 'entry'"); return m; }
    if (m.api_version <= 0)       { m.error = QStringLiteral("missing/invalid 'api_version'"); return m; }
    if (m.protocol_id.isEmpty())  { m.error = QStringLiteral("missing 'protocol_id'"); return m; }
    // native 插件允许 entry 为裸库名(按平台解析);脚本插件 entry 必须为确切文件名。
    // 入口必须是插件目录内的文件(canonical 包含检查,拒绝 ../、绝对路径、越界符号链接)
    const QString entry_path = plugin_contained_entry_path(plugin_dir, m.runtime, m.entry);
    if (entry_path.isEmpty()) {
        m.error = QStringLiteral("entry file not found or outside plugin dir: %1").arg(m.entry);
        return m;
    }
    // 市场层启用开关:同目录 meta.json 的 enabled,缺失默认 true。
    // 放在有效性校验之后读也可以(无效清单调用方直接跳过),这里顺带读出,
    // 供 PluginManager::load_all/start_plugin/restart_all 做 gate。
    m.enabled = plugin_meta_enabled(plugin_dir);
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
