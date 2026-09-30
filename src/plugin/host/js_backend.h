/// @file js_backend.h
/// @brief JsBackend:QJSEngine 运行 JS 插件
/// @details JS 插件 API v1:
///   function get_info() -> { protocolId: "X", displayName: "..." }
///   function parse(frame) -> {
///     accept: bool, rejectReason?: string, summary?: string,
///     mpdu?: { frameType, srcTei, dstTei, netId },
///     fields?: [{ name, value, relStart?, relLen?, children?: [...] }]
///   }
///   frame = { data: [byte...], rawWire: [byte...], arrivalUs: number }
/// 图形 API(Phase3, manifest graphics=true 时):
///   function render(p, w, h)  // p: ScriptPainter
///   function on_event(type, x, y, button, modifiers, delta) -> bool(needs_redraw)
///   type: 0=press 1=release 2=move 3=wheel 4=resize 5=leave
/// @note JS 插件不支持 SOF 跨帧重组(Phase2 限制),MsduState 透传不使用。
#ifndef BPLC_JS_BACKEND_H
#define BPLC_JS_BACKEND_H

#include <QJSEngine>
#include <QJSValue>
#include <QObject>

#include <functional>

#include "plugin_backend.h"

/// @brief request_redraw 的 QObject 桥(Qt6.5 QJSEngine 无 newFunction)
class RedrawHelper : public QObject {
    Q_OBJECT
public:
    std::function<void()> cb;
public slots:
    void request() { if (cb) cb(); }
};

/// @brief JS 插件后端
class JsBackend : public IPluginBackend {
public:
    JsBackend() = default;
    ~JsBackend() override = default;

    bool initialize(const PluginManifest& m, QString* err) override;
    void shutdown() override;
    QString protocol_id() const override;
    ParseResult parse(const BplcFrame& frame, MsduState& msdu,
                      const ParseFilter& filter, QString* err) override;

    // ---- 图形(Phase3) ----
    bool has_graphics() const override;
    QSize graphics_preferred_size() const override;
    QImage render_graphics(int w, int h, QString* err) override;
    bool handle_graphics_event(const GraphicsEvent& e, QString* err) override;

private:
    bool convert_fields(const QJSValue& js_fields, QVector<MsduFieldNode>* out,
                        QString* err);
    static bool check_error(const QJSValue& v, QString* err,
                            const QString& ctx);

    QJSEngine m_engine;
    QJSValue m_parse_fn;
    QJSValue m_render_fn;
    QJSValue m_on_event_fn;
    RedrawHelper* m_redraw_helper = nullptr;  ///< engine 拥有
    bool m_has_graphics = false;
    QString m_protocol_id;
    QString m_display_name;
};

#endif // BPLC_JS_BACKEND_H
