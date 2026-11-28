/// @file protocolfactory.cpp
/// @brief 协议注册表实现(分层:内置层 + 插件覆盖层)+ 字段树构建器工厂
/// @details 本文件不认识任何具体协议:内置协议由各自 <proto>_provider.cpp
///          静态自注册(归属为空 → 内置层),插件协议由 PluginManager 在握手时
///          注册(归属为插件 protocol_id → 覆盖层)。新增协议版本时本文件无需改动。
///          内置层只读:写内置层拒绝覆盖已登记的内置 id(A-7 守卫),注销只删
///          覆盖层,插件永远删不掉内置协议;no-shadow(用户 2026-11-20 定):
///          影子覆盖路径已去掉,覆盖层只许注册新 id,注册内置 id 直接拒绝并告警
///          (A4);与内置同名共存只能走外置路径(register_external_protocol),
///          Ext 条目永不参与按 id 的查找,按 id 查找恒为内置优先。
#include "protocolfactory.h"
#include "protocol_tree_builder.h"

#include <QDebug>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

namespace {

/// @brief 内置层:四内置协议静态自注册,此后只读(注销/覆盖都不碰它)
QMap<QString, ProtocolProviderPtr>& builtin_registry() {
    static QMap<QString, ProtocolProviderPtr> r;
    return r;
}

/// @brief 覆盖层条目:插件协议 + 归属 + 注册引用计数
struct OverrideEntry {
    ProtocolProviderPtr provider;
    QString             owner_plugin_id;
    int                 ref_count = 0;
};

/// @brief 插件覆盖层:插件协议(新 id 或与内置同名共存的 Ext 条目),随插件装卸增删
/// @details 条目以 (协议 id + 归属) 区分:同 id 至多一条,先到先得;Ext 条目与内置
///          同名时并列存在,按 id 查找恒取内置(见 find_protocol_locked)。
QMap<QString, OverrideEntry>& override_registry() {
    static QMap<QString, OverrideEntry> r;
    return r;
}

QMutex& registry_mutex() {
    static QMutex m;
    return m;
}

/// @brief 内置协议 id 静态名单(四内置变体,与注册表内容无关)
/// @details A-7 守卫与 no-shadow 拒绝的判定依据:不依赖内置层是否已登记,
///          单测只链接本文件时同样生效。
bool is_builtin_id(const QString& id_upper) {
    static const QStringList kBuiltinIds = {
        QStringLiteral("GW_2022"), QStringLiteral("NW_2021"),
        QStringLiteral("GW_2026"), QStringLiteral("NW_2026"),
    };
    return kBuiltinIds.contains(id_upper);
}

/// @brief 按 id 查协议(调用方已持锁):内置优先、其次覆盖层
/// @details no-shadow 的第二道防线:即使覆盖层里有内置同名 Ext 条目,按 id
///          查找也恒取内置;插件新 id 无内置可取时才落到覆盖层。
ProtocolProviderPtr find_protocol_locked(const QString& id_upper) {
    auto bit = builtin_registry().find(id_upper);
    if (bit != builtin_registry().end()) return bit.value();
    auto oit = override_registry().find(id_upper);
    return (oit == override_registry().end()) ? ProtocolProviderPtr()
                                               : oit->provider;
}

/// @brief 仅提供解析器的协议(插件协议用)
/// @details 插件解析结果自带字段树(ParseResult.msdu.tree / MsduFieldNode),
///          不构造 QTreeWidget,因此 create_tree_builder 返回 nullptr;
///          变体沿用 GW_2022(与 PluginParserProxy::variant 保持一致)。
class ParserOnlyProvider : public IProtocolProvider {
public:
    ParserOnlyProvider(QString id, QString zh, QString en, int order,
                       ParserFactoryFn fn, QString owner_plugin_id)
        : m_id(std::move(id)), m_zh(std::move(zh)), m_en(std::move(en)),
          m_order(order), m_fn(std::move(fn)),
          m_owner_plugin_id(std::move(owner_plugin_id)) {}

