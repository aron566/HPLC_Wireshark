/// @file ipluginbus.h
/// @brief 插件间通讯接口（可选实现；主进程总线中转，见 docs/DISSECTOR_ARCH.md）
/// @details 插件实现此接口即可收发插件间消息：宿主在加载后注入发送函数，
///          其他插件的消息经 on_plugin_message 投递。寻址用插件名
///          （plugin.json 的 name），topic 自定义，payload 为不透明字节
///          （约定 UTF-8 JSON）。目标不存在时主进程静默丢弃。
#ifndef BPLC_IPLUGINBUS_H
#define BPLC_IPLUGINBUS_H

#include <QByteArray>
#include <QString>

#include <functional>

/// @brief 插件间通讯接口 IID
#define BPLC_PLUGIN_BUS_IID "com.bplc.monitor.PluginBusAware/1.0"

/// @brief 插件间通讯接口（可与解析/图形/解剖接口同插件共存实现）
class IPluginBusAware {
public:
    virtual ~IPluginBusAware() = default;

    /// @brief 发送函数类型（宿主注入；to=目标插件名）
    using SendFn = std::function<void(const QString& to, const QString& topic,
                                      const QByteArray& payload)>;

    /// @brief 宿主注入发送函数（加载后调用一次）
    virtual void set_bus_sender(SendFn fn) = 0;

    /// @brief 收到其他插件的消息
    virtual void on_plugin_message(const QString& from, const QString& topic,
                                   const QByteArray& payload) = 0;
};

Q_DECLARE_INTERFACE(IPluginBusAware, BPLC_PLUGIN_BUS_IID)

#endif // BPLC_IPLUGINBUS_H
