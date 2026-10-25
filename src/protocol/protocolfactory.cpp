/// @file protocolfactory.cpp
/// @brief 协议注册表实现 + 字段树构建器工厂(按变体查注册表)
/// @details 本文件不认识任何具体协议:内置协议由各自 <proto>_provider.cpp
///          静态自注册,插件协议由 PluginManager 注册。新增协议版本时
///          本文件无需改动。
#include "protocolfactory.h"
#include "protocol_tree_builder.h"

#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

namespace {

QMap<QString, ProtocolProviderPtr>& registry() {
    static QMap<QString, ProtocolProviderPtr> r;
    return r;
}

QMutex& registry_mutex() {
    static QMutex m;
    return m;
}

/// @brief 仅提供解析器的协议(插件协议用)
/// @details 插件解析结果自带字段树(ParseResult.msdu.tree / MsduFieldNode),
///          不构造 QTreeWidget,因此 create_tree_builder 返回 nullptr;
///          变体沿用 GW_2022(与 PluginParserProxy::variant 保持一致)。
class ParserOnlyProvider : public IProtocolProvider {
public:
    ParserOnlyProvider(QString id, QString zh, QString en, int order, ParserFactoryFn fn)
        : m_id(std::move(id)), m_zh(std::move(zh)), m_en(std::move(en)),
          m_order(order), m_fn(std::move(fn)) {}

    QString id() const override { return m_id; }
    QString display_name_zh() const override { return m_zh.isEmpty() ? m_id : m_zh; }
    QString display_name_en() const override {
        return m_en.isEmpty() ? display_name_zh() : m_en;
    }
    int order() const override { return m_order; }
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }

    std::unique_ptr<IProtocolParser> create_parser() const override {
        return m_fn ? m_fn() : nullptr;
    }
    std::unique_ptr<IProtocolTreeBuilder> create_tree_builder() const override {
        return nullptr;
    }

private:
    QString         m_id;
    QString         m_zh;
    QString         m_en;
    int             m_order;
    ParserFactoryFn m_fn;
};

}  // namespace

void register_protocol(ProtocolProviderPtr provider) {
    if (!provider) return;
    const QString id = provider->id().toUpper();
    if (id.isEmpty()) return;
    QMutexLocker l(&registry_mutex());
    registry().insert(id, std::move(provider));
}

void unregister_protocol(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    registry().remove(protocol_id.toUpper());
}

QVector<ProtocolProviderPtr> registered_protocols() {
    QVector<ProtocolProviderPtr> out;
    {
        QMutexLocker l(&registry_mutex());
        out.reserve(registry().size());
        for (const ProtocolProviderPtr& p : registry())
            out.append(p);
    }
    std::sort(out.begin(), out.end(),
              [](const ProtocolProviderPtr& a, const ProtocolProviderPtr& b) {
                  if (a->order() != b->order()) return a->order() < b->order();
                  return a->id() < b->id();
              });
    return out;
}

ProtocolProviderPtr find_protocol(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    auto it = registry().find(protocol_id.toUpper());
    return (it == registry().end()) ? ProtocolProviderPtr() : it.value();
}

QStringList registered_protocol_ids() {
    QStringList out;
    for (const ProtocolProviderPtr& p : registered_protocols())
        out << p->id();
    return out;
}

std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id) {
    ProtocolProviderPtr p = find_protocol(protocol_id);
    return p ? p->create_parser() : nullptr;
}

/// @brief 按变体创建字段树构建器(声明在 protocol_tree_builder.h)
/// @details 取第一个"变体匹配且确实提供构建器"的协议;order 升序保证
///          内置协议优先于借用同变体的插件协议。
std::unique_ptr<IProtocolTreeBuilder> make_tree_builder(ProtocolVariant v) {
    for (const ProtocolProviderPtr& p : registered_protocols()) {
        if (p->variant() != v) continue;
        if (std::unique_ptr<IProtocolTreeBuilder> tb = p->create_tree_builder())
            return tb;
    }
    return nullptr;
}

void register_parser_provider(const QString& protocol_id,
                              const QString& display_zh,
                              const QString& display_en,
                              ParserFactoryFn fn,
                              int order) {
    register_protocol(std::make_shared<ParserOnlyProvider>(
        protocol_id, display_zh, display_en, order, std::move(fn)));
}

void register_parser(const QString& protocol_id, ParserFactoryFn fn) {
    register_parser_provider(protocol_id, QString(), QString(), std::move(fn));
}

void unregister_parser(const QString& protocol_id) {
    unregister_protocol(protocol_id);
}
