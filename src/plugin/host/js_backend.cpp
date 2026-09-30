/// @file js_backend.cpp
#include "js_backend.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>

#include "script_painter.h"

namespace {
// QByteArray → JS 数字数组
QJSValue byte_array_to_js(QJSEngine& e, const QByteArray& b) {
    QJSValue arr = e.newArray(b.size());
    for (int i = 0; i < b.size(); ++i)
        arr.setProperty(i, static_cast<quint8>(b[i]));
    return arr;
}
quint16 js_uint16(const QJSValue& o, const char* k, quint16 d = 0) {
    const QJSValue v = o.property(QString::fromLatin1(k));
    return v.isNumber() ? static_cast<quint16>(v.toUInt()) : d;
}
quint8 js_uint8(const QJSValue& o, const char* k, quint8 d = 0) {
    const QJSValue v = o.property(QString::fromLatin1(k));
    return v.isNumber() ? static_cast<quint8>(v.toUInt()) : d;
}
quint32 js_uint32(const QJSValue& o, const char* k, quint32 d = 0) {
    const QJSValue v = o.property(QString::fromLatin1(k));
    return v.isNumber() ? static_cast<quint32>(v.toUInt()) : d;
}
} // namespace

bool JsBackend::check_error(const QJSValue& v, QString* err,
                           const QString& ctx) {
    if (v.isError()) {
        *err = QStringLiteral("%1: %2").arg(ctx, v.toString());
        return false;
    }
    return true;
}

bool JsBackend::initialize(const PluginManifest& m, QString* err) {
    if (m.api_version != 1) {
        *err = QStringLiteral("api_version mismatch");
        return false;
    }
    const QString js_path = QDir(m.dir_path).filePath(m.entry);
    QFile f(js_path);
    if (!f.open(QIODevice::ReadOnly)) {
        *err = QStringLiteral("cannot open %1").arg(m.entry);
        return false;
    }
    const QString code = QString::fromUtf8(f.readAll());
    const QJSValue r = m_engine.evaluate(code, js_path);
    if (!check_error(r, err, QStringLiteral("evaluate"))) return false;

    // 全局 request_redraw():脚本主动请求重绘(QObject 桥)
    m_redraw_helper = new RedrawHelper();
    m_redraw_helper->cb = [this]() {
        if (m_redraw_cb) m_redraw_cb();
    };
    m_engine.globalObject().setProperty(
        QStringLiteral("__bplc_redraw"),
        m_engine.newQObject(m_redraw_helper));
    m_engine.evaluate(
        QStringLiteral("function request_redraw(){__bplc_redraw.request();}"));

    // get_info()
    QJSValue get_info = m_engine.globalObject().property("get_info");
    if (!get_info.isCallable()) {
        *err = QStringLiteral("missing function get_info()");
        return false;
    }
    const QJSValue info = get_info.call();
    if (!check_error(info, err, QStringLiteral("get_info"))) return false;
    m_protocol_id = info.property("protocolId").toString();
    m_display_name = info.property("displayName").toString();
    if (m_protocol_id.isEmpty()) {
        *err = QStringLiteral("get_info() missing protocolId");
        return false;
    }
    // 清单与脚本声明一致性校验
    if (m_protocol_id != m.protocol_id) {
        *err = QStringLiteral("protocolId mismatch: manifest=%1 script=%2")
                   .arg(m.protocol_id, m_protocol_id);
        return false;
    }

    m_parse_fn = m_engine.globalObject().property("parse");
    if (!m_parse_fn.isCallable()) {
        *err = QStringLiteral("missing function parse(frame)");
        return false;
    }

    // 图形函数(可选,manifest graphics=true 时必需)
    m_render_fn = m_engine.globalObject().property("render");
    m_on_event_fn = m_engine.globalObject().property("on_event");
    m_has_graphics = m_render_fn.isCallable();
    if (m.graphics && !m_has_graphics) {
        *err = QStringLiteral("manifest graphics=true but missing function render(p,w,h)");
        return false;
    }
    return true;
}

void JsBackend::shutdown() {
    m_parse_fn = QJSValue();
    m_render_fn = QJSValue();
    m_on_event_fn = QJSValue();
    m_has_graphics = false;
    // m_redraw_helper 由 engine 拥有(newQObject),engine 析构时清理
    m_redraw_helper = nullptr;
    // engine 析构自动清理
}

QString JsBackend::protocol_id() const {
    return m_protocol_id;
}

bool JsBackend::has_graphics() const {
    return m_has_graphics;
}

QSize JsBackend::graphics_preferred_size() const {
    return QSize(400, 300);
}

