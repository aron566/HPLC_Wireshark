/// @file protocolfactory.cpp
/// @brief 协议解析器工厂(注册表模式) + 字段树构建器工厂实现
#include "protocolfactory.h"
#include "protocol_tree_builder.h"
#include "gw_protocol/gw_2022/gw_2022_parser.h"
#include "nw_protocol/nw_2021/nw_2021_parser.h"
#include "gw_protocol/gw_2022/gw_2022_tree.h"
#include "nw_protocol/nw_2021/nw_2021_tree.h"

#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>

namespace {
struct ParserReg {
    ParserFactoryFn fn;
    QString display_name;
};
/// 内置协议层:启动时注册,此后只读,插件无法删除
QMap<QString, ParserReg>& builtin_registry() {
    static QMap<QString, ParserReg> r;
    return r;
}
/// 插件覆盖层:插件协议(含覆盖内置同名 id 的影子),随插件装卸增删
QMap<QString, ParserReg>& override_registry() {
    static QMap<QString, ParserReg> r;
    return r;
}
QMutex& registry_mutex() {
    static QMutex m;
    return m;
}
struct BuiltinRegistrar {
    BuiltinRegistrar() { register_builtin_parsers(); }
};
BuiltinRegistrar g_builtin_registrar;  // 启动时自注册
} // namespace

void register_builtin_parsers() {
    QMutexLocker l(&registry_mutex());
    builtin_registry().insert(QStringLiteral("GW_2022"),
        ParserReg{ []() { return std::make_unique<GW_2022_Parser>(); },
                   QStringLiteral("国网 GW_2022") });
    builtin_registry().insert(QStringLiteral("NW_2021"),
        ParserReg{ []() { return std::make_unique<NW_2021_Parser>(); },
                   QStringLiteral("南网 NW_2021") });
}

void register_parser(const QString& protocol_id, ParserFactoryFn fn,
                     const QString& display_name) {
    QMutexLocker l(&registry_mutex());
    // 覆盖内置同名 id 时告警(插件有意接管内置协议是合法用法,但可能是
    // 第三方插件 protocol_id 拼写/复制错误导致的意外接管,需可观测)
    if (builtin_registry().contains(protocol_id))
        qWarning() << "plugin protocol" << protocol_id
                   << "shadows builtin parser (override layer)";
    override_registry().insert(protocol_id, ParserReg{ std::move(fn), display_name });
}

void unregister_parser(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    // 仅移除插件覆盖层;内置层不动(插件永远删不掉内置协议)
    override_registry().remove(protocol_id);
}

QStringList registered_protocol_ids() {
    QMutexLocker l(&registry_mutex());
    // 内置在前,插件覆盖层在后(影子同名 id 不重复列出)
    QStringList out = builtin_registry().keys();
    const QStringList over = override_registry().keys();
    for (const QString& id : over) {
        if (!builtin_registry().contains(id))
            out.append(id);
    }
    return out;
}

QString protocol_display_name(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    auto it = override_registry().find(protocol_id);
    if (it != override_registry().end() && !it->display_name.isEmpty())
        return it->display_name;
    auto bit = builtin_registry().find(protocol_id);
    if (bit != builtin_registry().end() && !bit->display_name.isEmpty())
        return bit->display_name;
    return protocol_id;
}

std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id) {
    ParserFactoryFn fn;
    {
        QMutexLocker l(&registry_mutex());
        auto it = override_registry().find(protocol_id);
        if (it != override_registry().end()) {
            fn = it->fn;
        } else {
            auto bit = builtin_registry().find(protocol_id);
            if (bit == builtin_registry().end()) return nullptr;
            fn = bit->fn;
        }
    }
    return fn ? fn() : nullptr;
}

std::unique_ptr<IProtocolTreeBuilder> make_tree_builder(ProtocolVariant v) {
    switch (v) {
        case ProtocolVariant::GW_2022:
            return std::make_unique<GW_2022_TreeBuilder>();
        case ProtocolVariant::NW_2021:
            return std::make_unique<NW_2021_TreeBuilder>();
    }
    return nullptr;
}
