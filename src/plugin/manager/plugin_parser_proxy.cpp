/// @file plugin_parser_proxy.cpp
#include "plugin_parser_proxy.h"
#include "plugin_manager.h"

PluginParserProxy::PluginParserProxy(const QString& protocol_id)
    : m_protocol_id(protocol_id) {}

ProtocolVariant PluginParserProxy::variant() const {
    // 插件协议无对应枚举值:返回 GW_2022 占位。
    // 注意:variant() 仅用于内置协议的 switch 分支,插件路径不依赖它。
    return ProtocolVariant::GW_2022;
}

ParseResult PluginParserProxy::parse(const BplcFrame& in, MsduState& msdu,
                                     const ParseFilter& f) {
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
