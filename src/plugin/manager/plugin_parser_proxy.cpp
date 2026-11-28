/// @file plugin_parser_proxy.cpp
#include "plugin_parser_proxy.h"
#include "plugin_manager.h"
#include "protocolfactory.h"

PluginParserProxy::PluginParserProxy(const QString& protocol_id)
    : m_protocol_id(protocol_id) {}

ProtocolVariant PluginParserProxy::variant() const {
    // 插件协议无对应枚举值:返回 GW_2022 占位。
    // 注意:variant() 仅用于内置协议的 switch 分支,插件路径不依赖它。
    return ProtocolVariant::GW_2022;
}

ParseResult PluginParserProxy::parse(const BplcFrame& in, MsduState& msdu,
                                     const ParseFilter& f) {
    // A2(移植 parser 分支):逐帧懒回退。插件协议当前不可用(未注册/已超时停用/
    // 已注销)时,不直接拒收,改为懒取内置回退解析器逐帧解析;回退实例首次
    // 需要时创建并缓存,复用同一实例。回退只取内置层(make_builtin_parser_by_id),
    // 永不取他方 Ext 条目:采集中不切换,且插件在检查与查找之间重注册的竞态
    // 下也不会误取新插件实例。无内置可取(如纯插件新 id)时拒收并点明原因。
    // 注意:本分支不走 parse_via_plugin,故不触 A3 的超时注销;插件恢复后
    // 下次 parse 重新走插件(本代理不缓存"插件不可用"的结论)。
    if (!PluginManager::instance().is_plugin_protocol(m_protocol_id)) {
        if (!m_fallback)
            m_fallback = make_builtin_parser_by_id(m_protocol_id);
        if (m_fallback)
            return m_fallback->parse(in, msdu, f);
        ParseResult r;
        r.accept = false;
        r.reject_reason = QStringLiteral("plugin unavailable (no builtin fallback)");
        return r;
    }
    ParseResult r;
    if (!PluginManager::instance().parse_via_plugin(m_protocol_id, in, msdu, f, &r)) {
        r.accept = false;
        if (r.reject_reason.isEmpty())
            r.reject_reason = QStringLiteral("plugin unavailable");
    }
    return r;
}

std::unique_ptr<IProtocolParser> make_plugin_parser(const QString& protocol_id) {
    if (!PluginManager::instance().is_plugin_protocol(protocol_id))
        return nullptr;
    return std::make_unique<PluginParserProxy>(protocol_id);
}