QImage JsBackend::render_graphics(int w, int h, QString* err) {
    if (!m_has_graphics) {
        *err = QStringLiteral("no graphics");
        return QImage();
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        *err = QStringLiteral("bad size");
        return QImage();
    }
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    ScriptPainter bridge(&painter);
    QJSValue js_painter = m_engine.newQObject(&bridge);
    const QJSValue r = m_render_fn.call(
        QJSValueList{js_painter, QJSValue(w), QJSValue(h)});
    painter.end();
    if (!check_error(r, err, QStringLiteral("render"))) return QImage();
    return img;
}

bool JsBackend::handle_graphics_event(const GraphicsEvent& e, QString* err) {
    if (!m_has_graphics || !m_on_event_fn.isCallable()) return false;
    const QJSValue r = m_on_event_fn.call(QJSValueList{
        QJSValue(static_cast<int>(e.type)), QJSValue(e.x), QJSValue(e.y),
        QJSValue(e.button), QJSValue(e.modifiers), QJSValue(e.delta_y)});
    if (!check_error(r, err, QStringLiteral("on_event"))) return false;
    return r.toBool();
}

bool JsBackend::convert_fields(const QJSValue& js_fields,
                              QVector<MsduFieldNode>* out, QString* err) {
    if (!js_fields.isArray()) {
        *err = QStringLiteral("fields must be an array");
        return false;
    }
    const quint32 n = js_fields.property("length").toUInt();
    for (quint32 i = 0; i < n; ++i) {
        const QJSValue jf = js_fields.property(i);
        MsduFieldNode node;
        node.name = jf.property("name").toString();
        node.value = jf.property("value").toString();
        const QJSValue rs = jf.property("relStart");
        node.rel_start = rs.isNumber() ? rs.toInt() : -1;
        const QJSValue rl = jf.property("relLen");
        node.rel_len = rl.isNumber() ? rl.toInt() : 0;
        const QJSValue children = jf.property("children");
        if (!children.isUndefined()) {
            if (!convert_fields(children, &node.children, err)) return false;
        }
        out->append(node);
    }
    return true;
}

ParseResult JsBackend::parse(const BplcFrame& frame, MsduState& msdu,
                             const ParseFilter& filter, QString* err) {
    Q_UNUSED(msdu);   // Phase2:JS 插件不支持跨帧重组
    Q_UNUSED(filter);
    ParseResult r;
    r.meta = frame.meta;
    r.raw_wire = frame.raw_wire;
    r.arrival_us = frame.arrival_us;
    r.payload_for_log = frame.data;

    // 构造 frame 对象
    QJSValue js_frame = m_engine.newObject();
    js_frame.setProperty("data", byte_array_to_js(m_engine, frame.data));
    js_frame.setProperty("rawWire", byte_array_to_js(m_engine, frame.raw_wire));
    js_frame.setProperty("arrivalUs",
                         QJSValue(static_cast<double>(frame.arrival_us)));

    const QJSValue res = m_parse_fn.call(QJSValueList{js_frame});
    if (!check_error(res, err, QStringLiteral("parse"))) {
        r.accept = false;
        r.reject_reason = *err;
        return r;
    }
    if (!res.isObject()) {
        *err = QStringLiteral("parse() must return an object");
        r.accept = false;
        r.reject_reason = *err;
        return r;
    }

    r.accept = res.property("accept").toBool();
    if (!r.accept) {
        r.reject_reason = res.property("rejectReason").toString();
        if (r.reject_reason.isEmpty())
            r.reject_reason = QStringLiteral("rejected by plugin");
        return r;
    }

    // MPDU 基础字段(可选)
    const QJSValue mpdu = res.property("mpdu");
    if (mpdu.isObject()) {
        r.mpdu.frame_type = js_uint8(mpdu, "frameType");
        r.mpdu.src_tei = js_uint16(mpdu, "srcTei");
        r.mpdu.dst_tei = js_uint16(mpdu, "dstTei");
        r.mpdu.net_id = js_uint32(mpdu, "netId");
    } else if (!frame.data.isEmpty()) {
        r.mpdu.frame_type = 1;
    }

    // MSDU 字段树
    r.msdu.present = true;
    r.msdu.summary = res.property("summary").toString();
    const QJSValue fields = res.property("fields");
    if (!fields.isUndefined()) {
        if (!convert_fields(fields, &r.msdu.tree, err)) {
            r.accept = false;
            r.reject_reason = *err;
            r.msdu.tree.clear();
            return r;
        }
    }
    return r;
}
