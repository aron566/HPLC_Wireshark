/// @file js_backend.cpp
#include "js_backend.h"

#include <QDebug>
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

// 从 0x3C 帧中提取 MPDU 并解析 TEI
// 0x3C 帧格式: [0]=0x3C [1-2]=len [3-6]=timestamp [7..]=payload
// payload: [0]=phr_mcs [1]=option [2]=channel [3]=media_id [4..]=MPDU
// MPDU: byte0 bits0-2=frame_type, byte8 12bits=src_tei (SOF/BEACON)
struct MpduQuick {
    bool ok = false;
    quint8 frame_type = 0;
    quint16 src_tei = 0;
    quint16 dst_tei = 0;
};

static quint32 get_bits_q(const quint8* p, int byte_off, int bit_off, int nbits) {
    quint32 v = 0;
    for (int i = 0; i < nbits; ++i) {
        int bit = byte_off * 8 + bit_off + i;
        int b = bit / 8, o = bit % 8;
        if (p[b] & (1 << o)) v |= (1u << i);
    }
    return v;
}

MpduQuick parse_mpdu_quick(const QByteArray& frame) {
    MpduQuick r;
    if (frame.size() < 14) return r;
    const quint8* d = reinterpret_cast<const quint8*>(frame.constData());
    if (d[0] != 0x3C) return r;

    // 跳过 0x3C 头: [1-2]=len, [3-6]=timestamp(4B), [7..]=payload
    // payload[0..3]=媒介头, [4..]=MPDU
    int mpdu_off = 1 + 2 + 4 + 4;  // =11
    if (frame.size() < mpdu_off + 16) return r;

    const quint8* p = d + mpdu_off;
    r.frame_type = (quint8)get_bits_q(p, 0, 0, 3);
    // SOF(1)/BEACON(0): src_tei 在 byte8 的 12 bits
    if (r.frame_type == 0 || r.frame_type == 1) {
        r.src_tei = (quint16)get_bits_q(p, 8, 0, 12);
        r.ok = true;
    }
    // SOF: dst_tei 也在附近 (简化: 尝试 byte10)
    if (r.frame_type == 1 && frame.size() >= mpdu_off + 12) {
        r.dst_tei = (quint16)get_bits_q(p, 10, 4, 12);
    }
    return r;
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

// TopoEventKind → JS 可读字符串
QString topo_kind_name(TopoEventKind k) {
    switch (k) {
    case TopoEventKind::DiscoverList:  return QStringLiteral("discoverList");
    case TopoEventKind::AssocReq:      return QStringLiteral("assocReq");
    case TopoEventKind::AssocCnf:      return QStringLiteral("assocCnf");
    case TopoEventKind::AssocGatherInd:return QStringLiteral("assocGatherInd");
    case TopoEventKind::AssocInd:      return QStringLiteral("assocInd");
    case TopoEventKind::ChangeProxyReq:return QStringLiteral("changeProxyReq");
    case TopoEventKind::ChangeProxyCnf:return QStringLiteral("changeProxyCnf");
    case TopoEventKind::LeaveInd:      return QStringLiteral("leaveInd");
    case TopoEventKind::SuccessRate:   return QStringLiteral("successRate");
    case TopoEventKind::CcoRestart:    return QStringLiteral("ccoRestart");
    case TopoEventKind::StaRestart:    return QStringLiteral("staRestart");
    default:                          return QStringLiteral("other");
    }
}

// MAC 48-bit → "aa:bb:cc:dd:ee:ff"(帧内字节序,与原版 TopoWindow 一致)
QString format_mac_evt(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QStringLiteral("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += QLatin1Char(':');
    }
    return s;
}
} // namespace

// TopoEvent → JS 对象(供 frame.topoEvent)。定义在 namespace 外,parse 先用。
static QJSValue build_topo_event_object(QJSEngine& eng, const TopoEvent& e);

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

    // host 界面控制对象:host.jumpToFrame(frameIndex)
    m_host_helper = new HostHelper();
    m_host_helper->jump_cb = [this](qint64 idx) {
        if (m_host_jump_cb) m_host_jump_cb(idx);
    };
    m_engine.globalObject().setProperty(
        QStringLiteral("host"),
        m_engine.newQObject(m_host_helper));

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
    // m_redraw_helper/m_host_helper 由 engine 拥有(newQObject),engine 析构时清理
    m_redraw_helper = nullptr;
    m_host_helper = nullptr;
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

    // MPDU 快速解析: 提供准确的 TEI 信息给 JS 插件
    MpduQuick mq = parse_mpdu_quick(frame.data);
    QJSValue js_mpdu = m_engine.newObject();
    js_mpdu.setProperty("ok", QJSValue(mq.ok));
    js_mpdu.setProperty("frameType", QJSValue(mq.frame_type));
    js_mpdu.setProperty("srcTei", QJSValue(mq.src_tei));
    js_mpdu.setProperty("dstTei", QJSValue(mq.dst_tei));
    js_frame.setProperty("mpdu", js_mpdu);

    // 主程序解码信息(单入口):帧序号/时间戳/本帧拓扑事件(无事件=null)
    js_frame.setProperty("index",
                         QJSValue(static_cast<double>(frame.decoded_index)));
    js_frame.setProperty("epochMs",
                         QJSValue(static_cast<double>(frame.decoded_epoch_ms)));
    if (frame.topo_event.kind != TopoEventKind::Other)
        js_frame.setProperty("topoEvent",
                             build_topo_event_object(m_engine, frame.topo_event));
    else
        js_frame.setProperty("topoEvent",
                             QJSValue(QJSValue::NullValue));

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

