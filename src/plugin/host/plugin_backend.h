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

    // ---- 主界面控制(Phase4) ----
    /// @brief 插件请求主界面跳转到指定帧的回调类型(host.jumpToFrame)
    /// @details 后端在脚本侧暴露 host 对象;脚本调用 host.jumpToFrame(idx)
    ///          时触发此回调(工作线程),经 PluginWorker/LocalPluginEngine
    ///          转发到 MainWindow::jump_packet_to_frame(GUI 线程)。
    using HostJumpCallback = std::function<void(qint64)>;
    virtual void set_host_jump_callback(HostJumpCallback cb) {
        m_host_jump_cb = cb;
    }

    // ---- 界面语言(Phase5) ----
    /// @brief 设置当前界面语言(供 host.getEnv("BPLC_LANG") 用)
    /// @details 缺省中文;宿主(LocalPluginEngine)在 initialize 后按
    ///          trl::enabled() 调用一次
    virtual void set_ui_english(bool en) { m_ui_english = en; }

    // ---- 界面主题(供 host.getEnv("BPLC_THEME") 用) ----
    /// @brief 设置当前界面是否深色(缺省深色;宿主在 initialize 后按
    ///        theme::apply 解析后的主题调用一次)
    virtual void set_ui_dark(bool dark) { m_ui_dark = dark; }

    // ---- 主界面弹表格窗口(host.showTable) ----
    /// @brief 脚本请求主界面弹出独立表格窗口的回调类型
    /// @details 脚本侧 host.showTable(title, columns, rows);后端转成
    ///          QStringList 经此回调转发,宿主在 GUI 线程弹 QDialog。
    using HostTableCallback = std::function<void(const QString& title,
                                                 const QStringList& columns,
                                                 const QList<QStringList>& rows)>;
    virtual void set_host_table_callback(HostTableCallback cb) {
        m_host_table_cb = cb;
    }

    // ---- 主界面 → 插件通知 ----
    /// @brief 主界面帧列表选中变化通知(单击/双击帧)
    /// @details force_history=true 时强制进入历史冻结(双击,原版 enter_topo_history
    ///          语义);false 时插件按 frameIndex 是否为最新帧自行决定 live/历史
    ///          (原版 update_topo_history 语义:点到最新帧=回到实时)。
    ///          缺省无操作,脚本未定义 on_frame_selected 时静默忽略。
    virtual void notify_frame_selected(qint64 /*frameIndex*/,
                                       bool /*force_history*/) {}

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
    HostJumpCallback m_host_jump_cb;  ///< 插件请求主界面跳帧
    HostTableCallback m_host_table_cb;  ///< 插件请求主界面弹表格窗口
    bool m_ui_english = false;  ///< 界面语言是否为英文(宿主注入)
    bool m_ui_dark = true;  ///< 界面是否深色(宿主注入,缺省深色)
};

/// @brief 按 runtime 创建后端。未知 runtime 返回 nullptr
IPluginBackend* create_backend(const QString& runtime);

#endif // BPLC_PLUGIN_BACKEND_H
