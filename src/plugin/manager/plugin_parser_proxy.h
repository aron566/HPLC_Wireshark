/// @file plugin_parser_proxy.h
/// @brief PluginParserProxy:主程序内的 IProtocolParser 实现,转发到插件进程
/// @details 让插件解析器对 FrameDispatcher 完全透明:调用方看到的仍是
///          IProtocolParser,实际经 PluginManager 走 IPC 到 bplc-plugin-host。
#ifndef BPLC_PLUGIN_PARSER_PROXY_H
#define BPLC_PLUGIN_PARSER_PROXY_H

#include <QString>
#include <memory>

#include "iprotocolparser.h"

/// @brief 插件解析器代理
class PluginParserProxy : public IProtocolParser {
public:
    explicit PluginParserProxy(const QString& protocol_id);
    ~PluginParserProxy() override = default;

    ProtocolVariant variant() const override;
    ParseResult parse(const BplcFrame& in, MsduState& msdu,
                      const ParseFilter& f) override;

    /// @brief 所属插件协议 id
    QString protocolId() const { return m_protocol_id; }

private:
    QString m_protocol_id;
};

/// @brief 创建插件解析器代理(协议 id 未注册插件时返回 nullptr)
std::unique_ptr<IProtocolParser> make_plugin_parser(const QString& protocol_id);

#endif // BPLC_PLUGIN_PARSER_PROXY_H
