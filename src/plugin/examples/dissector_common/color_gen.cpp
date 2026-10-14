/// @file color_gen.cpp
/// @brief 参考解剖器着色配置:逐项转录 PacketListModel 的着色规则
///        (mme_color/bid_color 哈希公式与 data_color 帧型基色),
///        保证插件模式整行颜色与内置模式逐帧一致(由
///        export_consistency_test 逐帧比对前景色验证)。
#include "color_gen.h"

#include <QColor>

namespace dissect_gen {
namespace {

DissectColorRule rule(const char* id, const QString& name,
                      const QString& name_en, const QColor& fg) {
    DissectColorRule r;
    r.id = QString::fromLatin1(id);
    r.name = name;
    r.name_en = name_en;
    r.foreground = fg.rgba();
    r.background = 0;   // 当前着色配置只设前景
    return r;
}

/// 管理消息颜色(与宿主 mme_color 同公式:暖色系,避开红色区)
QColor mme_color(quint16 t) {
    const int hue = 20 + int((quint64(t) * 37) % 160);
    return QColor::fromHsv(hue, 190, 220);
}

/// 应用层 BID 颜色(与宿主 bid_color 同公式:冷色系)
QColor bid_color(quint8 bid) {
    const int hue = 180 + int((quint64(bid) * 37) % 160);
    return QColor::fromHsv(hue, 190, 220);
}

QString hex_id(const char* prefix, quint32 v, int width) {
    return QString::fromLatin1(prefix)
         + QStringLiteral("%1").arg(v, width, 16, QChar('0'));
}

struct MmeName { quint16 type; const char* name; };

#if defined(DISSECTOR_NW)
/// 南网 MMe 类型命名(转录 nw_2021_msdu_parser::mme_type_name)
const MmeName kMmeNames[] = {
    {0x0030, "MMeAssocReq"}, {0x0031, "MMeAssocCnf"},
    {0x0032, "MMeChangeProxyReq"}, {0x0034, "MMeAssocInd"},
    {0x0037, "MMeChangeProxyCnf"}, {0x003A, "MMeAssocGatherInd"},
    {0x003B, "MMeChangeProxyBitMapCnf"}, {0x0049, "MMeLeaveInd"},
    {0x0051, "MMeHeartBeatCheck"}, {0x0055, "MMeDiscoverNodeList"},
    {0x005D, "MMeDelayLeaveInd"}, {0x005E, "MMeSuccessRateReport"},
    {0x005F, "MMeNetworkConflictReport"},
    {0x0062, "MMeZeroCrossNTBCollectInd"},
    {0x0063, "MMeZeroCrossNTBReport"}, {0x0064, "MMeNetDiagnose"},
    {0x0070, "MMeRFChannelConflictReport"},
};
#else
/// 国网 MMe 类型命名(转录 gw_2022_msdu_parser::mme_type_name)
const MmeName kMmeNames[] = {
    {0x00, "MMeAssocReq"}, {0x01, "MMeAssocCnf"},
    {0x02, "MMeAssocGatherInd"}, {0x03, "MMeChangeProxyReq"},
    {0x04, "MMeChangeProxyCnf"}, {0x05, "MMeChangeProxyBitMapCnf"},
    {0x06, "MMeLeaveInd"}, {0x07, "MMeHeartBeatCheck"},
    {0x08, "MMeDiscoveryNodeList"}, {0x09, "MMeSuccessRateReport"},
    {0x0A, "MMeNetworkConflictReport"},
    {0x0B, "MMeZeroCrossNTBCollectInd"}, {0x0C, "MMeZeroCrossNTBReport"},
    {0x4F, "MMeDiagnose"}, {0x50, "MMeRouteRequest"},
    {0x51, "MMeRouteReply"}, {0x52, "MMeRouteError"},
    {0x53, "MMeRouteAck"}, {0x54, "MMeLinkConfirmRequest"},
    {0x55, "MMeLinkConfirmResponse"}, {0x80, "MMeRFChannelConflictReport"},
};
#endif

const char* mme_name(quint16 t) {
    for (const MmeName& n : kMmeNames)
        if (n.type == t) return n.name;
    return nullptr;
}

/// MMe 规则 id:国网类型为 quint8(2 位 hex),南网 quint16(4 位 hex)
QString mme_id(quint16 t) {
#if defined(DISSECTOR_NW)
    return hex_id("mme_", t, 4);
#else
    return hex_id("mme_", t, 2);
#endif
}

void append_mme_rules(QVector<DissectColorRule>& out) {
#if defined(DISSECTOR_NW)
    // 南网类型为 16 位值域,实际取值集中在低字节(命名类型在
    // 0x0030-0x0070,实测抓包另有 0x0081 等未命名取值):低字节
    // 全段(0x0000-0x00FF)逐一列项,命名回退与内置一致;高字节
    // 取值协议未定义,命中时宿主回退内置逻辑
    for (quint32 v = 0; v <= 0xFF; ++v) {
        const quint16 t = quint16(v);
        const char* nm = mme_name(t);
        const QString name = nm ? QString::fromLatin1(nm)
            : QStringLiteral("MMe 0x%1").arg(t, 4, 16, QChar('0'));
        out.append(rule(mme_id(t).toLatin1().constData(), name, name,
                        mme_color(t)));
    }
#else
    // 国网类型为 quint8:全值域逐一列项,命名回退与内置一致
    for (quint32 v = 0; v <= 0xFF; ++v) {
        const quint16 t = quint16(v);
        const char* nm = mme_name(t);
        const QString name = nm ? QString::fromLatin1(nm)
            : QStringLiteral("Unknown(0x%1)").arg(t, 2, 16, QChar('0'));
        out.append(rule(mme_id(t).toLatin1().constData(), name, name,
                        mme_color(t)));
    }
#endif
}

void append_bid_rules(QVector<DissectColorRule>& out) {
    out.append(rule("bid_confirm", QStringLiteral("确认 (ACK)"),
                    QStringLiteral("Confirm (ACK)"), QColor(46, 214, 106)));
    out.append(rule("bid_deny", QStringLiteral("否认 (NACK)"),
                    QStringLiteral("Deny (NACK)"), QColor(224, 64, 64)));
    for (quint32 v = 0; v <= 0xFE; ++v) {   // 0xFF=非应用层,不着色
        const QString name =
            QStringLiteral("BID 0x%1").arg(v, 2, 16, QChar('0'));
        out.append(rule(hex_id("bid_", v, 2).toLatin1().constData(),
                        name, name, bid_color(quint8(v))));
    }
}

}  // namespace

QVector<DissectColorRule> reference_color_rules() {
    QVector<DissectColorRule> out;
    // 帧类型基色(转录 PacketListModel::data_color)
    out.append(rule("rejected", QStringLiteral("未通过"),
                    QStringLiteral("Rejected"), QColor(128, 128, 128)));
    out.append(rule("frame_beacon", QStringLiteral("信标帧"),
                    QStringLiteral("Beacon"), QColor(86, 156, 214)));
    out.append(rule("frame_sof", QStringLiteral("SOF 帧"),
                    QStringLiteral("SOF"), QColor(106, 153, 78)));
    out.append(rule("frame_ack", QStringLiteral("ACK 帧"),
                    QStringLiteral("ACK"), QColor(196, 181, 79)));
    out.append(rule("frame_ack_fail", QStringLiteral("ACK 校验失败"),
                    QStringLiteral("ACK Check Failed"),
                    QColor(224, 64, 64)));
    out.append(rule("frame_coord", QStringLiteral("协调帧"),
                    QStringLiteral("Coordination"), QColor(140, 110, 190)));
    out.append(rule("frame_other", QStringLiteral("其他帧"),
                    QStringLiteral("Other"), QColor(170, 170, 170)));
    append_mme_rules(out);
    append_bid_rules(out);
    return out;
}

QString reference_color_id(const ParseResult& r) {
    // 与宿主 ForegroundRole 同序:MMe > BID > 帧类型(含未通过)
    if (r.msdu.present && r.msdu.mme_type != 0xFFFF)
        return mme_id(r.msdu.mme_type);
    if (r.msdu.present && r.msdu.business_id != 0xFF) {
        if (r.msdu.app_packet_type == 0) {   // 确认/否认帧
            if (r.msdu.business_id == 0) return QStringLiteral("bid_confirm");
            if (r.msdu.business_id == 1) return QStringLiteral("bid_deny");
        }
        return hex_id("bid_", r.msdu.business_id, 2);
    }
    if (!r.accept) return QStringLiteral("rejected");
    switch (r.mpdu.frame_type) {
        case 0: return QStringLiteral("frame_beacon");
        case 1: return QStringLiteral("frame_sof");
        case 2: return r.mpdu.ack_rx_res == 1
                        ? QStringLiteral("frame_ack_fail")
                        : QStringLiteral("frame_ack");
        case 3: return QStringLiteral("frame_coord");
        default: return QStringLiteral("frame_other");
    }
}

}  // namespace dissect_gen
