/// @file plugin_dissector_proxy.cpp
/// @brief 插件解剖器代理实现
#include "plugin_dissector_proxy.h"

#include "dissect.h"
#include "plugin_manager.h"

ParseResult PluginDissectorProxy::parse(const BplcFrame& in, MsduState&,
                                        const ParseFilter&) {
    ParseResult r;
    r.meta = in.meta;
    r.raw_wire = in.raw_wire;
    r.arrival_us = in.arrival_us;

    FrameContext ctx;
    ctx.epoch_ms = in.meta.epoch_ms;
    ctx.arrival_us = in.arrival_us;
    ctx.ntb = in.meta.timestamp;
    ctx.is_rf = in.meta.is_rf;
    ctx.channel = in.meta.channel;
    ctx.frame_index = ++m_frame_counter;
    ctx.seg_start = in.meta.seg_start;

    DissectResult dr;
    if (!PluginManager::instance().dissect_via_plugin(m_protocol_id, in.data,
                                                      ctx, &dr)) {
        r.accept = false;
        r.reject_reason = QStringLiteral("plugin dissector unavailable");
        return r;
    }
    r.accept = dr.accept;
    r.reject_reason = dr.reject_reason;
    // 详情 Hex/字段坐标空间=解剖器实际解剖的字节(与内置 payload_for_log 同义)
    r.payload_for_log = dr.dissected_bytes.isEmpty() ? in.data
                                                     : dr.dissected_bytes;
    // 解剖器从媒介头精化出的帧外信息覆盖 reader 的粗值(Physical 段来源)
    if (dr.has_refined_ctx) {
        r.meta.timestamp = dr.refined_ctx.ntb;
        r.meta.channel = dr.refined_ctx.channel;
        r.meta.is_rf = dr.refined_ctx.is_rf;
    }
    r.mpdu = dr.mpdu;
    r.msdu.present = dr.msdu_present;
    r.msdu.summary = dr.msdu_summary;
    r.msdu.topo_event = dr.topo_event;
    r.msdu.tei_mac_pairs = dr.tei_mac_pairs;
    r.msdu_body = dr.msdu_body;
    r.msdu_raw_base = dr.msdu_raw_base;
    r.dissect_fields = dr.fields;
    r.column_values = dr.column_values;
    r.color_id = dr.color_id;
    return r;
}

std::unique_ptr<IProtocolParser> make_dissector_parser(
    const QString& protocol_id) {
    if (!PluginManager::instance().is_dissector_protocol(protocol_id))
        return nullptr;
    return std::make_unique<PluginDissectorProxy>(protocol_id);
}
