/// @file iprotocolparserplugin.h
/// @brief 协议解析器插件接口(BPLC_STA_Monitor 插件系统 API v1)
/// @details 插件实现此接口以提供新的协议解析器。插件运行在独立的
///          bplc-plugin-host 进程中,通过 IPC 接收解析请求。
///          插件看到的接口语义与主程序内的 IProtocolParser 完全一致。
#ifndef BPLC_IPROTOCOLPARSERPLUGIN_H
#define BPLC_IPROTOCOLPARSERPLUGIN_H

#include "iplugin.h"
#include "iprotocolparser.h"  // IProtocolParser / ParseResult / BplcFrame / MsduState / ParseFilter

/// @brief 协议解析器插件接口 IID
#define BPLC_PARSER_PLUGIN_IID "com.bplc.monitor.ProtocolParserPlugin/1.0"

/// @brief 协议解析器插件接口
/// @note 实现者注意:
///   - createParser() 每次调用返回一个独立实例(多帧并发/多状态隔离)
///   - IProtocolParser::parse() 的语义与主程序内完全一致:
///     输入 BplcFrame + MsduState(重组状态) + ParseFilter,
///     返回 ParseResult(字段树 msdu.tree 由插件直接填充)
///   - 插件崩溃不会影响主进程,PluginManager 会检测并禁用该插件
class IProtocolParserPlugin : public IPlugin {
public:
    /// @brief 协议唯一标识(大写,如 "MYPROTO_2024",用于配置与注册)
    virtual QString protocolId() const = 0;
    /// @brief 创建解析器实例(调用方拥有所有权)
    virtual IProtocolParser* createParser() = 0;
};

Q_DECLARE_INTERFACE(IProtocolParserPlugin, BPLC_PARSER_PLUGIN_IID)

#endif // BPLC_IPROTOCOLPARSERPLUGIN_H