/// @brief TopoEvent → JS 对象(供 frame.topoEvent)
static QJSValue build_topo_event_object(QJSEngine& eng, const TopoEvent& e) {
    QJSValue evt = eng.newObject();
    evt.setProperty(QStringLiteral("kind"), QJSValue(topo_kind_name(e.kind)));
    evt.setProperty(QStringLiteral("nid"), QJSValue(e.nid));
    // ccoMac:只有携带 CCO 的事件(discoverList 类)才有值,无 CCO 时为 null
    // (零 MAC 发 "00:.." 空字符串会误触发脚本侧 if(evt.ccoMac) 守卫)
    if (e.cco_mac != 0)
        evt.setProperty(QStringLiteral("ccoMac"),
                        QJSValue(format_mac_evt(e.cco_mac)));
    else
        evt.setProperty(QStringLiteral("ccoMac"),
                        QJSValue(QJSValue::NullValue));
    QJSValue js_nodes = eng.newArray(e.nodes.size());
    for (int i = 0; i < e.nodes.size(); ++i) {
        QJSValue n = eng.newObject();
        n.setProperty(QStringLiteral("tei"), QJSValue(e.nodes[i].tei));
        n.setProperty(QStringLiteral("mac"),
                      QJSValue(format_mac_evt(e.nodes[i].mac)));
        js_nodes.setProperty(i, n);
    }
    evt.setProperty(QStringLiteral("nodes"), js_nodes);
    QJSValue js_routes = eng.newArray(e.routes.size());
    for (int i = 0; i < e.routes.size(); ++i) {
        QJSValue r = eng.newObject();
        r.setProperty(QStringLiteral("child"), QJSValue(e.routes[i].first));
        r.setProperty(QStringLiteral("parent"), QJSValue(e.routes[i].second));
        js_routes.setProperty(i, r);
    }
    evt.setProperty(QStringLiteral("routes"), js_routes);
    // upRoutes:发现列表上行路由,解析层已只保留 RouteType=3(代理主路径)
    QJSValue js_up = eng.newArray(e.up_routes.size());
    for (int i = 0; i < e.up_routes.size(); ++i) {
        QJSValue r = eng.newObject();
        r.setProperty(QStringLiteral("sta"), QJSValue(e.up_routes[i].first));
        r.setProperty(QStringLiteral("nextHop"), QJSValue(e.up_routes[i].second));
        js_up.setProperty(i, r);
    }
    evt.setProperty(QStringLiteral("upRoutes"), js_up);
    evt.setProperty(QStringLiteral("discoverSrcTei"),
                    QJSValue(e.discover_src_tei));
    QJSValue js_nb = eng.newArray(e.neighbor_teis.size());
    for (int i = 0; i < e.neighbor_teis.size(); ++i)
        js_nb.setProperty(i, QJSValue(e.neighbor_teis[i]));
    evt.setProperty(QStringLiteral("neighborTeis"), js_nb);
    QJSValue js_leaves = eng.newArray(e.leaves.size());
    for (int i = 0; i < e.leaves.size(); ++i)
        js_leaves.setProperty(i, QJSValue(format_mac_evt(e.leaves[i])));
    evt.setProperty(QStringLiteral("leaves"), js_leaves);
    QJSValue js_cr = eng.newArray(e.comm_rates.size());
    for (int i = 0; i < e.comm_rates.size(); ++i) {
        QJSValue c = eng.newObject();
        c.setProperty(QStringLiteral("tei"), QJSValue(e.comm_rates[i].tei));
        c.setProperty(QStringLiteral("down"), QJSValue(e.comm_rates[i].down));
        c.setProperty(QStringLiteral("up"), QJSValue(e.comm_rates[i].up));
        js_cr.setProperty(i, c);
    }
    evt.setProperty(QStringLiteral("commRates"), js_cr);
    evt.setProperty(QStringLiteral("isRf"), QJSValue(e.is_rf));
    evt.setProperty(QStringLiteral("restartCount"), QJSValue(e.restart_count));
    evt.setProperty(QStringLiteral("desc"), QJSValue(e.desc));
    evt.setProperty(QStringLiteral("epochMs"),
                    QJSValue(static_cast<double>(e.epoch_ms)));
    evt.setProperty(QStringLiteral("frameIndex"),
                    QJSValue(static_cast<double>(e.frame_index)));
    return evt;
}

QString JsBackend::call_text_function(const char* name, QString* err) {
    const QJSValue fn =
        m_engine.globalObject().property(QString::fromLatin1(name));
    if (!fn.isCallable()) {
        *err = QStringLiteral("function %1 not found").arg(QLatin1String(name));
        return QString();
    }
    const QJSValue r = fn.call();
    if (!check_error(r, err, QLatin1String(name))) return QString();
    if (r.isArray()) {  // 数组按行拼接(如 get_replay_data 返回字符串数组)
        QStringList parts;
        const quint32 n = r.property(QStringLiteral("length")).toUInt();
        for (quint32 i = 0; i < n; ++i)
            parts.append(r.property(i).toString());
        return parts.join(QLatin1Char('\n'));
    }
    return r.toString();
}

void JsBackend::notify_frame_selected(qint64 frameIndex, bool force_history) {
    const QJSValue fn =
        m_engine.globalObject().property(QStringLiteral("on_frame_selected"));
    if (!fn.isCallable()) return;  // 脚本不关心帧选中:静默忽略
    const QJSValue r = fn.call(QJSValueList{
        QJSValue(static_cast<double>(frameIndex)), QJSValue(force_history)});
    QString err;
    if (!check_error(r, &err, QStringLiteral("on_frame_selected")))
        qWarning() << "JsBackend::notify_frame_selected:" << err;
}
