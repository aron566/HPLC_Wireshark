/// @file script_painter.h
/// @brief 脚本绘图桥:给 JS/Lua 暴露 QPainter 子集(QObject 供 QJSEngine, C 函数供 Lua)
/// @details API(颜色为 "#rrggbb" 或 SVG 颜色名):
///   set_pen(color, width), set_brush(color), no_brush(),
///   set_font(family, point_size, bold),
///   draw_line(x1,y1,x2,y2), draw_rect(x,y,w,h), fill_rect(x,y,w,h,color),
///   draw_ellipse(x,y,w,h), draw_text(x,y,text),
///   text_width(text): 当前字体下文本像素宽度(布局用),
///   draw_point(x,y), clear(color),
///   draw_icon(name, x, y, w, h):绘制内置节点图标,与原版 TopoWindow 同源;
///     name: "cco" | "meter_online" | "meter_joining" | "meter_offline";
///     资源缺失时退化为彩色圆点,不抛错
#ifndef BPLC_SCRIPT_PAINTER_H
#define BPLC_SCRIPT_PAINTER_H

#include <QObject>
#include <QString>

class QPainter;

/// @brief QPainter 的脚本友好封装(JS 用 newQObject 暴露,Lua 用 c 函数表)
class ScriptPainter : public QObject {
    Q_OBJECT
public:
    explicit ScriptPainter(QPainter* p, QObject* parent = nullptr);

    Q_INVOKABLE void set_pen(const QString& color, double width = 1.0);
    Q_INVOKABLE void set_brush(const QString& color);
    Q_INVOKABLE void no_brush();
    Q_INVOKABLE void no_pen();
    Q_INVOKABLE void set_font(const QString& family, int point_size,
                              bool bold = false);
    Q_INVOKABLE void clear(const QString& color);
    Q_INVOKABLE void draw_line(double x1, double y1, double x2, double y2);
    Q_INVOKABLE void draw_rect(double x, double y, double w, double h);
    Q_INVOKABLE void fill_rect(double x, double y, double w, double h,
                               const QString& color);
    Q_INVOKABLE void draw_ellipse(double x, double y, double w, double h);
    Q_INVOKABLE void draw_text(double x, double y, const QString& text);
    /// @brief 当前字体下文本的像素宽度(布局用,如 tooltip 自动撑宽)
    Q_INVOKABLE double text_width(const QString& text);
    Q_INVOKABLE void draw_point(double x, double y);
    Q_INVOKABLE void draw_icon(const QString& name, double x, double y,
                               double w, double h);

    QPainter* painter() const { return m_p; }

private:
    QPainter* m_p;  ///< 不拥有
};

#endif // BPLC_SCRIPT_PAINTER_H
