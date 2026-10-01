/// @file plugin_backend.h
/// @brief 插件后端抽象接口(plugin-host 内多种 runtime 的统一抽象)
/// @details NativeBackend(QPluginLoader)/JsBackend(QJSEngine)/LuaBackend
///          都实现此接口,PluginHost 只依赖接口,不感知具体 runtime。
#ifndef BPLC_PLUGIN_BACKEND_H
#define BPLC_PLUGIN_BACKEND_H

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>

#include "bplcframe.h"
#include "igraphicsplugin.h"
#include "iprotocolparser.h"
#include "plugin_manifest.h"

/// @brief 插件后端接口
class IPluginBackend {
public:
    virtual ~IPluginBackend() = default;

    /// @brief 初始化(加载插件代码)。false=失败,err 置原因
    virtual bool initialize(const PluginManifest& m, QString* err) = 0;
    /// @brief 清理
    virtual void shutdown() = 0;
    /// @brief 协议 id(从插件获取,用于校验)
    virtual QString protocol_id() const = 0;
    /// @brief 解析一帧。err 非空时置错误信息
    virtual ParseResult parse(const BplcFrame& frame, MsduState& msdu,
                              const ParseFilter& filter, QString* err) = 0;

    // ---- 图形能力(Phase3,缺省无) ----
    /// @brief 是否提供图形能力
    virtual bool has_graphics() const { return false; }
    /// @brief 首选初始尺寸
    virtual QSize graphics_preferred_size() const { return QSize(400, 300); }
    /// @brief 绘制到 w*h 的 QImage(返回空图=失败)
    virtual QImage render_graphics(int w, int h, QString* err) {
        Q_UNUSED(w); Q_UNUSED(h);
        *err = QStringLiteral("no graphics");
        return QImage();
    }
    /// @brief 处理图形事件,返回 true=需要重绘
    virtual bool handle_graphics_event(const GraphicsEvent& e, QString* err) {
        Q_UNUSED(e);
        *err = QStringLiteral("no graphics");
        return false;
    }
    /// @brief 调用脚本无参全局函数,返回其文本结果(报表/回放数据等)
    /// @details 函数不存在、调用失败或返回非文本时 err 置原因并返回空串;
    ///          JS 端若返回数组则按行拼接
    virtual QString call_text_function(const char* name, QString* err) {
        Q_UNUSED(name);
        *err = QStringLiteral("call_text_function not supported");
        return QString();
    }
    /// @brief 设置重绘回调(插件主动请求重绘时调)
    using RedrawCallback = std::function<void()>;
    virtual void set_redraw_callback(RedrawCallback cb) { m_redraw_cb = cb; }

protected:
    RedrawCallback m_redraw_cb;  ///< 插件调此请求主进程重绘
};

/// @brief 按 runtime 创建后端。未知 runtime 返回 nullptr
IPluginBackend* create_backend(const QString& runtime);

#endif // BPLC_PLUGIN_BACKEND_H