    QString id() const override { return m_id; }
    QString display_name_zh() const override { return m_zh.isEmpty() ? m_id : m_zh; }
    QString display_name_en() const override {
        return m_en.isEmpty() ? display_name_zh() : m_en;
    }
    int order() const override { return m_order; }
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }
    QString owner_plugin_id() const override { return m_owner_plugin_id; }

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
    QString         m_owner_plugin_id;
};

}  // namespace

bool register_protocol(ProtocolProviderPtr provider) {
    if (!provider) return false;
    const QString id = provider->id().toUpper();
    if (id.isEmpty()) return false;
    const QString owner = provider->owner_plugin_id();
    if (owner.isEmpty()) {
        // 内置层:只读,拒绝覆盖已登记的内置 id(A-7 守卫)
        QMutexLocker l(&registry_mutex());
        if (builtin_registry().contains(id)) {
            qWarning() << "register_protocol refuses to overwrite builtin protocol"
                       << id;
            return false;
        }
        builtin_registry().insert(id, std::move(provider));
        return true;
    }
    return register_override_protocol(std::move(provider), owner);
}

bool register_override_protocol(ProtocolProviderPtr provider,
                                const QString& owner_plugin_id) {
    if (!provider) return false;
    const QString id = provider->id().toUpper();
    if (id.isEmpty()) return false;
    if (is_builtin_id(id)) {
        // no-shadow(A4:拒绝 + 告警):影子覆盖路径已去掉,覆盖层只许注册新 id;
        // 与内置同名共存请走外置路径 register_external_protocol
        qWarning() << "register_protocol refuses shadow of builtin protocol" << id
                   << "(rejected owner:"
                   << (owner_plugin_id.isEmpty() ? QStringLiteral("<unknown>")
                                                 : owner_plugin_id)
                   << ")";
        return false;
    }
    QMutexLocker l(&registry_mutex());
    auto it = override_registry().find(id);
    if (it != override_registry().end()) {
        if (it->owner_plugin_id != owner_plugin_id) {
            // 后注册拒绝:先到先得,不同归属抢占已登记的 id 直接拒绝
            qWarning() << "register_protocol refuses override of protocol" << id
                       << "owned by plugin" << it->owner_plugin_id
                       << "(rejected owner:" << owner_plugin_id << ")";
            return false;
        }
        it->ref_count += 1;
        it->provider = std::move(provider);
        return true;
    }
    OverrideEntry entry;
    entry.provider = std::move(provider);
    entry.owner_plugin_id = owner_plugin_id;
    entry.ref_count = 1;
    override_registry().insert(id, std::move(entry));
    return true;
}

bool register_external_protocol(ProtocolProviderPtr provider,
                                const QString& owner_plugin_id) {
    if (!provider) return false;
    const QString id = provider->id().toUpper();
    if (id.isEmpty()) return false;
    QMutexLocker l(&registry_mutex());
    auto it = override_registry().find(id);
    if (it != override_registry().end()) {
        if (it->owner_plugin_id != owner_plugin_id) {
            // 后注册拒绝:先到先得,不同归属抢占已登记的 id 直接拒绝
            qWarning() << "register_protocol refuses external protocol" << id
                       << "owned by plugin" << it->owner_plugin_id
                       << "(rejected owner:" << owner_plugin_id << ")";
            return false;
        }
        it->ref_count += 1;
        it->provider = std::move(provider);
        return true;
    }
    // Ext 条目与内置同名是经批准的共存(不告警):它永不参与按 id 的查找,
    // 只经按归属查找取用,下拉框里与内置并列显示并带 " (Ext)" 标记
    OverrideEntry entry;
    entry.provider = std::move(provider);
    entry.owner_plugin_id = owner_plugin_id;
    entry.ref_count = 1;
    override_registry().insert(id, std::move(entry));
    return true;
}

bool unregister_protocol(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    // 只删覆盖层:内置层永远不受影响(停机清理也一样)
    return override_registry().remove(protocol_id.toUpper()) > 0;
}

