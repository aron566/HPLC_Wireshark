/// @file native_backend.cpp
#include "native_backend.h"
#include <QDir>
#include <QImage>
#include <QPainter>

NativeBackend::~NativeBackend() {
    shutdown();
}

bool NativeBackend::initialize(const PluginManifest& m, QString* err) {
    if (m.api_version != 1) {
        *err = QStringLiteral("api_version mismatch");
        return false;
    }
    const QString lib_path = plugin_resolve_native_entry(m.dir_path, m.entry);
    m_loader.setFileName(lib_path);
    QObject* inst = m_loader.instance();
    if (!inst) {
        *err = QStringLiteral("load failed: %1").arg(m_loader.errorString());
        return false;
    }
    m_instance = inst;   // 根实例由本后端拥有,shutdown 时先删后卸库
    // 解析器接口(可选:纯图形插件可不实现)
    m_plugin = qobject_cast<IProtocolParserPlugin*>(inst);
    // 图形接口(可选)
    m_graphics = qobject_cast<IGraphicsPlugin*>(inst);
    // 解剖器接口(可选:新架构)
    m_dissector = qobject_cast<IDissectorPlugin*>(inst);
    // 插件间通讯接口(可选)
    m_bus_aware = qobject_cast<IPluginBusAware*>(inst);

    if (!m_plugin && !m_graphics && !m_dissector) {
        *err = QStringLiteral("implements neither IProtocolParserPlugin, "
                              "IGraphicsPlugin nor IDissectorPlugin");
        return false;
    }
    if (m_dissector &&
        m_dissector->dissector_protocol_id() != m.protocol_id) {
        *err = QStringLiteral("dissector protocol mismatch: manifest=%1 plugin=%2")
                   .arg(m.protocol_id, m_dissector->dissector_protocol_id());
        return false;
    }
    if (m_plugin) {
        if (!m_plugin->initialize()) {
            *err = QStringLiteral("plugin initialize() failed");
            return false;
        }
        m_parser = m_plugin->create_parser();
        if (!m_parser) {
            *err = QStringLiteral("create_parser() returned null");
            return false;
        }
        m_protocol_id = m_plugin->protocol_id();
    } else {
        // 纯图形插件:protocol_id 取清单
        m_protocol_id = m.protocol_id;
    }
    if (m.graphics && !m_graphics) {
        *err = QStringLiteral("manifest graphics=true but plugin has no IGraphicsPlugin");
        return false;
    }
    if (m_graphics) {
        // 注入重绘回调(经 backend 的 m_redraw_cb 转发)
        m_graphics->set_redraw_callback([this]() {
            if (m_redraw_cb) m_redraw_cb();
        });
    }
    return true;
}

void NativeBackend::shutdown() {
    delete m_parser;
    m_parser = nullptr;
    if (m_plugin) { m_plugin->shutdown(); m_plugin = nullptr; }
    m_graphics = nullptr;
    m_dissector = nullptr;
    m_bus_aware = nullptr;
    // 先删插件根实例再卸库:实例的虚表/回调驻留在 DLL 内,实例存活时
    // unload 即使成功也会留下悬空指针(渲染回调等会踩已卸载代码)。
    delete m_instance;
    m_instance = nullptr;
    if (m_loader.isLoaded() && !m_loader.unload())
        qWarning("NativeBackend: plugin unload failed: %s",
                 qPrintable(m_loader.errorString()));
}

bool NativeBackend::has_dissector() const {
    return m_dissector != nullptr;
}

bool NativeBackend::dissect(const QByteArray& payload, const FrameContext& ctx,
                            DissectResult* out, QString* err) {
    if (!m_dissector) {
        if (err) *err = QStringLiteral("plugin implements no IDissectorPlugin");
        return false;
    }
    *out = m_dissector->dissect(payload, ctx);
    return true;
}

void NativeBackend::dissect_reset() {
    if (m_dissector) m_dissector->dissect_reset();
}

void NativeBackend::set_bus_send_callback(BusSendCallback cb) {
    IPluginBackend::set_bus_send_callback(cb);
    if (m_bus_aware) {
        m_bus_aware->set_bus_sender(
            [this](const QString& to, const QString& topic,
                   const QByteArray& payload) {
                if (m_bus_send_cb) m_bus_send_cb(to, topic, payload);
            });
    }
}

void NativeBackend::on_plugin_message(const QString& from, const QString& topic,
                                      const QByteArray& payload) {
    if (m_bus_aware) m_bus_aware->on_plugin_message(from, topic, payload);
}

QString NativeBackend::protocol_id() const {
    return m_protocol_id;
}

ParseResult NativeBackend::parse(const BplcFrame& frame, MsduState& msdu,
                                 const ParseFilter& filter, QString* err) {
    if (!m_parser) {
        *err = QStringLiteral("no parser");
        return ParseResult();
    }
    Q_UNUSED(err);
    return m_parser->parse(frame, msdu, filter);
}

bool NativeBackend::has_graphics() const {
    return m_graphics && m_graphics->has_graphics();
}

QSize NativeBackend::graphics_preferred_size() const {
    if (m_graphics) return m_graphics->preferred_size();
    return QSize(400, 300);
}

QImage NativeBackend::render_graphics(int w, int h, QString* err) {
    if (!m_graphics || !m_graphics->has_graphics()) {
        *err = QStringLiteral("no graphics");
        return QImage();
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        *err = QStringLiteral("bad size");
        return QImage();
    }
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    m_graphics->render(&p, w, h);
    p.end();
    return img;
}

bool NativeBackend::handle_graphics_event(const GraphicsEvent& e, QString* err) {
    if (!m_graphics || !m_graphics->has_graphics()) {
        *err = QStringLiteral("no graphics");
        return false;
    }
    return m_graphics->handle_event(e);
}

void NativeBackend::set_ui_dark(bool dark) {
    IPluginBackend::set_ui_dark(dark);
    if (m_graphics) m_graphics->set_dark(dark);
}
