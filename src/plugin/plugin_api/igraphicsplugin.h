/// @file igraphicsplugin.h
/// @brief 图形插件接口:插件进程用 QPainter 绘制到 QImage,经 IPC 发主进程显示
/// @details 插件可选实现此能力(与解析器能力正交):
///   - render():主进程请求重绘时调用,在给定的 QPainter 上绘制
///   - handle_event():主进程转发鼠标/滚轮/缩放事件,返回 true 表示需要重绘
///   - request_redraw():插件主动请求重绘(如定时动画),经 IPC 通知主进程
/// @note 坐标系:逻辑像素,原点左上;主进程按 devicePixelRatio 处理高分屏。
#ifndef BPLC_IGRAPHICSPLUGIN_H
#define BPLC_IGRAPHICSPLUGIN_H

#include <QSize>
#include <QString>

#include <functional>

class QPainter;

/// @brief 图形事件类型(主进程 → 插件)
enum class GraphicsEventType : quint8 {
    MousePress = 0,
    MouseRelease = 1,
    MouseMove = 2,      ///< 悬停/拖动
    Wheel = 3,          ///< delta_y: 滚轮增量(1/8 度)
    Resize = 4,         ///< 视图尺寸变化(插件可忽略,render 会带新尺寸)
    Leave = 5,          ///< 鼠标离开视图
    MouseDblClick = 6   ///< 鼠标双击(供帧记录双击联动主界面等)
};

/// @brief 图形事件(可 IPC 序列化,见 plugin_serialization)
struct GraphicsEvent {
    GraphicsEventType type = GraphicsEventType::MouseMove;
    int x = 0;            ///< 逻辑像素
    int y = 0;
    int button = 0;       ///< Qt::MouseButton
    int modifiers = 0;    ///< Qt::KeyboardModifiers
    int delta_y = 0;      ///< Wheel: 1/8 度
    int width = 0;        ///< Resize: 新宽
    int height = 0;       ///< Resize: 新高
};

/// @brief 图形插件接口(native 插件实现;JS/Lua 由 backend 转调脚本函数)
class IGraphicsPlugin {
public:
    virtual ~IGraphicsPlugin() = default;

    /// @brief 是否提供图形能力
    virtual bool has_graphics() const = 0;
    /// @brief 首选初始尺寸(逻辑像素)
    virtual QSize preferred_size() const = 0;
    /// @brief 绘制。painter 已 begin 到 w*h 的 QImage 上
    virtual void render(QPainter* painter, int w, int h) = 0;
    /// @brief 处理图形事件。返回 true=需要重绘
    virtual bool handle_event(const GraphicsEvent& e) = 0;
    /// @brief 注入重绘回调(插件主动请求重绘时调,如定时动画)。缺省忽略
    virtual void set_redraw_callback(std::function<void()> cb) {
        Q_UNUSED(cb);
    }
};

#define BplcGraphicsPlugin_iid "com.bplc.BplcGraphicsPlugin/1.0"
Q_DECLARE_INTERFACE(IGraphicsPlugin, BplcGraphicsPlugin_iid)

#include <QMetaType>
Q_DECLARE_METATYPE(GraphicsEvent)

#endif // BPLC_IGRAPHICSPLUGIN_H
