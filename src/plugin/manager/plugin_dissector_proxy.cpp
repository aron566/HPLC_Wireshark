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
    r.payload_for_log = in.data;   // 详情 Hex/字段坐标空间=去封装净荷

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
    r.mpdu = dr.mpdu;
    r.msdu.present = dr.msdu_present;
    r.msdu.summary = dr.msdu_summary;
    r.msdu.topo_event = dr.topo_event;
    r.msdu.tei_mac_pairs = dr.tei_mac_pairs;
    r.msdu_body = dr.msdu_body;
    r.msdu_raw_base = dr.msdu_raw_base;
    r.dissect_fields = dr.fields;
    return r;
}

std::unique_ptr<IProtocolParser> make_dissector_parser(
    const QString& protocol_id) {
    if (!PluginManager::instance().is_dissector_protocol(protocol_id))
        return nullptr;
    return std::make_unique<PluginDissectorProxy>(protocol_id);
}
