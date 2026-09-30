/// @file plugin_graphics_view.h
/// @brief PluginGraphicsView:主进程侧图形插件视图
/// @details 显示插件进程渲染的 QImage;鼠标/滚轮/缩放事件经 IPC 转发给插件;
///          插件返回 needs_redraw 或主动 RequestRedraw 时重新请求渲染。
/// @note 线程:主线程。IPC 走 PluginManager 的 socket(复用解析通道)。
#ifndef BPLC_PLUGIN_GRAPHICS_VIEW_H
#define BPLC_PLUGIN_GRAPHICS_VIEW_H

#include <QImage>
#include <QWidget>

#include "igraphicsplugin.h"

class PluginManager;
class QTimer;

/// @brief 图形插件视图(主进程)
class PluginGraphicsView : public QWidget {
    Q_OBJECT
public:
    explicit PluginGraphicsView(PluginManager* mgr, const QString& plugin_id,
                                QWidget* parent = nullptr);
    ~PluginGraphicsView() override;

    /// @brief 插件 id
    QString plugin_id() const { return m_plugin_id; }
    /// @brief 收到新的渲染图(由 PluginManager 回调)
    void set_image(const QImage& img);
    /// @brief 插件请求重绘(由 PluginManager 回调)
    void on_plugin_request_redraw();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    /// @brief 请求重绘(节流:resize 连续触发时合并)
    void request_render();
    /// @brief 发送图形事件,按返回决定是否重绘
    void send_event(const GraphicsEvent& e);

    PluginManager* m_mgr;  ///< 不拥有
    QString m_plugin_id;
    QImage m_image;
    QTimer* m_render_timer = nullptr;  ///< resize 节流
    bool m_render_pending = false;
};

#endif // BPLC_PLUGIN_GRAPHICS_VIEW_H
