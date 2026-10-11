/// @file dissector_plugin.cpp
/// @brief 南/国网参考解剖器插件（一个实现，两个打包：gw_dissector/nw_dissector）
/// @details 内部复用主程序内置 GW_2022 / NW_2021 解析器完成真实解码，
///          再由 field_gen 把 ParseResult 转成通用 DissectField 字段树。
///          证明新解剖器架构（净荷+FrameContext 进、结构化字段出）能与
///          内置解析器达到逐字段一致的效果（dissect_compare_test 校验）。
///          变体由 .pro 的 DEFINES 选择：DISSECTOR_NW=南网，否则国网。
#include <QObject>
#include <memory>

#include "field_gen.h"
#include "idissectorplugin.h"
#include "iplugin.h"
#include "iprotocolparserplugin.h"

#if defined(DISSECTOR_NW)
#include "nw_2021_parser.h"
#define DISSECTOR_PARSER_T NW_2021_Parser
#define DISSECTOR_PROTO_ID "NW_2021_PLUGIN"
#define DISSECTOR_PLUGIN_ID "nw-dissector"
#define DISSECTOR_NAME_ZH "南网 NW_2021 解剖器"
#define DISSECTOR_NAME_EN "NW_2021 Dissector (Plugin)"
#else
#include "gw_2022_parser.h"
#define DISSECTOR_PARSER_T GW_2022_Parser
#define DISSECTOR_PROTO_ID "GW_2022_PLUGIN"
#define DISSECTOR_PLUGIN_ID "gw-dissector"
#define DISSECTOR_NAME_ZH "国网 GW_2022 解剖器"
#define DISSECTOR_NAME_EN "GW_2022 Dissector (Plugin)"
#endif

class ReferenceDissectorPlugin : public QObject,
                                 public IProtocolParserPlugin,
                                 public IDissectorPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID BPLC_PARSER_PLUGIN_IID)
    Q_INTERFACES(IPlugin IProtocolParserPlugin IDissectorPlugin)
public:
    // ---- IPlugin ----
    QString id() const override { return QStringLiteral(DISSECTOR_PLUGIN_ID); }
    QString display_name() const override {
        return QStringLiteral(DISSECTOR_NAME_ZH);
    }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QString description() const override {
        return QStringLiteral("参考解剖器:复用内置协议解析,返回通用结构化字段");
    }
    QString author() const override { return QStringLiteral("BPLC Team"); }
    bool initialize() override {
        m_parser = std::make_unique<DISSECTOR_PARSER_T>();
        return true;
    }
    void shutdown() override {
        m_parser.reset();
    }

    // ---- IProtocolParserPlugin（旧路径兼容:create_parser 返回真实解析器） ----
    QString protocol_id() const override {
        return QStringLiteral(DISSECTOR_PROTO_ID);
    }
    IProtocolParser* create_parser() override {
        return new DISSECTOR_PARSER_T();
    }

    // ---- IDissectorPlugin ----
    QString dissector_protocol_id() const override {
        return QStringLiteral(DISSECTOR_PROTO_ID);
    }
    DissectResult dissect(const QByteArray& payload,
                          const FrameContext& ctx) override {
        DissectResult out;
        if (!m_parser) {
            out.reject_reason = QStringLiteral("dissector not initialized");
            return out;
        }
        // 由净荷 + 帧外信息重建解析器输入（FrameContext 的用途示例:
        // is_rf/channel/时间戳都来自主程序告知,净荷本身不含这些）
        BplcFrame frame;
        frame.data = payload;
        frame.meta.is_rf = ctx.is_rf;
        frame.meta.channel = quint16(ctx.channel < 0 ? 0 : ctx.channel);
        frame.meta.timestamp = ctx.ntb;
        frame.meta.epoch_ms = ctx.epoch_ms;
        frame.meta.seg_start = ctx.seg_start;
        frame.arrival_us = ctx.arrival_us;

        const ParseFilter filter;
        const ParseResult r = m_parser->parse(frame, m_msdu_state, filter);

        out.accept = r.accept;
        out.reject_reason = r.reject_reason;
        out.mpdu = r.mpdu;
        out.msdu_present = r.msdu.present;
        out.msdu_summary = r.msdu.summary;
        out.msdu_body = r.msdu_body;
        out.msdu_raw_base = r.msdu_raw_base;
        out.topo_event = r.msdu.topo_event;
        out.tei_mac_pairs = r.msdu.tei_mac_pairs;
        if (r.accept) {
#if defined(DISSECTOR_NW)
            out.fields = dissect_gen::nw_fields(payload, r);
#else
            out.fields = dissect_gen::gw_fields(payload, r);
#endif
        }
        return out;
    }
    void dissect_reset() override { m_msdu_state = MsduState(); }

private:
    std::unique_ptr<DISSECTOR_PARSER_T> m_parser;
    MsduState m_msdu_state;   ///< 跨帧重组状态（插件自持）
};

#include "dissector_plugin.moc"