bool unregister_protocol_for_owner(const QString& protocol_id,
                                   const QString& owner_plugin_id) {
    QMutexLocker l(&registry_mutex());
    const QString id = protocol_id.toUpper();
    auto it = override_registry().find(id);
    if (it == override_registry().end()) return false;
    if (it->owner_plugin_id != owner_plugin_id) {
        qWarning() << "unregister_protocol refuses: protocol" << id
                   << "owned by plugin" << it->owner_plugin_id
                   << "(unregister requested by:" << owner_plugin_id << ")";
        return false;
    }
    if (it->ref_count > 1) {
        it->ref_count -= 1;
        return true;
    }
    override_registry().erase(it);
    return true;
}

bool is_builtin_protocol(const QString& protocol_id) {
    return is_builtin_id(protocol_id.toUpper());
}

QString protocol_owner_plugin_id(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    auto it = override_registry().find(protocol_id.toUpper());
    return (it == override_registry().end()) ? QString()
                                             : it->owner_plugin_id;
}

int override_ref_count(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    auto it = override_registry().find(protocol_id.toUpper());
    return (it == override_registry().end()) ? 0 : it->ref_count;
}

QVector<ProtocolProviderPtr> registered_protocols() {
    QVector<ProtocolProviderPtr> out;
    QMutexLocker l(&registry_mutex());
    // 内置在前:内置层 id 按 (order, id) 排;同名 Ext 条目不替换内置,
    // 而是与内置并列出现在覆盖层一段(下拉框据此显示两条,各带归属)。
    QVector<ProtocolProviderPtr> builtins = builtin_registry().values().toVector();
    std::sort(builtins.begin(), builtins.end(),
              [](const ProtocolProviderPtr& a, const ProtocolProviderPtr& b) {
                  if (a->order() != b->order()) return a->order() < b->order();
                  return a->id() < b->id();
              });
    for (const ProtocolProviderPtr& b : builtins) out.append(b);
    // 覆盖层全部条目在后,同样按 (order, id) 排(含与内置同名的 Ext 条目)
    QVector<OverrideEntry> overrides = override_registry().values().toVector();
    std::sort(overrides.begin(), overrides.end(),
              [](const OverrideEntry& a, const OverrideEntry& b) {
                  if (a.provider->order() != b.provider->order())
                      return a.provider->order() < b.provider->order();
                  return a.provider->id() < b.provider->id();
              });
    for (const OverrideEntry& e : overrides) out.append(e.provider);
    return out;
}

ProtocolProviderPtr find_protocol(const QString& protocol_id) {
    QMutexLocker l(&registry_mutex());
    return find_protocol_locked(protocol_id.toUpper());
}

ProtocolProviderPtr find_protocol_for_owner(const QString& protocol_id,
                                            const QString& owner_plugin_id) {
    QMutexLocker l(&registry_mutex());
    const QString id = protocol_id.toUpper();
    if (!owner_plugin_id.isEmpty()) {
        auto oit = override_registry().find(id);
        if (oit != override_registry().end()
            && oit->owner_plugin_id == owner_plugin_id)
            return oit->provider;
        // 归属条目不存在(插件已卸载/未注册):只回退同 id 内置,绝不取另一归属的条目
        auto bit = builtin_registry().find(id);
        return (bit == builtin_registry().end()) ? ProtocolProviderPtr()
                                                 : bit.value();
    }
    return find_protocol_locked(id);
}

QStringList registered_protocol_ids() {
    QStringList out;
    for (const ProtocolProviderPtr& p : registered_protocols())
        out << p->id().toUpper();
    return out;
}

std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id) {
    ProtocolProviderPtr p = find_protocol(protocol_id);
    return p ? p->create_parser() : nullptr;
}

std::unique_ptr<IProtocolParser> make_parser_by_id_for_owner(
    const QString& protocol_id, const QString& owner_plugin_id) {
    ProtocolProviderPtr p = find_protocol_for_owner(protocol_id, owner_plugin_id);
    return p ? p->create_parser() : nullptr;
}

