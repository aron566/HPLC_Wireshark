/// @file beaconparser.cpp
/// @brief BEACON 帧载荷区解析器实现(独立帧解析模块,beaconparser.pri)
/// @details 输入 MPDU(payload_for_log,自 FrameType 起含 16B FCH),按 51242/
///          51243 解析载荷区:固定头 + 信标管理信息(条目)/PB Padding/
///          载荷 CRC32/PB CRC24。字段树复用 MsduFieldNode(见 bplcframe.h),
///          公共字段工具见 fieldspec.h。
#include "beaconparser.h"
#include "fieldspec.h"
#include "i18n.h"
#include <QtEndian>
#include <cstdint>
#include <functional>

// ================= BEACON 载荷区(MPDU_BEACON_LOAD) =================
// gbBeaconMPDU = payload[16 : 16+PBSize-3](PBSize 由 FCH 内 TMI 决定,回放 TMI=4→136B)。
// 载荷固定头字段相对 gb 起点。解析输出与 Python log 对照:
//   BeaconType/NetWorkingFlag/SimpleBeaconFlag/AssociationFlag/BeaconCEFlag
//   NetSN/CCO_MAC/BeaconPeriodCount/NetRfChannel/NetRfOption + ItemNum + 各条目
// PB 物理块检查序列 CRC24:公共 crc24_lsb(fieldspec.h,poly=0xC60001、
// init=0、LSB 先行);校验目标 = 帧载荷 + 帧载荷校验序列(块内前 len-3B)

// 信标类型名称(51242 表39:0 发现信标/1 代理信标/2 中央信标/其它保留)
static QString beacon_type_name(quint8 t) {
    switch (t) {
        case 0:  return trl::L("发现信标");
        case 1:  return trl::L("代理信标");
        case 2:  return trl::L("中央信标");
        default: return trl::L("保留");
    }
}

// 条目头英文全称(51242 表46)
static QString beacon_item_head_name(quint8 h) {
    switch (h) {
        case 0x00: return QStringLiteral("STA Capability Item");
        case 0x01: return QStringLiteral("Route Parameter Item");
        case 0x02: return QStringLiteral("Band Change Item");
        case 0x03: return QStringLiteral("RF Route Parameter Item");
        case 0x04: return QStringLiteral("RF Channel Change Item");
        case 0x05: return QStringLiteral("Lite STA Info & Slot Item");
        case 0xC0: return QStringLiteral("Time Slot Allocation Item");
        default:   return QStringLiteral("Reserved");  // 0x06..0xBF / 0xC1..0xFF
    }
}

// 条目头中文含义(表46 定义说明)
static QString beacon_item_head_desc(quint8 h) {
    switch (h) {
        case 0x00: return trl::L("站点能力条目(标准信标必选)");
        case 0x01: return trl::L("路由参数条目(标准信标必选)");
        case 0x02: return trl::L("频段变更条目(可选)");
        case 0x03: return trl::L("无线路由参数条目(标准信标必选)");
        case 0x04: return trl::L("无线信道变更条目(可选)");
        case 0x05: return trl::L("精简信标站点信息及时隙条目(精简信标必选)");
        case 0xC0: return trl::L("时隙分配条目(TSA,标准信标必选)");
        default:   return trl::L("保留");
    }
}

// 相线名称(表47/52/53:0 全相线 1 A相 2 B相 3 C相)
static QString line_name(quint8 l) {
    switch (l) {
        case 0:  return trl::L("全相线");
        case 1:  return trl::L("A相线");
        case 2:  return trl::L("B相线");
        case 3:  return trl::L("C相线");
        default: return trl::L("保留");
    }
}

// 无线信标标志(51242 表51):高速载波信标与无线信标的发送组合方式
static QString rf_wireless_desc(quint8 rf) {
    switch (rf) {
        case 0:  return trl::L("仅发送高速载波信标");
        case 1:  return trl::L("仅发送无线标准信标");
        case 2:  return trl::L("载波信标后发无线标准信标");
        case 3:  return trl::L("载波信标后发无线精简信标");
        case 4:  return trl::L("载波信标+CSMA时隙发无线精简信标");
        default: return trl::L("保留");
    }
}

