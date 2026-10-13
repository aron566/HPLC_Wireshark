/// @file dissector_plugin.cpp
/// @brief 南/国网参考解剖器插件（一个实现，两个打包：gw_dissector/nw_dissector）
/// @details 内部复用主程序内置 GW_2022 / NW_2021 解析器完成真实解码，
///          再由 field_gen 把 ParseResult 转成通用 DissectField 字段树。
///          证明新解剖器架构（净荷+FrameContext 进、结构化字段出）能与
///          内置解析器达到逐字段一致的效果（dissect_compare_test 校验）。
///          变体由 .pro 的 DEFINES 选择：DISSECTOR_NW=南网，否则国网。
#include <QObject>
#include <memory>

#include "column_gen.h"
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
    QVector<DissectColumn> dissector_columns() const override {
        return dissect_gen::reference_columns();
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
        out.dissected_bytes = r.payload_for_log;   // 字段坐标基准(剥前缀后)
        // 精化帧外信息:内置解析从媒介头解出的 NTB/通道/RF 标志
        out.has_refined_ctx = r.accept;
        out.refined_ctx = ctx;
        out.refined_ctx.ntb = r.meta.timestamp;
        out.refined_ctx.channel = r.meta.channel;
        out.refined_ctx.is_rf = r.meta.is_rf;
        out.topo_event = r.msdu.topo_event;
        out.tei_mac_pairs = r.msdu.tei_mac_pairs;
        // 列值:先以本帧学习对更新自持 TEI→MAC 表(与宿主模型 append
        // 时先学习后渲染同序),再按列能力生成本帧各列文本
        dissect_gen::update_tei_mac(m_tei_mac, r);
        out.column_values = dissect_gen::reference_column_values(r, m_tei_mac);
        if (r.accept) {
            // 字段生成器的字节坐标以剥前缀后的 MPDU(payload_for_log)为准,
            // 与内置构建器读 e.raw_bytes 的口径一致
#if defined(DISSECTOR_NW)
            out.fields = dissect_gen::nw_fields(r.payload_for_log, r);
#else
            out.fields = dissect_gen::gw_fields(r.payload_for_log, r);
#endif
        }
        return out;
    }
    void dissect_reset() override {
        m_msdu_state = MsduState();
        m_tei_mac.clear();   // 列值用的 TEI→MAC 学习表随新会话清空
    }

private:
    std::unique_ptr<DISSECTOR_PARSER_T> m_parser;
    MsduState m_msdu_state;   ///< 跨帧重组状态（插件自持）
    dissect_gen::TeiMacMap m_tei_mac;   ///< 列值 MAC 后缀的学习表（插件自持）
};

#include "dissector_plugin.moc"
