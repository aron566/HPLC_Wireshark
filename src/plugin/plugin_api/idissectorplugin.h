/// @file idissectorplugin.h
/// @brief 解剖器插件接口：以通用结构化字段返回解析结果（见 docs/DISSECTOR_ARCH.md）
/// @details 插件在 bplc-plugin-host 进程中实现此接口。与旧
///          IProtocolParserPlugin::create_parser 路径的区别：
///          - 输入只有去封装净荷 + FrameContext，不含主程序内部状态；
///          - 跨帧状态（如 MSDU 重组）由插件实例自持，
///            主程序新开采集时经 dissect_reset() 通知清理；
///          - 输出为 DissectResult（通用字段树），主界面统一渲染，
///            与内置国网/南网解析器的详情视图行为一致。
///          只有清单声明 "dissector": true 的插件才会被主程序注册为
///          可选协议并出现在协议下拉框中。
#ifndef BPLC_IDISSECTORPLUGIN_H
#define BPLC_IDISSECTORPLUGIN_H

#include <QByteArray>
#include <QString>

#include "dissect.h"

/// @brief 解剖器插件接口 IID
#define BPLC_DISSECTOR_PLUGIN_IID "com.bplc.monitor.DissectorPlugin/1.0"

/// @brief 解剖器插件接口（可与 IProtocolParserPlugin 同插件共存实现）
class IDissectorPlugin {
public:
    virtual ~IDissectorPlugin() = default;

    /// @brief 本解剖器负责的协议 id（须与 plugin.json 的 protocol_id 一致）
    virtual QString dissector_protocol_id() const = 0;

    /// @brief 解剖一帧去封装净荷
    /// @param payload 去封装后的帧字节（BplcFrame::data）
    /// @param ctx     主程序告知的帧外信息（时间戳/通道等，可忽略）
    virtual DissectResult dissect(const QByteArray& payload,
                                  const FrameContext& ctx) = 0;

    /// @brief 主程序开始新采集/回放时调用：清除跨帧状态（默认空实现）
    virtual void dissect_reset() {}
};

Q_DECLARE_INTERFACE(IDissectorPlugin, BPLC_DISSECTOR_PLUGIN_IID)

#endif // BPLC_IDISSECTORPLUGIN_H
