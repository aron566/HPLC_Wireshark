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
/// @note JS 插件不支持 SOF 跨帧重组(Phase2 限制),MsduState 透传不使用。
#ifndef BPLC_JS_BACKEND_H
#define BPLC_JS_BACKEND_H

#include <QJSEngine>
#include <QJSValue>

#include "plugin_backend.h"

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

private:
    /// @brief JS fields 数组 → MsduFieldNode 树(递归)
    bool convert_fields(const QJSValue& js_fields, QVector<MsduFieldNode>* out,
                       QString* err);
    /// @brief 抛异常/非法的 JS 返回 → err
    static bool check_error(const QJSValue& v, QString* err,
                           const QString& ctx);

    QJSEngine m_engine;
    QJSValue m_parse_fn;
    QString m_protocol_id;
    QString m_display_name;
};

#endif // BPLC_JS_BACKEND_H