std::unique_ptr<IProtocolParser> make_builtin_parser_by_id(const QString& protocol_id) {
    ProtocolProviderPtr p;
    {
        QMutexLocker lock(&registry_mutex());
        auto it = builtin_registry().find(protocol_id.toUpper());
        if (it != builtin_registry().end()) p = it.value();
    }
    return p ? p->create_parser() : nullptr;
}

QString encode_protocol_selection(const QString& protocol_id,
                                  const QString& owner_plugin_id) {
    const QString id = protocol_id.toLower();
    if (owner_plugin_id.isEmpty()) return id;
    return id + QStringLiteral("@") + owner_plugin_id;
}

ProtocolSelection decode_protocol_selection(const QString& encoded) {
    ProtocolSelection sel;
    const int at = encoded.indexOf(QLatin1Char('@'));
    if (at < 0) {
        sel.protocol_id = encoded.toUpper();
        return sel;
    }
    sel.protocol_id = encoded.left(at).toUpper();
    sel.owner_plugin_id = encoded.mid(at + 1);
    return sel;
}

/// @brief 按变体创建字段树构建器(声明在 protocol_tree_builder.h)
/// @details 取第一个"变体匹配且确实提供构建器"的协议;内置层优先于覆盖层——
///          插件协议不提供 QTreeWidget 构建器,故 Ext 条目也抢不走字段树。
///          注意:不能直接遍历 registered_protocols(),因为同名 Ext 条目在那里
///          与内置并列,须显式先内置后覆盖。
std::unique_ptr<IProtocolTreeBuilder> make_tree_builder(ProtocolVariant v) {
    QVector<ProtocolProviderPtr> builtins;
    QVector<ProtocolProviderPtr> overrides;
    {
        QMutexLocker l(&registry_mutex());
        builtins = builtin_registry().values().toVector();
        for (const OverrideEntry& e : override_registry())
            overrides.append(e.provider);
    }
    const auto by_order = [](const ProtocolProviderPtr& a,
                             const ProtocolProviderPtr& b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->id() < b->id();
    };
    std::sort(builtins.begin(), builtins.end(), by_order);
    std::sort(overrides.begin(), overrides.end(), by_order);
    for (const ProtocolProviderPtr& p : builtins) {
        if (p->variant() != v) continue;
        if (std::unique_ptr<IProtocolTreeBuilder> tb = p->create_tree_builder())
            return tb;
    }
    for (const ProtocolProviderPtr& p : overrides) {
        if (p->variant() != v) continue;
        if (std::unique_ptr<IProtocolTreeBuilder> tb = p->create_tree_builder())
            return tb;
    }
    return nullptr;
}

bool register_parser_provider(const QString& protocol_id,
                              const QString& display_zh,
                              const QString& display_en,
                              ParserFactoryFn fn,
                              int order,
                              const QString& owner_plugin_id) {
    return register_override_protocol(
        std::make_shared<ParserOnlyProvider>(protocol_id, display_zh, display_en,
                                             order, std::move(fn), owner_plugin_id),
        owner_plugin_id);
}

bool register_external_parser_provider(const QString& protocol_id,
                                       const QString& display_zh,
                                       const QString& display_en,
                                       ParserFactoryFn fn,
                                       int order,
                                       const QString& owner_plugin_id) {
    return register_external_protocol(
        std::make_shared<ParserOnlyProvider>(protocol_id, display_zh, display_en,
                                             order, std::move(fn), owner_plugin_id),
        owner_plugin_id);
}

bool register_parser(const QString& protocol_id, ParserFactoryFn fn) {
    return register_parser_provider(protocol_id, QString(), QString(),
                                    std::move(fn));
}

bool unregister_parser(const QString& protocol_id) {
    return unregister_protocol(protocol_id);
}

bool unregister_parser_for_owner(const QString& protocol_id,
                                 const QString& owner_plugin_id) {
    return unregister_protocol_for_owner(protocol_id, owner_plugin_id);
}
