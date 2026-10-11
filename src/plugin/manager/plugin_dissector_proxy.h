/// @file plugin_dissector_proxy.h
/// @brief 插件解剖器在主程序侧的 IProtocolParser 代理（见 docs/DISSECTOR_ARCH.md）
/// @details 分发器像用内置解析器一样用它；内部把去封装净荷 + FrameContext
///          经 IPC 交给 host 进程中的 IDissectorPlugin，返回的
///          DissectResult 转成 ParseResult（标量/topo/字段树），下游
///          make_entry/列表/过滤/topo 全部沿用既有契约。
#ifndef BPLC_PLUGIN_DISSECTOR_PROXY_H
#define BPLC_PLUGIN_DISSECTOR_PROXY_H

#include <memory>

#include "iprotocolparser.h"

/// @brief 插件解剖器代理（每实例自带帧计数，用于 FrameContext.frame_index）
class PluginDissectorProxy : public IProtocolParser {
public:
    explicit PluginDissectorProxy(const QString& protocol_id)
        : m_protocol_id(protocol_id) {}

    ProtocolVariant variant() const override {
        return ProtocolVariant::GW_2022;  // 占位（插件路径不依赖变体树构建器）
    }
    ParseResult parse(const BplcFrame& in, MsduState& msdu,
                      const ParseFilter& filter) override;

private:
    QString m_protocol_id;
    quint64 m_frame_counter = 0;
};

/// @brief 工厂函数（注册进解析器工厂；id 未就绪/非解剖器返回 nullptr）
std::unique_ptr<IProtocolParser> make_dissector_parser(
    const QString& protocol_id);

#endif // BPLC_PLUGIN_DISSECTOR_PROXY_H
