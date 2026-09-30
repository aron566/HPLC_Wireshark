/// @file plugin_graphics_view.cpp
#include "plugin_graphics_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include "plugin_manager.h"

PluginGraphicsView::PluginGraphicsView(PluginManager* mgr,
                                       const QString& plugin_id,
                                       QWidget* parent)
    : QWidget(parent), m_mgr(mgr), m_plugin_id(plugin_id) {
    setMouseTracking(true);
    setMinimumSize(200, 150);
    m_render_timer = new QTimer(this);
    m_render_timer->setSingleShot(true);
    m_render_timer->setInterval(50);
    connect(m_render_timer, &QTimer::timeout, this, [this]() {
        m_render_pending = false;
        if (m_mgr) m_mgr->request_render(m_plugin_id, width(), height());
    });
    // 初始渲染
    request_render();
}

PluginGraphicsView::~PluginGraphicsView() = default;

void PluginGraphicsView::set_image(const QImage& img) {
    m_image = img;
    update();
}

void PluginGraphicsView::on_plugin_request_redraw() {
    request_render();
}

void PluginGraphicsView::request_render() {
    if (m_render_pending || !m_mgr) return;
    m_render_pending = true;
    m_render_timer->start();
}

void PluginGraphicsView::send_event(const GraphicsEvent& e) {
    if (!m_mgr) return;
    const bool redraw = m_mgr->send_graphics_event(m_plugin_id, e);
    if (redraw) request_render();
}

void PluginGraphicsView::paintEvent(QPaintEvent* e) {
    Q_UNUSED(e);
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    if (!m_image.isNull()) {
        // 按控件尺寸拉伸(插件按请求尺寸绘制,通常一致)
        p.drawImage(rect(), m_image);
    } else {
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("rendering..."));
    }
}

void PluginGraphicsView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    GraphicsEvent ge;
    ge.type = GraphicsEventType::Resize;
    ge.width = width();
    ge.height = height();
    send_event(ge);  // 插件可忽略;render 会带新尺寸
    request_render();
}

void PluginGraphicsView::mousePressEvent(QMouseEvent* e) {
    GraphicsEvent ge;
    ge.type = GraphicsEventType::MousePress;
    ge.x = e->pos().x(); ge.y = e->pos().y();
    ge.button = e->button(); ge.modifiers = e->modifiers();
    send_event(ge);
}

void PluginGraphicsView::mouseReleaseEvent(QMouseEvent* e) {
    GraphicsEvent ge;
    ge.type = GraphicsEventType::MouseRelease;
    ge.x = e->pos().x(); ge.y = e->pos().y();
    ge.button = e->button(); ge.modifiers = e->modifiers();
    send_event(ge);
}

void PluginGraphicsView::mouseMoveEvent(QMouseEvent* e) {
    GraphicsEvent ge;
    ge.type = GraphicsEventType::MouseMove;
    ge.x = e->pos().x(); ge.y = e->pos().y();
    ge.button = e->buttons();
    ge.modifiers = e->modifiers();
    send_event(ge);
}

void PluginGraphicsView::wheelEvent(QWheelEvent* e) {
    GraphicsEvent ge;
    ge.type = GraphicsEventType::Wheel;
    ge.x = e->position().x(); ge.y = e->position().y();
    ge.delta_y = e->angleDelta().y();
    ge.modifiers = e->modifiers();
    send_event(ge);
    e->accept();
}

void PluginGraphicsView::leaveEvent(QEvent* e) {
    Q_UNUSED(e);
    GraphicsEvent ge;
    ge.type = GraphicsEventType::Leave;
    send_event(ge);
}
