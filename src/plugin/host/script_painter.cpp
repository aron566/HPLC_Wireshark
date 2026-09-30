/// @file script_painter.cpp
#include "script_painter.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>

ScriptPainter::ScriptPainter(QPainter* p, QObject* parent)
    : QObject(parent), m_p(p) {}

void ScriptPainter::set_pen(const QString& color, double width) {
    m_p->setPen(QPen(QColor(color), width));
}
void ScriptPainter::set_brush(const QString& color) {
    m_p->setBrush(QColor(color));
}
void ScriptPainter::no_brush() {
    m_p->setBrush(Qt::NoBrush);
}
void ScriptPainter::no_pen() {
    m_p->setPen(Qt::NoPen);
}
void ScriptPainter::set_font(const QString& family, int point_size,
                             bool bold) {
    QFont f(family, point_size);
    f.setBold(bold);
    m_p->setFont(f);
}
void ScriptPainter::clear(const QString& color) {
    m_p->fillRect(m_p->window(), QColor(color));
}
void ScriptPainter::draw_line(double x1, double y1, double x2, double y2) {
    m_p->drawLine(QLineF(x1, y1, x2, y2));
}
void ScriptPainter::draw_rect(double x, double y, double w, double h) {
    m_p->drawRect(QRectF(x, y, w, h));
}
void ScriptPainter::fill_rect(double x, double y, double w, double h,
                               const QString& color) {
    m_p->fillRect(QRectF(x, y, w, h), QColor(color));
}
void ScriptPainter::draw_ellipse(double x, double y, double w, double h) {
    m_p->drawEllipse(QRectF(x, y, w, h));
}
void ScriptPainter::draw_text(double x, double y, const QString& text) {
    m_p->drawText(QPointF(x, y), text);
}
void ScriptPainter::draw_point(double x, double y) {
    m_p->drawPoint(QPointF(x, y));
}