// 槽信息叶子节点(字段级展示辅助)
static void slot_leaf(QVector<MsduFieldNode>& out, const QString& name,
                      const QString& value, int rel_abs, int rel_len) {
    MsduFieldNode n;
    n.name      = name;
    n.value     = value;
    n.rel_start = rel_abs;
    n.rel_len   = rel_len;
    out.append(n);
}

// beacon_pb_size 公共实现见 bplcframe.h(供 bplcparser/protocoltree 共用)

// 载荷固定头(相对 gb)。依据 51242 表38 标准信标帧载荷字段:
//   字节0:类型3b+组网1b+精简1b+保留1b+开始关联1b+信标使用1b
//   字节1 组网序列号;2-7 CCO MAC;8-11 信标周期计数;12 本网络无线信道编号
//   13-19 保留(56b);20+ 信标管理信息
static const FieldSpec kBeaconLoadSpec[] = {
    {"BeaconType",        0, 0, 3,  Fmt::DEC},
    {"NetWorkingFlag",    0, 3, 1,  Fmt::DEC},
    {"SimpleBeaconFlag",  0, 4, 1,  Fmt::DEC},
    {"RSV0",              0, 5, 1,  Fmt::DEC},
    {"AssociationFlag",   0, 6, 1,  Fmt::DEC},
    {"BeaconCEFlag",      0, 7, 1,  Fmt::DEC},
    {"NetSN",             1, 0, 8,  Fmt::DEC},
    {"CCO_MACAddr",       2, 0, 48, Fmt::MAC},
    {"BeaconPeriodCount", 8, 0, 32, Fmt::DEC},
    {"NetRfChannel",     12, 0, 8,  Fmt::DEC},
    {"RSV1",             13, 0, 56, Fmt::DEC},
};
static const int kBeaconLoadSpecN = int(sizeof(kBeaconLoadSpec) / sizeof(kBeaconLoadSpec[0]));

// 精简信标帧固定头(51243 表56):0..11 与标准一致,无信道编号/13-19 保留,
// 字节12 起即信标管理信息
static const FieldSpec kBeaconLiteHeadSpec[] = {
    {"BeaconType",        0, 0, 3,  Fmt::DEC},
    {"NetWorkingFlag",    0, 3, 1,  Fmt::DEC},
    {"SimpleBeaconFlag",  0, 4, 1,  Fmt::DEC},
    {"RSV0",              0, 5, 1,  Fmt::DEC},
    {"AssociationFlag",   0, 6, 1,  Fmt::DEC},
    {"BeaconCEFlag",      0, 7, 1,  Fmt::DEC},
    {"NetSN",             1, 0, 8,  Fmt::DEC},
    {"CCO_MACAddr",       2, 0, 48, Fmt::MAC},
    {"BeaconPeriodCount", 8, 0, 32, Fmt::DEC},
};
static const int kBeaconLiteHeadSpecN =
    int(sizeof(kBeaconLiteHeadSpec) / sizeof(kBeaconLiteHeadSpec[0]));

// STA Cap 条目(相对条目数据)。51242 表47 站点能力条目:TEI/代理TEI/
// 路径最低通信成功率/发送信标站点MAC/角色/层级数/代理站点信道质量/相线/
// 链路上RF跳数/保留(12,6,2)
static const FieldSpec kStaCapSpec[] = {
    {"TEI",                  0, 0, 12, Fmt::DEC},
    {"PCOTEI",               1, 4, 12, Fmt::DEC},
    {"LinkMinCommSuccessRate", 3, 0, 8,  Fmt::DEC},
    {"SourceMAC",            4, 0, 48, Fmt::MAC},
    {"Role",                10, 0, 4,  Fmt::DEC},
    {"NetLevel",            10, 4, 4,  Fmt::DEC},
    {"PCOChannelQuality",   11, 0, 8,  Fmt::DEC},
    {"STALine",             12, 0, 2,  Fmt::DEC},
    {"LinkRFHopNum",        12, 2, 4,  Fmt::DEC},
    {"RSV2",                12, 6, 2,  Fmt::DEC},
};
static const int kStaCapSpecN = int(sizeof(kStaCapSpec) / sizeof(kStaCapSpec[0]));

