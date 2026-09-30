/// @file iplugin.h
/// @brief 插件基础接口(BPLC_STA_Monitor 插件系统 API v1)
/// @details 所有插件(无论 native/lua/js)都实现此基础接口。
///          native 插件通过 Q_PLUGIN_METADATA + Q_INTERFACES 导出。
#ifndef BPLC_IPLUGIN_H
#define BPLC_IPLUGIN_H

#include <QString>

/// @brief 插件基础接口 IID(native 插件用 Q_DECLARE_INTERFACE 声明)
#define BPLC_PLUGIN_IID "com.bplc.monitor.Plugin/1.0"

/// @brief 插件基础接口
class IPlugin {
public:
    virtual ~IPlugin() = default;

    /// @brief 插件唯一标识(小写,如 "my-proto")
    virtual QString id() const = 0;
    /// @brief 插件显示名(如 "My Protocol 2024")
    virtual QString displayName() const = 0;
    /// @brief 插件版本(语义化版本,如 "1.0.0")
    virtual QString version() const = 0;
    /// @brief 插件描述
    virtual QString description() const = 0;
    /// @brief 插件作者
    virtual QString author() const = 0;

    /// @brief 初始化(加载后调用,返回 false 则插件被拒绝)
    virtual bool initialize() = 0;
    /// @brief 清理(卸载前调用)
    virtual void shutdown() = 0;
};

Q_DECLARE_INTERFACE(IPlugin, BPLC_PLUGIN_IID)

#endif // BPLC_IPLUGIN_H
