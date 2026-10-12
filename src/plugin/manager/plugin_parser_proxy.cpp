/// @file plugin_parser_proxy.cpp
#include "plugin_parser_proxy.h"
#include "plugin_manager.h"
#include "protocolfactory.h"  // make_parser_by_id(懒回退用)

PluginParserProxy::PluginParserProxy(const QString& protocol_id)
    : m_protocol_id(protocol_id) {}

ProtocolVariant PluginParserProxy::variant() const {
    // 插件协议无对应枚举值:返回 GW_2022 占位。
    // 注意:variant() 仅用于内置协议的 switch 分支,插件路径不依赖它。
    return ProtocolVariant::GW_2022;
}

ParseResult PluginParserProxy::parse(const BplcFrame& in, MsduState& msdu,
                                     const ParseFilter& f) {
    // 插件已禁用/卸载/崩溃(崩溃经 on_process_finished、超时经 parse_via_plugin
    // 都会把 rt->disabled 置真):懒回退到同 id 的内置解析器,保证采集不因插件
    // 崩溃而中断(热回退)。m_fallback 首次创建后缓存,后续帧零开销。
    if (!PluginManager::instance().is_plugin_protocol(m_protocol_id)) {
        if (!m_fallback)
            m_fallback = make_parser_by_id(m_protocol_id);  // 覆盖层已删,取内置层
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