// Route Param 条目(相对条目数据)。51242 表48 路由参数通知条目(全部 16b,单位 s)
static const FieldSpec kRouteParamSpec[] = {
    {"RoutePeriod",            0, 0, 16, Fmt::DEC},
    {"NextRouteEstimationTime",2, 0, 16, Fmt::DEC},
    {"PCODiscoveryListPeriod", 4, 0, 16, Fmt::DEC},
    {"STADiscoveryListPeriod", 6, 0, 16, Fmt::DEC},
};
static const int kRouteParamSpecN = int(sizeof(kRouteParamSpec) / sizeof(kRouteParamSpec[0]));

// 频段通知条目 0x02(51242 表49):目标频段(0,8)+频段切换剩余时间(1,32,ms)
static const FieldSpec kBandChangeSpec[] = {
    {"TargetBand",        0, 0, 8,  Fmt::DEC},
    {"SwitchRemainTime",  1, 0, 32, Fmt::DEC},
};
static const int kBandChangeSpecN = int(sizeof(kBandChangeSpec) / sizeof(kBandChangeSpec[0]));

// 无线路由参数条目 0x03(51242 表54):无线发现列表周期(0,8,s)+
// 无线接收率老化周期个数(1,8,单位=无线发现列表周期)
static const FieldSpec kRfRouteSpec[] = {
    {"RfDiscoveryListPeriod", 0, 0, 8, Fmt::DEC},
    {"RfRateAgePeriodNum",    1, 0, 8, Fmt::DEC},
};
static const int kRfRouteSpecN = int(sizeof(kRfRouteSpec) / sizeof(kRfRouteSpec[0]));

// 无线信道变更条目 0x04(51242 表55):目标信道(0,8)+信道切换剩余时间(1,32,ms)
static const FieldSpec kRfChChangeSpec[] = {
    {"TargetChannel",      0, 0, 8,  Fmt::DEC},
    {"ChSwitchRemainTime", 1, 0, 32, Fmt::DEC},
};
static const int kRfChChangeSpecN = int(sizeof(kRfChChangeSpec) / sizeof(kRfChChangeSpec[0]));

// 精简信标站点信息及时隙条目 0x05(51243 表57,内容 17B)
static const FieldSpec kLiteStaSpec[] = {
    {"TEI",             0, 0, 12, Fmt::DEC},
    {"PCOTEI",          1, 4, 12, Fmt::DEC},
    {"Role",            3, 0, 4,  Fmt::DEC},
    {"NetLevel",        3, 4, 4,  Fmt::DEC},
    {"SourceMAC",       4, 0, 48, Fmt::MAC},
    {"LinkRFHopNum",   10, 0, 4,  Fmt::DEC},
    {"RSV",            10, 4, 4,  Fmt::DEC},
    {"CSMASlotStart",  11, 0, 32, Fmt::DEC},
    {"CSMASlotLen",    15, 0, 16, Fmt::DEC},
};
static const int kLiteStaSpecN = int(sizeof(kLiteStaSpec) / sizeof(kLiteStaSpec[0]));

// TSA/时隙分配条目头(相对条目数据)。51242 表50:含跨字节保留
// (1,6,10)与(19,2,6);CSMASlotSplitLen 单位 10ms
static const FieldSpec kTsaHeadSpec[] = {
    {"NonCCOBeaconNum",        0, 0, 8,  Fmt::DEC},
    {"CCOBeaconNum",           1, 0, 4,  Fmt::DEC},
    {"CSMALineSupportNum",     1, 4, 2,  Fmt::DEC},
    {"RSV1",                   1, 6, 10, Fmt::DEC},
    {"PCOBeaconNum",           3, 0, 8,  Fmt::DEC},
    {"BeaconSlotLen",          4, 0, 8,  Fmt::DEC},
    {"CSMASlotSplitLen",       5, 0, 8,  Fmt::DEC},
    {"BindingCSMALineNum",     6, 0, 8,  Fmt::DEC},
    {"BindingCSMALinkID",      7, 0, 8,  Fmt::DEC},
    {"TDMASlotLen",            8, 0, 8,  Fmt::DEC},
    {"TDMALinkID",             9, 0, 8,  Fmt::DEC},
    {"BeaconPeriodStartNTB",  10, 0, 32, Fmt::HEX8},
    {"BeaconPeriod",          14, 0, 32, Fmt::DEC},
    {"RfBeaconSlotLen",       18, 0, 10, Fmt::DEC},
    {"RSV2",                  19, 2, 6,  Fmt::DEC},
};
static const int kTsaHeadSpecN = int(sizeof(kTsaHeadSpec) / sizeof(kTsaHeadSpec[0]));

