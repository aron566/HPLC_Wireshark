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

namespace {
struct ParserReg {
    ParserFactoryFn fn;
    QString display_name;
};
QMap<QString, ParserReg>& registry() {
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

void register_builtin_parsers(const QString& only_id) {
    QMutexLocker l(&registry_mutex());
    if (only_id.isEmpty() || only_id == QLatin1String("GW_2022"))
        registry().insert(QStringLiteral("GW_2022"),
            ParserReg{ []() { return std::make_unique<GW_2022_Parser>(); },
                       QStringLiteral("国网 GW_2022") });
    if (only_id.isEmpty() || only_id == QLatin1String("NW_2021"))
        registry().insert(QStringLiteral("NW_2021"),
            ParserReg{ []() { return std::make_unique<NW_2021_Parser>(); },
                       QStringLiteral("南网 NW_2021") });
}

void register_parser(const QString& protocol_id, ParserFactoryFn fn,
                     const QString& display_name) {
    QMutexLocker l(&registry_mutex());
    registry().insert(protocol_id, ParserReg{ std::move(fn), display_name });
}

void unregister_parser(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    registry().remove(protocol_id);
}

QStringList registered_protocol_ids() {
    QMutexLocker l(&registry_mutex());
    return registry().keys();
}

QString protocol_display_name(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    auto it = registry().find(protocol_id);
    if (it == registry().end() || it->display_name.isEmpty())
        return protocol_id;
    return it->display_name;
}

std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id) {
    ParserFactoryFn fn;
    {
        QMutexLocker l(&registry_mutex());
        auto it = registry().find(protocol_id);
        if (it == registry().end()) return nullptr;
        fn = it->fn;
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