MsduInfo BeaconParser::parse_beacon(const QByteArray& payload) {
    MsduInfo out;
    out.present = false;
    if (payload.size() < 40) return out;  // 16 FCH + 载荷头
    const quint8* p = reinterpret_cast<const quint8*>(payload.constData());
    quint8 tmi = (quint8)get_bits(p, 9, 4, 4);   // BEACON TMI 在 FCH b9 高 4bit
    int pbsize = beacon_pb_size(tmi);
    if (pbsize <= 0 || payload.size() < 16 + pbsize) return out;

    // gbBeaconMPDU = payload[16 : 16+PBSize-3](含载荷头 + 管理区 + 4B CRC32)
    QByteArray gb = payload.mid(16, pbsize - 3);
    if (gb.size() < 24) return out;
    out.present = true;

    // CRC32 校验(存储于 gb 尾 4B,LE;cal_crc32 计算前 len-4 字节)
    bool crc_ok = false;
    if (gb.size() >= 4) {
        quint32 stored = (quint8)gb[gb.size() - 4]
                       | ((quint32)(quint8)gb[gb.size() - 3] << 8)
                       | ((quint32)(quint8)gb[gb.size() - 2] << 16)
                       | ((quint32)(quint8)gb[gb.size() - 1] << 24);
        quint32 calc = crc32_le(
            reinterpret_cast<const quint8*>(gb.constData()), gb.size());
        crc_ok = (stored == calc);
    }
    out.summary = QStringLiteral("BEACON Load CRC32:%1")
                      .arg(crc_ok ? QStringLiteral("OK") : QStringLiteral("FAIL"));

    auto& root = group(out.tree, QStringLiteral("Beacon Load [%1B]").arg(gb.size()));

    // 精简信标标志(字节0 bit4):标准信标管理区在字节20 起;精简信标
    // (51243 表56)无字节12 信道编号与 13-19 保留,管理区在字节12 起
    const bool lite = ((quint8)gb[0] & 0x10) != 0;

    // 固定头:标准 0..19 / 精简 0..11
    if (lite)
        add_fields(root.children, gb, 0, kBeaconLiteHeadSpec, kBeaconLiteHeadSpecN);
    else
        add_fields(root.children, gb, 0, kBeaconLoadSpec, kBeaconLoadSpecN);

    quint8 beacon_type = (quint8)get_bits(gb, 0, 0, 3);
    for (auto& ch : root.children) {
        if (ch.name.startsWith(QStringLiteral("BeaconType"))) {
            ch.value = QStringLiteral("%1 - %2").arg(
                ch.value, beacon_type_name(beacon_type));
        }
    }
    // 标志位含义注释(表40/41/42/43)
    for (auto& ch : root.children) {
        if (ch.name.startsWith(QStringLiteral("NetWorkingFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 3, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("组网完成") : trl::L("组网未完成"));
        } else if (ch.name.startsWith(QStringLiteral("SimpleBeaconFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 4, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("精简信标帧") : trl::L("标准信标帧"));
        } else if (ch.name.startsWith(QStringLiteral("AssociationFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 6, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("允许站点发起关联请求")
                  : trl::L("不允许站点发起关联请求"));
        } else if (ch.name.startsWith(QStringLiteral("BeaconCEFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 7, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("允许使用信标进行信道评估")
                  : trl::L("不允许使用信标进行信道评估"));
        }
    }

    // 管理区 = gb[mgmt_off : -4]:首字节 ItemNum,随后 head(1B)+len(1B)+内容。
    // 依 51242 表44 信标管理信息格式,集中展示为一个分组
    const int item_num_off = lite ? 12 : 20;
    if (gb.size() <= item_num_off + 1) return out;
    quint8 item_num = (quint8)gb[item_num_off];
    auto& mgmt = group(root.children, QStringLiteral("Beacon Mgmt Info"));

    MsduFieldNode in;
    in.name  = QStringLiteral("ItemNum [8b]");
    in.value = QString::number(item_num);
    in.rel_start = item_num_off;
    in.rel_len   = 1;
    mgmt.children.append(in);

    int pos = item_num_off + 1;
    for (int n = 0; n < item_num && pos < gb.size() - 4; ++n) {
        const int head_abs = pos;         // 条目头在 gb 中的绝对偏移(供高亮)
        quint8 head = (quint8)gb[pos++];
        if (pos >= gb.size() - 4) break;
        int item_len, len_bytes = 1;
        quint32 len_raw;
        if (head == 0xC0) {  // TSA:长度 2B LE,len-3 = 内容长
            if (pos + 1 >= gb.size() - 4) break;
            len_raw = (quint8)gb[pos] | ((quint8)gb[pos + 1] << 8);
            pos += 2;
            len_bytes = 2;
            item_len  = int(len_raw) - 3;
        } else {
            len_raw = (quint8)gb[pos++];
            item_len  = int(len_raw) - 2;
        }
        if (item_len <= 0 || pos + item_len > gb.size() - 4) break;
        QByteArray it = gb.mid(pos, item_len);
        int abs0 = pos;  // 条目数据在 gb 中的绝对偏移(供高亮)
        pos += item_len;

        // 条目头/条目长度显式成行(表44/表46):长度字段大小 0xC0 为 2B,其余 1B
        MsduFieldNode hd;
        hd.name      = QStringLiteral("ItemHead [8b]");
        hd.value     = QStringLiteral("0x%1 - %2 - %3")
                           .arg(head, 2, 16, QChar('0'))
                           .arg(beacon_item_head_name(head))
                           .arg(beacon_item_head_desc(head));
        hd.rel_start = head_abs;
        hd.rel_len   = 1;
        MsduFieldNode ln;
        ln.name      = (len_bytes == 2) ? QStringLiteral("ItemLen [16b]")
                                        : QStringLiteral("ItemLen [8b]");
        ln.value     = trl::L("%1 (内容 %2B)").arg(len_raw).arg(item_len);
        ln.rel_start = head_abs + 1;
        ln.rel_len   = len_bytes;

        auto& grp = group(mgmt.children,
                          trl::L("Item[%1] %2 (内容 %3B)")
                              .arg(n).arg(beacon_item_head_name(head)).arg(item_len));
        grp.children.append(hd);
        grp.children.append(ln);
        switch (head) {
            case 0x00: {  // STA Cap(13B)
                add_fields(grp.children, it, 0, kStaCapSpec, kStaCapSpecN, abs0);
                annotate_unit(grp.children, "LinkMinCommSuccessRate", QStringLiteral("%"));
                annotate_unit(grp.children, "PCOChannelQuality", QStringLiteral("dB"));
                for (auto& ch : grp.children) {
                    if (ch.name.startsWith(QStringLiteral("Role"))) {
                        quint8 r = (quint8)get_bits(it, 10, 0, 4);
                        static const char* nm[] = {"Unknown","STA","PCO",nullptr,"CCO"};
                        ch.value = QStringLiteral("%1 - %2").arg(r)
                            .arg((r <= 4 && nm[r]) ? QLatin1String(nm[r])
                                                   : QStringLiteral("?"));
                    }
                    if (ch.name.startsWith(QStringLiteral("STALine"))) {
                        quint8 l = (quint8)get_bits(it, 12, 0, 2);
                        ch.value = QStringLiteral("%1 - %2").arg(l)
                            .arg(line_name(l));
                    }
                }
                break;
            }
            case 0x01: {  // Route Param(8B)
                add_fields(grp.children, it, 0, kRouteParamSpec, kRouteParamSpecN, abs0);
                // 周期/时间单位(与 Python log "RoutePeriod: 80s" 一致)
                annotate_unit(grp.children, "RoutePeriod", QStringLiteral("s"));
                annotate_unit(grp.children, "NextRouteEstimationTime", QStringLiteral("s"));
                annotate_unit(grp.children, "PCODiscoveryListPeriod", QStringLiteral("s"));
                annotate_unit(grp.children, "STADiscoveryListPeriod", QStringLiteral("s"));
                break;
            }
            case 0x02: {  // 频段通知条目(51242 表49)
                add_fields(grp.children, it, 0, kBandChangeSpec, kBandChangeSpecN, abs0);
                annotate_unit(grp.children, "SwitchRemainTime", QStringLiteral("ms"));
                // 目标频段:0x00=频段0 / 0x01=频段1 / 其它保留(见物理层规范)
                for (auto& ch : grp.children) {
                    if (ch.name.startsWith(QStringLiteral("TargetBand"))) {
                        const quint8 b = (quint8)get_bits(it, 0, 0, 8);
                        ch.value = QStringLiteral("%1 - %2").arg(b).arg(
                            (b <= 1) ? QStringLiteral("Band %1").arg(b)
                                     : trl::L("保留值"));
                    }
                }
                break;
            }
            case 0x03: {  // 无线路由参数条目(51242 表54,必选)
                add_fields(grp.children, it, 0, kRfRouteSpec, kRfRouteSpecN, abs0);
                annotate_unit(grp.children, "RfDiscoveryListPeriod", QStringLiteral("s"));
                // 老化周期个数的单位 = 无线发现列表周期
                for (auto& ch : grp.children)
                    if (ch.name.startsWith(QStringLiteral("RfRateAgePeriodNum")))
                        ch.value += QLatin1Char(' ') + trl::L("(x 发现列表周期)");
                break;
            }
            case 0x04: {  // 无线信道变更条目(51242 表55)
                add_fields(grp.children, it, 0, kRfChChangeSpec, kRfChChangeSpecN, abs0);
                annotate_unit(grp.children, "ChSwitchRemainTime", QStringLiteral("ms"));
                break;
            }
            case 0x05: {  // 精简信标站点信息及时隙条目(51243 表57)
                add_fields(grp.children, it, 0, kLiteStaSpec, kLiteStaSpecN, abs0);
                for (auto& ch : grp.children) {
                    if (ch.name.startsWith(QStringLiteral("Role"))) {
                        quint8 r = (quint8)get_bits(it, 3, 0, 4);
                        static const char* nm[] = {"Unknown","STA","PCO",nullptr,"CCO"};
                        ch.value = QStringLiteral("%1 - %2").arg(r)
                            .arg((r <= 4 && nm[r]) ? QLatin1String(nm[r])
                                                   : QStringLiteral("?"));
                    }
                }
                break;
            }
            case 0xC0: {  // TSA/时隙分配条目:头 20B + 槽信息
                add_fields(grp.children, it, 0, kTsaHeadSpec, kTsaHeadSpecN, abs0);
                // TSA 时长字段单位 ms(与 Python log "BeaconSlotLen: 30ms" 一致)
                annotate_unit(grp.children, "BeaconSlotLen", QStringLiteral("ms"));
                annotate_unit(grp.children, "CSMASlotSplitLen", QStringLiteral("10ms"));
                annotate_unit(grp.children, "TDMASlotLen", QStringLiteral("ms"));
                annotate_unit(grp.children, "BeaconPeriod [", QStringLiteral("ms"));
                annotate_unit(grp.children, "RfBeaconSlotLen", QStringLiteral("ms"));
                int noncco = (int)get_bits(it, 0, 0, 8);
                int csma   = (int)get_bits(it, 1, 4, 2);
                int bind   = (int)get_bits(it, 6, 0, 8);
                int o = 20;
                // 三段槽信息独立分组,槽内字段按 51242 表51/52/53 逐行展开
                if (noncco > 0) {  // 非中央信标信息(2B/条):代理/发现站点时隙
                    auto& ng = group(grp.children,
                        QStringLiteral("NonCCOBeaconInfo [%1]").arg(noncco));
                    for (int i = 0; i < noncco && o + 2 <= it.size(); ++i) {
                        auto& sg = group(ng.children,
                            QStringLiteral("NonCCO[%1] (2B)").arg(i), QString());
                        quint16 tei = (quint16)get_bits(it, o, 0, 12);
                        quint8 bt  = (quint8)get_bits(it, o + 1, 4, 1);
                        quint8 rf  = (quint8)get_bits(it, o + 1, 5, 3);
                        slot_leaf(sg.children, QStringLiteral("TEI [12b]"),
                                  QString::number(tei), abs0 + o, 2);
                        slot_leaf(sg.children, QStringLiteral("Beacon Type [1b]"),
                                  QStringLiteral("%1 - %2").arg(bt).arg(
                                      bt == 0 ? trl::L("发现信标")
                                              : (bt == 1 ? trl::L("代理信标")
                                                         : trl::L("保留"))),
                                  abs0 + o + 1, 1);
                        slot_leaf(sg.children, QStringLiteral("RF Beacon Flag [3b]"),
                                  QStringLiteral("%1 - %2").arg(rf).arg(rf_wireless_desc(rf)),
                                  abs0 + o + 1, 1);
                        o += 2;
                    }
                }
                if (csma > 0) {  // CSMA 时隙信息(4B/条)
                    auto& cg = group(grp.children,
                        QStringLiteral("CSMASlotInfo [%1]").arg(csma));
                    for (int i = 0; i < csma && o + 4 <= it.size(); ++i) {
                        auto& sg = group(cg.children,
                            QStringLiteral("CSMA[%1] (4B)").arg(i), QString());
                        quint32 len = (quint32)get_bits(it, o, 0, 24);
                        quint8  ln  = (quint8)get_bits(it, o + 3, 0, 2);
                        quint8  rsv = (quint8)get_bits(it, o + 3, 2, 6);
                        slot_leaf(sg.children, QStringLiteral("CSMA Slot Len [24b]"),
                                  QStringLiteral("%1 ms").arg(len), abs0 + o, 3);
                        slot_leaf(sg.children, QStringLiteral("CSMA Slot Line [2b]"),
                                  QStringLiteral("%1 - %2").arg(ln).arg(line_name(ln)),
                                  abs0 + o + 3, 1);
                        slot_leaf(sg.children, QStringLiteral("RSV [6b]"),
                                  QString::number(rsv), abs0 + o + 3, 1);
                        o += 4;
                    }
                }
                if (bind > 0) {  // 绑定 CSMA 时隙信息(4B/条)
                    auto& bg = group(grp.children,
                        QStringLiteral("BindingCSMASlotInfo [%1]").arg(bind));
                    for (int i = 0; i < bind && o + 4 <= it.size(); ++i) {
                        auto& sg = group(bg.children,
                            QStringLiteral("Binding[%1] (4B)").arg(i), QString());
                        quint32 len = (quint32)get_bits(it, o, 0, 24);
                        quint8  ln  = (quint8)get_bits(it, o + 3, 0, 2);
                        quint8  rsv = (quint8)get_bits(it, o + 3, 2, 6);
                        slot_leaf(sg.children, QStringLiteral("Binding Slot Len [24b]"),
                                  QStringLiteral("%1 ms").arg(len), abs0 + o, 3);
                        slot_leaf(sg.children, QStringLiteral("Binding Slot Line [2b]"),
                                  QStringLiteral("%1 - %2").arg(ln).arg(line_name(ln)),
                                  abs0 + o + 3, 1);
                        slot_leaf(sg.children, QStringLiteral("RSV [6b]"),
                                  QString::number(rsv), abs0 + o + 3, 1);
                        o += 4;
                    }
                }
                break;
            }
            default: {
                MsduFieldNode raw;
                raw.name  = QStringLiteral("Payload");
                raw.value = QString(it.toHex(' '));
                grp.children.append(raw);
                break;
            }
        }
    }
    // 管理条目之后的填充区(条目消费终点 pos 到 CRC32 前):正常为全 0x00,
    // 与 Python 原版 "tails in Beacon Mgr Info is not 0" 校验语义一致;
    // 非零时把前 8B 展示出来便于发现未识别条目/异常(部分帧该区含 0xC2 段)。
    if (pos < gb.size() - 4) {
        const int pad_len = gb.size() - 4 - pos;
        bool all_zero = true;
        for (int k = pos; k < gb.size() - 4 && all_zero; ++k)
            all_zero = (gb[k] == 0);
        MsduFieldNode pad;
        pad.name = QStringLiteral("PB Padding");
        pad.value = all_zero
            ? QStringLiteral("%1 B (0x00 fill)").arg(pad_len)
            : trl::L("%1 B (含非 0x00: %2 ...)")
                  .arg(pad_len)
                  .arg(QString(gb.mid(pos, qMin(pad_len, 8)).toHex(' ')));
        pad.rel_start = pos;
        pad.rel_len   = pad_len;
        root.children.append(pad);
    }

    // 载荷 CRC32 位于数据区末尾 4B,字段展示顺序与字节流一致(最后出现);
    // 高亮整段 4B,与 msduparser MSDU CRC32 行风格一致。
    MsduFieldNode crc;
    crc.name      = QStringLiteral("BeaconCRC32 [32b]");
    crc.value     = crc_ok ? QStringLiteral("OK") : QStringLiteral("FAIL");
    crc.rel_start = gb.size() - 4;
    crc.rel_len   = 4;
    root.children.append(crc);

    // PB 物理块检查序列(24-bit):紧随载荷 CRC32 之后的块尾 3B。
    // 校验目标 = 帧载荷 + 帧载荷校验序列 = 块内前 pbsize-3 B(poly 0xC60001)。
    // 位置相对 gb 起点(载荷区在帧偏移 16)为 pbsize-3,共 3B。
    if (payload.size() >= 16 + pbsize) {
        const quint8* blk = p + 16;   // p = payload 数据指针(函数入口已取)
        quint32 stored = (quint32)blk[pbsize - 3]
                       | ((quint32)blk[pbsize - 2] << 8)
                       | ((quint32)blk[pbsize - 1] << 16);
        const bool ok24 = (stored == crc24_lsb(blk, pbsize));
        MsduFieldNode pbc;
        pbc.name  = QStringLiteral("PB CRC24");
        pbc.value = QStringLiteral("0x%1 %2")
                        .arg(stored, 6, 16, QChar('0'))
                        .arg(ok24 ? "OK" : "FAIL");
        pbc.rel_start = pbsize - 3;   // 相对 gb/载荷区起点(=帧偏移 16)
        pbc.rel_len   = 3;
        root.children.append(pbc);
    }
    return out;
}


// ===== i18n:文件级 中→英 显示词典(仅显示翻译;未命中回退中文) =====
namespace {
struct I18nReg {
    I18nReg() {
        // 信标帧标志位/条目注释(51242/51243)
        trl::register_en("组网完成", "Network established");
        trl::register_en("组网未完成", "Network not established");
        trl::register_en("精简信标帧", "Lite beacon frame");
        trl::register_en("标准信标帧", "Standard beacon frame");
        trl::register_en("允许站点发起关联请求", "Association request allowed");
        trl::register_en("不允许站点发起关联请求", "Association request not allowed");
        trl::register_en("允许使用信标进行信道评估", "Channel estimation allowed");
        trl::register_en("不允许使用信标进行信道评估", "Channel estimation not allowed");
        trl::register_en("保留值", "Reserved value");
        trl::register_en("(x 发现列表周期)", " (x discovery-list period)");
        // 条目头含义(表46)
        trl::register_en("站点能力条目(标准信标必选)", "STA Capability Item (mandatory, standard beacon)");
        trl::register_en("路由参数条目(标准信标必选)", "Route Parameter Item (mandatory, standard beacon)");
        trl::register_en("频段变更条目(可选)", "Band Change Item (optional)");
        trl::register_en("无线路由参数条目(标准信标必选)", "RF Route Parameter Item (mandatory, standard beacon)");
        trl::register_en("无线信道变更条目(可选)", "RF Channel Change Item (optional)");
        trl::register_en("精简信标站点信息及时隙条目(精简信标必选)", "Lite STA Info & Slot Item (mandatory, lite beacon)");
        trl::register_en("时隙分配条目(TSA,标准信标必选)", "Time Slot Allocation Item (mandatory, standard beacon)");
        trl::register_en("保留", "Reserved");
        trl::register_en("发现信标", "Discovery Beacon");
        trl::register_en("代理信标", "Proxy Beacon");
        trl::register_en("中央信标", "Central Beacon");
        // 相线/无线信标标志(表51/52/53)
        trl::register_en("全相线", "All lines");
        trl::register_en("A相线", "Line A");
        trl::register_en("B相线", "Line B");
        trl::register_en("C相线", "Line C");
        trl::register_en("仅发送高速载波信标", "Carrier beacon only");
        trl::register_en("仅发送无线标准信标", "RF standard beacon only");
        trl::register_en("载波信标后发无线标准信标", "Carrier then RF standard beacon");
        trl::register_en("载波信标后发无线精简信标", "Carrier then RF lite beacon");
        trl::register_en("载波信标+CSMA时隙发无线精简信标", "Carrier + RF lite beacon in CSMA slot");
        // 带占位符文案(与 trl::L 原文 key 一致,调用方再 .arg)
        trl::register_en("%1 (内容 %2B)", "%1 (%2B content)");
        trl::register_en("Item[%1] %2 (内容 %3B)", "Item[%1] %2 (%3B content)");
        trl::register_en("%1 B (含非 0x00: %2 ...)", "%1 B (non-zero bytes: %2 ...)");
    }
};
const I18nReg g_i18n_reg_beacon;

}  // namespace
