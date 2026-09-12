/// @file msduparser.cpp
/// @brief MSDU 解析器实现
/// @details 字段位域坐标与 Python MSDU_Class.py 的 BitDefine 定义一一对应。
///          解析 = 公共头(MSDU_BASE/MSDU_BASE_S) + 类型分支(MMe / APP)。
#include "msduparser.h"
#include "fieldspec.h"
#include "i18n.h"
#include <QtEndian>
#include <cstdint>

// ---------- 工具:取位域(与 bplcparser 同算法,本地独立实现) ----------
/// @brief MMe 管理消息类型(表59,8bit;不处理时保留枚举供扩展)
enum MMeType : quint8 {
    MME_ASSOC_REQ                 = 0x00,
    MME_ASSOC_CNF                 = 0x01,
    MME_ASSOC_GATHER_IND          = 0x02,
    MME_CHANGE_PROXY_REQ          = 0x03,
    MME_CHANGE_PROXY_CNF          = 0x04,
    MME_CHANGE_PROXY_BITMAP_CNF   = 0x05,
    MME_LEAVE_IND                 = 0x06,
    MME_HEARTBEAT_CHECK           = 0x07,
    MME_DISCOVER_NODE_LIST        = 0x08,
    MME_SUCCESS_RATE_REPORT       = 0x09,
    MME_NETWORK_CONFLICT_REPORT   = 0x0A,
    MME_ZERO_CROSS_NTB_COLLECT_IND= 0x0B,
    MME_ZERO_CROSS_NTB_REPORT     = 0x0C,
    MME_DIAGNOSE                  = 0x4F,
    MME_ROUTE_REQUEST             = 0x50,
    MME_ROUTE_REPLY               = 0x51,
    MME_ROUTE_ERROR               = 0x52,
    MME_ROUTE_ACK                 = 0x53,
    MME_LINK_CONFIRM_REQUEST      = 0x54,
    MME_LINK_CONFIRM_RESPONSE     = 0x55,
    MME_RF_CHANNEL_CONFLICT_REPORT= 0x80,
};
// ================= 公共头字段表(Python MSDU_BASE) =================
static const FieldSpec kMsduBaseSpec[] = {
    {"Version",             0, 0, 4,  Fmt::DEC},
    {"SourceTEI",           0, 4, 12, Fmt::DEC},
    {"DestinationTEI",      2, 0, 12, Fmt::DEC},
    {"SendType",            3, 4, 4,  Fmt::DEC},
    {"MaxSendCountLimit",   4, 0, 5,  Fmt::DEC},
    {"RSV0",                4, 5, 3,  Fmt::DEC},
    {"MSDU Seq",            5, 0, 16, Fmt::DEC},
    {"MSDUType",            7, 0, 8,  Fmt::DEC},
    {"MSDULen",             8, 0, 11, Fmt::DEC},
    {"RestartCount",        9, 3, 4,  Fmt::DEC},
    {"MainProxyFlag",       9, 7, 1,  Fmt::DEC},
    {"RouteHopsTotalNum",  10, 0, 4,  Fmt::DEC},
    {"RouteHopsLeftNum",   10, 4, 4,  Fmt::DEC},
    {"BroadcastDirection", 11, 0, 2,  Fmt::DEC},
    {"PathRepairFlag",     11, 2, 1,  Fmt::DEC},
    {"MACAddrFlag",        11, 3, 1,  Fmt::DEC},
    {"RSV1",               11, 4, 12, Fmt::DEC},
    {"NetSN",              13, 0, 8,  Fmt::DEC},
    {"RSV2",               14, 0, 8,  Fmt::DEC},
    {"RSV3",               15, 0, 8,  Fmt::DEC},
};
static const int kMsduBaseSpecN = int(sizeof(kMsduBaseSpec) / sizeof(kMsduBaseSpec[0]));

// 扩展 MAC 地址字段(仅在 MACAddrFlag=1 时追加)
static const FieldSpec kMsduMacSpec[] = {
    {"SourceMACAddr",      16, 0, 48, Fmt::MAC},
    {"DestinationMACAddr", 22, 0, 48, Fmt::MAC},
};

// ================= MMe 公共头(MMe_BASE):MMType(0,0,8) RSV(2,0,8) =================
static QString mme_type_name(quint8 t) {
    switch (t) {
        case MME_ASSOC_REQ: return QStringLiteral("MMeAssocReq");
        case MME_ASSOC_CNF: return QStringLiteral("MMeAssocCnf");
        case MME_ASSOC_GATHER_IND: return QStringLiteral("MMeAssocGatherInd");
        case MME_CHANGE_PROXY_REQ: return QStringLiteral("MMeChangeProxyReq");
        case MME_CHANGE_PROXY_CNF: return QStringLiteral("MMeChangeProxyCnf");
        case MME_CHANGE_PROXY_BITMAP_CNF: return QStringLiteral("MMeChangeProxyBitMapCnf");
        case MME_LEAVE_IND: return QStringLiteral("MMeLeaveInd");
        case MME_HEARTBEAT_CHECK: return QStringLiteral("MMeHeartBeatCheck");
        case MME_DISCOVER_NODE_LIST: return QStringLiteral("MMeDiscoveryNodeList");
        case MME_SUCCESS_RATE_REPORT: return QStringLiteral("MMeSuccessRateReport");
        case MME_NETWORK_CONFLICT_REPORT: return QStringLiteral("MMeNetworkConflictReport");
        case MME_ZERO_CROSS_NTB_COLLECT_IND: return QStringLiteral("MMeZeroCrossNTBCollectInd");
        case MME_ZERO_CROSS_NTB_REPORT: return QStringLiteral("MMeZeroCrossNTBReport");
        case MME_DIAGNOSE: return QStringLiteral("MMeDiagnose");
        case MME_ROUTE_REQUEST: return QStringLiteral("MMeRouteRequest");
        case MME_ROUTE_REPLY: return QStringLiteral("MMeRouteReply");
        case MME_ROUTE_ERROR: return QStringLiteral("MMeRouteError");
        case MME_ROUTE_ACK: return QStringLiteral("MMeRouteAck");
        case MME_LINK_CONFIRM_REQUEST: return QStringLiteral("MMeLinkConfirmRequest");
        case MME_LINK_CONFIRM_RESPONSE: return QStringLiteral("MMeLinkConfirmResponse");
        case MME_RF_CHANNEL_CONFLICT_REPORT: return QStringLiteral("MMeRFChannelConflictReport");
        default:   return QStringLiteral("Unknown(0x%1)").arg(t, 2, 16, QChar('0'));
    }
}

static QString assoc_result_str(quint8 v) {
    static const char* names[] = {
        "Request Succeeded", "STA not in Whitelist", "STA in blacklist",
        "STA number exceeded limit", "Whitelist not configured",
        "PCO number exceeded limit", "Child STA number exceeded limit",
        "Reserved", "Duplicate MAC", "Topology levels exceeded",
        "Re-Association Succeeded", "Use subordinate as proxy",
        "Loops in topology", "Unknown CCO error", "RF PCO exceeded limit"};
    if (v <= 0x0e) return QStringLiteral("%1 - %2").arg(v).arg(QLatin1String(names[v]));
    return QString::number(v);
}

/// 枚举字典翻译:name 命中则替换 value 为 "值 - 含义"
static void translate_enum(QVector<MsduFieldNode>& nodes, const char* field,
                           const char* const* dict, int dict_size) {
    for (auto& n : nodes) {
        if (n.name.startsWith(QLatin1String(field))) {
            bool ok = false;
            int v = n.value.toInt(&ok);
            if (ok && v >= 0 && v < dict_size && dict[v])
                n.value = QStringLiteral("%1 - %2").arg(v).arg(QLatin1String(dict[v]));
        }
    }
}

/// 稀疏数值字典翻译(值→含义):value 改为 "值 - 含义"
static void translate_enum_sparse(QVector<MsduFieldNode>& nodes, const char* field,
                                  const QVector<QPair<int, QString>>& dict) {
    for (auto& n : nodes) {
        if (!n.name.startsWith(QLatin1String(field))) continue;
        bool ok = false;
        int v = n.value.toInt(&ok);
        if (!ok) continue;
        for (const auto& kv : dict) {
            if (kv.first == v) {
                n.value = QStringLiteral("%1 - %2").arg(v).arg(kv.second);
                break;
            }
        }
    }
}

/// APP PacketID → 报文类型注释
/// 依据协议"表 2 报文 ID"(含义/报文端口号),与 Python MPDU_Process 分发补充。
static QString packet_id_name(quint16 id) {
    switch (id) {
        case 0x0001: return trl::L("终端主动抄表");
        case 0x0002: return trl::L("路由主动抄表");
        case 0x0003:
        case 0x00B3: return trl::L("终端主动并发抄表");
        case 0x0004: return trl::L("校时");
        case 0x0006: return trl::L("通信测试");
        case 0x0008: return trl::L("事件上报");
        case 0x0011: return trl::L("查询从节点主动注册");
        case 0x0012: return trl::L("启动从节点主动注册");
        case 0x0013: return trl::L("停止从节点主动注册");
        case 0x0020: return trl::L("确认/否认");
        case 0x0030: return trl::L("开始升级");
        case 0x0031: return trl::L("停止升级");
        case 0x0032: return trl::L("传输文件数据");
        case 0x0033: return trl::L("传输文件数据(单播转本地广播)");
        case 0x0034: return trl::L("查询站点升级状态");
        case 0x0035: return trl::L("执行升级");
        case 0x0036: return trl::L("查询站点信息");
        case 0x0040: return trl::L("抄控器 CCO");
        case 0x0041: return trl::L("抄控器数据透传串口转发");
        case 0x00A0: return trl::L("鉴权安全");
        case 0x00A1: return trl::L("台区户变关系识别");
        case 0x00A2: return trl::L("查询ID信息");
        case 0x00A3: return trl::L("精准校时");
        case 0x00A4: return trl::L("配电信息上报");
        // 以下为 Python 已实现但"表 2"未单列的应用报文(扩展补充)
        case 0x00B0: return trl::L("存储采集扩展配置");
        case 0x00B1: return trl::L("存储数据广播时规");
        case 0x00B2: return trl::L("存储数据同步配置");
        case 0x00C1: return trl::L("认证");
        case 0x00CC: return trl::L("存储 HRF 中继心跳");
        default:     return trl::L("(未实现)");
    }
}

/// APP PortNum → 端口注释(表 2 报文端口号列)
static QString app_port_name(quint8 port) {
    switch (port) {
        case 0x11: return trl::L("管理/抄表端口");
        case 0x12: return trl::L("升级端口");
        case 0x1A: return trl::L("安全端口");
        default:   return QString();
    }
}

/// 2 值标志字段:value 改为 "0 (0: 含义A, 1: 含义B)"
static void annotate_flag(QVector<MsduFieldNode>& nodes, const char* field,
                          const char* desc0, const char* desc1) {
    for (auto& n : nodes) {
        if (n.name.startsWith(QLatin1String(field))) {
            bool ok = false;
            int v = n.value.toInt(&ok);
            if (ok)
                n.value = QStringLiteral("%1 (0: %2, 1: %3)")
                              .arg(v).arg(QLatin1String(desc0))
                                     .arg(QLatin1String(desc1));
        }
    }
}

/// MSDU_BASE 顶层字段解释(与 Python MSDU_BASE display 一致)
static void annotate_msdu_base(QVector<MsduFieldNode>& nodes) {
    // 稀疏字典避免大数组错位:0=网管 48=应用 49=IP
    translate_enum_sparse(nodes, "SendType", {
        {0, QStringLiteral("Unicast")},
        {1, QStringLiteral("Global Broadcast")},
        {2, QStringLiteral("Local Broadcast")},
        {3, QStringLiteral("Proxy Broadcast")},
    });
    translate_enum_sparse(nodes, "MSDUType", {
        {0, QStringLiteral("Net Management Message")},
        {48, QStringLiteral("Application Level Message")},
        {49, QStringLiteral("IP Message")},
    });
    translate_enum_sparse(nodes, "BroadcastDirection", {
        {0, QStringLiteral("Bi-direction Broadcast")},
        {1, QStringLiteral("Downstream Broadcast")},
        {2, QStringLiteral("Upstream Broadcast")},
        {3, QStringLiteral("UNKNOWN")},
    });
    annotate_flag(nodes, "MainProxyFlag",
                  "Disable Main Proxy Path Mode", "Enable Proxy Main Path Mode");
    annotate_flag(nodes, "PathRepairFlag",
                  "Not Repaired Path", "Repaired Path");
    annotate_flag(nodes, "MACAddrFlag", "No MAC Addr", "Have MAC Addr");
}

static const char* kDeviceType[] = {
    nullptr, "Central Controller", "Concentrator Comm Module",
    "Meter Comm Module", "Repeater", "TypeII Data Collector",
    "TypeI Data Collector", "3 Phase Meter Comm Module"};
static const char* kMacAddrType[] = {
    "Use Meter MAC Address", "Use Comm Module MAC Address"};
static const char* kModuleType[] = {
    "HPLC Module", "HPLC+HRF Module", "HRF Module"};
static const char* kLinePhase[] = {
    "Unknown", "LineA", "LineB", "LineC"};
static const char* kLinkType[] = {
    "HPLC Link", "HRF Link"};
static const char* kBootReason[] = {
    "Normal Boot", "PowerOff Boot", "WatchDog Boot", "PC exception Boot"};
static const char* kReason[] = {
    "Unknown", "Periodically Proxy Change"};
static const char* kRole[] = {
    nullptr, "STA", "PCO", nullptr, "CCO"};
static const char* kCarrierFreq[] = {
    "1.953-11.96MHz", "2.441-5.615MHz", "0.781-2.930MHz", "1.758-2.930MHz"};

/// 把组内翻译应用到一组已 add_fields 的兄弟字段
static void apply_dicts(QVector<MsduFieldNode>& nodes) {
    translate_enum(nodes, "DeviceType", kDeviceType, 8);
    translate_enum(nodes, "MACAddrType", kMacAddrType, 2);
    translate_enum(nodes, "ModuleType", kModuleType, 3);
    translate_enum(nodes, "LinePhase0", kLinePhase, 4);
    translate_enum(nodes, "CandidateLinePhase1", kLinePhase, 4);
    translate_enum(nodes, "CandidateLinePhase2", kLinePhase, 4);
    translate_enum(nodes, "LinkType", kLinkType, 2);
    translate_enum(nodes, "BootReason", kBootReason, 4);
    translate_enum(nodes, "Reason", kReason, 2);
    translate_enum(nodes, "Role", kRole, 5);
    translate_enum(nodes, "CarrierFreq", kCarrierFreq, 4);
    // LinkType0..4(AssocReq/ChangeProxyReq)
    for (int i = 0; i <= 4; ++i) {
        QString fn = QStringLiteral("LinkType%1").arg(i);
        translate_enum(nodes, qPrintable(fn), kLinkType, 2);
    }
}

// ================= MMe 子类型字段表(偏移均为 MMeHeadSize=4 + 相对偏移) =================
// ---- MMeAssocReq (0x00) ----
// 固定头 b[0..24):STAMACAddr(0,0,48) 5×(TEI(2B)+LinkType(1b)+RSV(3b)) LinePhase
static const FieldSpec kAssocReqSpec[] = {
    {"STAMACAddr",       0, 0, 48, Fmt::MAC},
    {"CandidateTEI0",    6, 0, 12, Fmt::DEC},
    {"LinkType0",        7, 4, 1,  Fmt::DEC},
    {"RSV0",             7, 5, 3,  Fmt::DEC},
    {"CandidateTEI1",    8, 0, 12, Fmt::DEC},
    {"LinkType1",        9, 4, 1,  Fmt::DEC},
    {"RSV1",             9, 5, 3,  Fmt::DEC},
    {"CandidateTEI2",   10, 0, 12, Fmt::DEC},
    {"LinkType2",       11, 4, 1,  Fmt::DEC},
    {"RSV2",            11, 5, 3,  Fmt::DEC},
    {"CandidateTEI3",   12, 0, 12, Fmt::DEC},
    {"LinkType3",       13, 4, 1,  Fmt::DEC},
    {"RSV3",            13, 5, 3,  Fmt::DEC},
    {"CandidateTEI4",   14, 0, 12, Fmt::DEC},
    {"LinkType4",       15, 4, 1,  Fmt::DEC},
    {"RSV4",            15, 5, 3,  Fmt::DEC},
    {"LinePhase0",      16, 0, 2,  Fmt::DEC},
    {"CandidateLinePhase1", 16, 2, 2, Fmt::DEC},
    {"CandidateLinePhase2", 16, 4, 2,  Fmt::DEC},
    {"RSV5",            16, 6, 2,  Fmt::DEC},
    {"DeviceType",      17, 0, 8,  Fmt::DEC},
    {"MACAddrType",     18, 0, 8,  Fmt::DEC},
    {"ModuleType",      19, 0, 2,  Fmt::DEC},
    {"RSV6",            19, 2, 6,  Fmt::DEC},
    {"STAAssocRandomData", 20, 0, 32, Fmt::HEX8},
};
static const int kAssocReqSpecN = int(sizeof(kAssocReqSpec) / sizeof(kAssocReqSpec[0]));

// 站点版本信息(513211 表66,相对 42:10B):
// 系统启动原因/BOOT版本号/软件版本号(BCD)/版本时间(年7+月4+日5)/
// 厂商代码(ASCII)/芯片代码
static const FieldSpec kStaVerSpec[] = {
    {"BootReason",        0, 0, 8,  Fmt::DEC},
    {"BootVersion",       1, 0, 8,  Fmt::DEC},
    {"SoftwareVersion",   2, 0, 16, Fmt::HEX4},
    {"VersionDataYear",   4, 0, 7,  Fmt::DEC},
    {"VersionDataMonth",  4, 7, 4,  Fmt::DEC},
    {"VersionDataDay",    5, 3, 5,  Fmt::DEC},
    {"ManufacturerID",    6, 0, 16, Fmt::HEX4},
    {"ChipID",            8, 0, 16, Fmt::HEX4},
};
static const int kStaVerSpecN = int(sizeof(kStaVerSpec) / sizeof(kStaVerSpec[0]));

// AssocReq 版本信息之后(52..60):硬/软复位累积次数、代理类型、保留、端到端序号
static const FieldSpec kAssocReqTailSpec[] = {
    {"HardRstCount",      52, 0, 16, Fmt::DEC},
    {"SoftRstCount",      54, 0, 16, Fmt::DEC},
    {"ProxyType",         56, 0, 8,  Fmt::DEC},
    {"RSV7",              57, 0, 24, Fmt::DEC},
    {"EndSequence",       60, 0, 32, Fmt::HEX8},
};
static const int kAssocReqTailSpecN =
    int(sizeof(kAssocReqTailSpec) / sizeof(kAssocReqTailSpec[0]));

// 变长尾随字节区(AssocReq 24..42 ManufacturerInfo 18B;64..88 ManagementID 24B)
static QString bytes_hex(const QByteArray& d, int from, int len) {
    QByteArray s = d.mid(from, len).toHex(' ');
    return QString::fromLatin1(s).toUpper();
}

// ---- MMeAssocCnf (0x01) ----
static const FieldSpec kAssocCnfSpec[] = {
    {"STAMACAddr",       0, 0, 48, Fmt::MAC},
    {"CCOMACAddr",       6, 0, 48, Fmt::MAC},
    {"AssocResult",     12, 0, 8,  Fmt::DEC},
    {"STALevel",        13, 0, 8,  Fmt::DEC},
    {"STATEI",          14, 0, 12, Fmt::DEC},
    {"LinkType",        15, 4, 1,  Fmt::DEC},
    {"CarrierFreq",     15, 5, 2,  Fmt::DEC},
    {"RSV0",            15, 7, 1,  Fmt::DEC},
    {"ProxyTEI",        16, 0, 12, Fmt::DEC},
    {"RSV1",            17, 4, 4,  Fmt::DEC},
    {"TotalPacketNum",  18, 0, 8,  Fmt::DEC},
    {"PacketIndex",     19, 0, 8,  Fmt::DEC},
    {"STAAssocRandomData", 20, 0, 32, Fmt::HEX8},
    {"STAReAssocTime",  24, 0, 32, Fmt::DEC},
    {"EndSequence",     28, 0, 32, Fmt::HEX8},
    {"PathSequence",    32, 0, 32, Fmt::HEX8},
    {"RSV2",            36, 0, 32, Fmt::DEC},
};
static const int kAssocCnfSpecN = int(sizeof(kAssocCnfSpec) / sizeof(kAssocCnfSpec[0]));
static const FieldSpec kAssocGatherIndSpec[] = {
    // 关联汇总指示(5134 表76,相对 b=MMeHeadSize 起)
    {"AssocResult",      0, 0, 8,  Fmt::DEC},
    {"STALevel",         1, 0, 8,  Fmt::DEC},
    {"CCOMACAddr",       2, 0, 48, Fmt::MAC},
    {"ProxyTEI",         8, 0, 12, Fmt::DEC},
    {"CarrierFreq",      9, 4, 2,  Fmt::DEC},
    {"RSV0",             9, 6, 2,  Fmt::DEC},
    {"RSV1",            10, 0, 8,  Fmt::DEC},
    {"NewSTANumber",    11, 0, 8,  Fmt::DEC},
    {"RSV2",            12, 0, 32, Fmt::DEC},
};
static const int kAssocGatherIndSpecN = int(sizeof(kAssocGatherIndSpec) / sizeof(kAssocGatherIndSpec[0]));

// ---- MMeChangeProxyReq (0x03) ----
static const FieldSpec kChangeProxyReqSpec[] = {
    {"STATEI",         0, 0, 12, Fmt::DEC},
    {"RSV0",           1, 4, 4,  Fmt::DEC},
    {"NewProxyTEI0",   2, 0, 12, Fmt::DEC},
    {"LinkType0",      3, 4, 1,  Fmt::DEC},
    {"RSV1",           3, 5, 3,  Fmt::DEC},
    {"NewProxyTEI1",   4, 0, 12, Fmt::DEC},
    {"LinkType1",      5, 4, 1,  Fmt::DEC},
    {"RSV2",           5, 5, 3,  Fmt::DEC},
    {"NewProxyTEI2",   6, 0, 12, Fmt::DEC},
    {"LinkType2",      7, 4, 1,  Fmt::DEC},
    {"RSV3",           7, 5, 3,  Fmt::DEC},
    {"NewProxyTEI3",   8, 0, 12, Fmt::DEC},
    {"LinkType3",      9, 4, 1,  Fmt::DEC},
    {"RSV4",           9, 5, 3,  Fmt::DEC},
    {"NewProxyTEI4",  10, 0, 12, Fmt::DEC},
    {"LinkType4",     11, 4, 1,  Fmt::DEC},
    {"RSV5",          11, 5, 3,  Fmt::DEC},
    {"OldProxyTEI",   12, 0, 12, Fmt::DEC},
    {"RSV6",          13, 4, 4,  Fmt::DEC},
    {"ProxyType",     14, 0, 8,  Fmt::DEC},
    {"Reason",        15, 0, 8,  Fmt::DEC},
    {"EndSequence",   16, 0, 32, Fmt::HEX8},
    {"LinePhase0",    20, 0, 2,  Fmt::DEC},
    {"CandidateLinePhase1", 20, 2, 2, Fmt::DEC},
    {"CandidateLinePhase2", 20, 4, 2, Fmt::DEC},
    {"RSV7",          20, 6, 2,  Fmt::DEC},
    {"Reserved",      21, 0, 24, Fmt::DEC},
};
static const int kChangeProxyReqSpecN = int(sizeof(kChangeProxyReqSpec) / sizeof(kChangeProxyReqSpec[0]));

// ---- MMeChangeProxyBitMapCnf (0x05) ----
static const FieldSpec kChangeProxyBitMapCnfSpec[] = {
    {"Result",        0, 0, 8,  Fmt::DEC},
    {"RSV0",          1, 0, 8,  Fmt::DEC},
    {"BitMapSize",    2, 0, 16, Fmt::DEC},
    {"STATEI",        4, 0, 12, Fmt::DEC},
    {"LinkType",      5, 4, 1,  Fmt::DEC},
    {"RSV1",          5, 5, 3,  Fmt::DEC},
    {"ProxyTEI",      6, 0, 12, Fmt::DEC},
    {"RSV2",          7, 4, 4,  Fmt::DEC},
    {"EndSequence",   8, 0, 32, Fmt::HEX8},
    {"PathSequence", 12, 0, 32, Fmt::HEX8},
    {"RSV3",         16, 0, 32, Fmt::DEC},
};
static const int kChangeProxyBitMapCnfSpecN = int(sizeof(kChangeProxyBitMapCnfSpec) / sizeof(kChangeProxyBitMapCnfSpec[0]));

// ---- MMeHeartBeatCheck (0x07) ----
static const FieldSpec kHeartBeatSpec[] = {
    {"OriginalSourceTEI", 0, 0, 12, Fmt::DEC},
    {"RSV0",              1, 4, 4,  Fmt::DEC},
    {"DiscoverCountTEI",  2, 0, 12, Fmt::DEC},
    {"RSV1",              3, 4, 4,  Fmt::DEC},
    {"DiscoverCount",     4, 0, 16, Fmt::DEC},
    {"BitMapSize",        6, 0, 16, Fmt::DEC},
};
static const int kHeartBeatSpecN = int(sizeof(kHeartBeatSpec) / sizeof(kHeartBeatSpec[0]));

// ---- MMeDiscoverNodeList (0x08) ----
static const FieldSpec kDiscoverNodeListSpec[] = {
    {"STATEI",             0, 0, 12, Fmt::DEC},
    {"ProxyTEI",           1, 4, 12, Fmt::DEC},
    {"Role",               3, 0, 4,  Fmt::DEC},
    {"Level",              3, 4, 4,  Fmt::DEC},
    {"MACAddr",            4, 0, 48, Fmt::MAC},
    {"CCOMACAddr",        10, 0, 48, Fmt::MAC},
    {"LinePhase0",        16, 0, 2,  Fmt::DEC},
    {"CandidateLinePhase1", 16, 2, 2, Fmt::DEC},
    {"CandidateLinePhase2", 16, 4, 2, Fmt::DEC},
    {"RSV0",              16, 6, 2,  Fmt::DEC},
    {"ProxyChannelQuality", 17, 0, 8, Fmt::DEC},
    {"ProxyCommRate",     18, 0, 8,  Fmt::DEC},
    {"ProxyDownCommRate", 19, 0, 8,  Fmt::DEC},
    {"DiscoverNodeNum",   20, 0, 16, Fmt::DEC},
    {"SendDiscoveryPacketCount", 22, 0, 8, Fmt::DEC},
    {"UpRouteEntryNum",   23, 0, 8,  Fmt::DEC},
    {"EvaluateBeginTimeout", 24, 0, 16, Fmt::DEC},
    {"DiscoverySTABitMapSize", 26, 0, 16, Fmt::DEC},
    {"MinCommRate",       28, 0, 8,  Fmt::DEC},
    {"RSV1",              29, 0, 24, Fmt::DEC},
};
static const int kDiscoverNodeListSpecN = int(sizeof(kDiscoverNodeListSpec) / sizeof(kDiscoverNodeListSpec[0]));

// ---- MMeChangeProxyCnf (0x04):固定 20B + 子站点 2B×N ----
static const FieldSpec kChangeProxyCnfSpec[] = {
    {"Result",            0, 0, 8,  Fmt::DEC},
    {"TotalPacketNum",    1, 0, 8,  Fmt::DEC},
    {"PacketIndex",       2, 0, 8,  Fmt::DEC},
    {"RSV0",              3, 0, 8,  Fmt::DEC},
    {"STATEI",            4, 0, 12, Fmt::DEC},
    {"LinkType",          5, 4, 1,  Fmt::DEC},
    {"RSV1",              5, 5, 3,  Fmt::DEC},
    {"ProxyTEI",          6, 0, 12, Fmt::DEC},
    {"RSV2",              7, 4, 4,  Fmt::DEC},
    {"EndSequence",       8, 0, 32, Fmt::HEX8},
    {"PathSequence",     12, 0, 32, Fmt::HEX8},
    {"ChildSum",         16, 0, 16, Fmt::DEC},
    {"RSV3",             18, 0, 16, Fmt::DEC},
};
static const int kChangeProxyCnfSpecN = int(sizeof(kChangeProxyCnfSpec) / sizeof(kChangeProxyCnfSpec[0]));

// ---- MMeLeaveInd (0x06):固定 16B + 离线站点 MAC 6B×N ----
static const FieldSpec kLeaveIndSpec[] = {
    {"Reason",            0, 0, 16, Fmt::DEC},
    {"LeaveSTANum",       2, 0, 16, Fmt::DEC},
    {"LeaveDelayTime",    4, 0, 16, Fmt::DEC},
    {"RSV0",              6, 0, 80, Fmt::DEC},
};
static const int kLeaveIndSpecN = int(sizeof(kLeaveIndSpec) / sizeof(kLeaveIndSpec[0]));

// ---- MMeNetworkConflictReport (0x0A):固定 8B + NIDSize×N 邻居网络号 ----
static const FieldSpec kNetworkConflictSpec[] = {
    {"CCOMACAddr",        0, 0, 48, Fmt::MAC},
    {"NeighbourNetWorkCount", 6, 0, 8, Fmt::DEC},
    {"NIDSize",           7, 0, 8,  Fmt::DEC},
};
static const int kNetworkConflictSpecN = int(sizeof(kNetworkConflictSpec) / sizeof(kNetworkConflictSpec[0]));

// ---- MMeRFChannelConflictReport (0x80):固定 7B + channel/option 各 Count 字节 ----
static const FieldSpec kRFChannelConflictSpec[] = {
    {"CCOMACAddr",        0, 0, 48, Fmt::MAC},
    {"NeighbourNetWorkCount", 6, 0, 8, Fmt::DEC},
};
static const int kRFChannelConflictSpecN = int(sizeof(kRFChannelConflictSpec) / sizeof(kRFChannelConflictSpec[0]));

// ---- 路由/链路类(0x50-0x55,公共头版本 0x00) ----
static const FieldSpec kRouteReqReplyHeadSpec[] = {   // RouteRequest/RouteReply 前 7B
    {"RouteVersion",      0, 0, 8,  Fmt::DEC},
    {"RouterReqSN",       1, 0, 32, Fmt::HEX8},
};
static const int kRouteReqReplyHeadSpecN = int(sizeof(kRouteReqReplyHeadSpec) / sizeof(kRouteReqReplyHeadSpec[0]));
static const FieldSpec kRouteRequestTailSpec[] = {    // 字节5-6(RouteRequest)
    {"RSV0",              5, 0, 3,  Fmt::DEC},
    {"RoutePriorFlag",    5, 3, 1,  Fmt::DEC},
    {"LoadDataType",      5, 4, 4,  Fmt::DEC},
    {"LoadDataLength",    6, 0, 8,  Fmt::DEC},
};
static const int kRouteRequestTailSpecN = int(sizeof(kRouteRequestTailSpec) / sizeof(kRouteRequestTailSpec[0]));
static const FieldSpec kRouteReplyTailSpec[] = {       // 字节5-6(RouteReply)
    {"RSV0",              5, 0, 4,  Fmt::DEC},
    {"LoadDataType",      5, 4, 4,  Fmt::DEC},
    {"LoadDataLength",    6, 0, 8,  Fmt::DEC},
};
static const int kRouteReplyTailSpecN = int(sizeof(kRouteReplyTailSpec) / sizeof(kRouteReplyTailSpec[0]));
static const FieldSpec kRouteAckSpec[] = {            // RouteAck 8B
    {"RouteVersion",      0, 0, 8,  Fmt::DEC},
    {"RSV0",              1, 0, 24, Fmt::DEC},
    {"RouterReqSN",       4, 0, 32, Fmt::HEX8},
};
static const int kRouteAckSpecN = int(sizeof(kRouteAckSpec) / sizeof(kRouteAckSpec[0]));
static const FieldSpec kRouteErrorSpec[] = {          // RouteError 前 7B
    {"Version",           0, 0, 8,  Fmt::DEC},
    {"RouterReqSN",       1, 0, 32, Fmt::HEX8},
    {"RSV0",              5, 0, 8,  Fmt::DEC},
    {"UnReachableSTANum", 6, 0, 8,  Fmt::DEC},
};
static const int kRouteErrorSpecN = int(sizeof(kRouteErrorSpec) / sizeof(kRouteErrorSpec[0]));
static const FieldSpec kLinkConfirmRequestSpec[] = {  // 链路确认请求前 7B
    {"Version",           0, 0, 8,  Fmt::DEC},
    {"RouterReqSN",       1, 0, 32, Fmt::HEX8},
    {"RSV0",              5, 0, 8,  Fmt::DEC},
    {"AckStaNum",         6, 0, 8,  Fmt::DEC},
};
static const int kLinkConfirmRequestSpecN = int(sizeof(kLinkConfirmRequestSpec) / sizeof(kLinkConfirmRequestSpec[0]));
static const FieldSpec kLinkConfirmResponseSpec[] = { // 链路确认回应 8B
    {"Version",           0, 0, 8,  Fmt::DEC},
    {"Level",             1, 0, 8,  Fmt::DEC},
    {"ChannelQuality",    2, 0, 8,  Fmt::DEC},
    {"RoutePriorityFlag", 3, 0, 1,  Fmt::DEC},
    {"RSV0",              3, 1, 7,  Fmt::DEC},
    {"RouterReqSN",       4, 0, 32, Fmt::HEX8},
};
static const int kLinkConfirmResponseSpecN = int(sizeof(kLinkConfirmResponseSpec) / sizeof(kLinkConfirmResponseSpec[0]));

// ---- MMeDiagnose (0x4F):芯片厂商 ID ----
static const FieldSpec kDiagnoseSpec[] = {
    {"ChipManufacID",     0, 0, 16, Fmt::DEC},
};
static const int kDiagnoseSpecN = int(sizeof(kDiagnoseSpec) / sizeof(kDiagnoseSpec[0]));

// ---- MMeZeroCrossNTBCollectInd (0x0B):固定 8B ----
static const FieldSpec kZeroCrossCollectSpec[] = {
    {"STATEI",            0, 0, 16, Fmt::DEC},
    {"RSV0",              1, 4, 4,  Fmt::DEC},
    {"NTBCollectionMode", 2, 0, 8,  Fmt::DEC},
    {"NTBCollectionPeriod", 3, 0, 8, Fmt::DEC},
    {"NTBCollectionQuantity", 4, 0, 8, Fmt::DEC},
    {"RSV1",              5, 0, 24, Fmt::DEC},
};
static const int kZeroCrossCollectSpecN = int(sizeof(kZeroCrossCollectSpec) / sizeof(kZeroCrossCollectSpec[0]));

// ---- MMeZeroCrossNTBReport (0x0C):固定 10B + 12bit 差分 NTB 打包流 ----
static const FieldSpec kZeroCrossReportSpec[] = {
    {"STATEI",            0, 0, 12, Fmt::DEC},
    {"RSV0",              1, 4, 4,  Fmt::DEC},
    {"TotalCount",        2, 0, 8,  Fmt::DEC},
    {"LineATotalCount",   3, 0, 8,  Fmt::DEC},
    {"LineBTotalCount",   4, 0, 8,  Fmt::DEC},
    {"LineCTotalCount",   5, 0, 8,  Fmt::DEC},
    {"NTBBase",           6, 0, 32, Fmt::DEC},
};
static const int kZeroCrossReportSpecN = int(sizeof(kZeroCrossReportSpec) / sizeof(kZeroCrossReportSpec[0]));

/// @brief 渲染 RouteRequest/RouteReply 的 LoadData 段(MMe_RoutePathList,4B×N):
///        每 4B = TEI(12b)+RSV0(4b)+CommSuccRate(1B,%)+ChannelQuality(1B,dB)
static void annotate_route_loaddata(QVector<MsduFieldNode>& nodes,
                                    const QByteArray& b, int rel_base) {
    for (auto& n : nodes) {
        if (!n.name.startsWith(QStringLiteral("LoadDataType"))) continue;
        const int v = n.value.toInt();
        n.value = QStringLiteral("%1 - %2").arg(v).arg(
            v == 1 ? QStringLiteral("LoadData included")
                   : QStringLiteral("No LoadData"));
    }
    if (b.size() < 8) return;
    const int len = (int)get_bits(b, 6, 0, 8);   // LoadDataLength
    const int cnt = len / 4;
    if (cnt <= 0) return;
    auto& pg = group(nodes, QStringLiteral("RoutePathList [%1]").arg(cnt));
    for (int i = 0; i < cnt && 7 + 4 * i + 4 <= b.size(); ++i) {
        const int off = 7 + 4 * i;
        const int rel0 = rel_base + off;
        auto& rp = group(pg.children, QStringLiteral("RoutePath[%1]").arg(i));
        MsduFieldNode t;
        t.name  = QStringLiteral("TEI [12b]");
        t.value = QString::number((quint16)get_bits(b, off, 0, 12));
        t.rel_start = rel0; t.rel_len = 2;
        rp.children.append(t);
        MsduFieldNode rv;
        rv.name  = QStringLiteral("RSV0 [4b]");
        rv.value = QString::number((quint8)get_bits(b, off + 1, 4, 4));
        rv.rel_start = rel0 + 1; rv.rel_len = 1;
        rp.children.append(rv);
        MsduFieldNode cr;
        cr.name  = QStringLiteral("CommSuccRate");
        cr.value = QStringLiteral("%1%").arg((quint8)b[off + 2]);
        cr.rel_start = rel0 + 2; cr.rel_len = 1;
        rp.children.append(cr);
        MsduFieldNode cq;
        cq.name  = QStringLiteral("ChannelQuality");
        cq.value = QStringLiteral("%1 dB").arg((quint8)b[off + 3]);
        cq.rel_start = rel0 + 3; cq.rel_len = 1;
        rp.children.append(cq);
    }
}

// ---- MMeSuccessRateReport (0x09) ----
static const FieldSpec kSuccessRateSpec[] = {
    {"ProxySTATEI", 0, 0, 12, Fmt::DEC},
    {"RSV0",        1, 4, 4,  Fmt::DEC},
    {"STANumber",   2, 0, 16, Fmt::DEC},
};
static const int kSuccessRateSpecN = int(sizeof(kSuccessRateSpec) / sizeof(kSuccessRateSpec[0]));

// ================= APP 公共头(APP_BASE) =================
static const FieldSpec kAppBaseSpec[] = {
    {"PortNum",      0, 0, 8,  Fmt::HEX4},
    {"PacketID",     1, 0, 16, Fmt::HEX4},
    {"PacketCtrWord", 3, 0, 8, Fmt::HEX4},
};
static const int kAppBaseSpecN = int(sizeof(kAppBaseSpec) / sizeof(kAppBaseSpec[0]));

// ================= MSDU_BASE_S 简头 =================
static const FieldSpec kMsduBaseSSpec[] = {
    {"Version",   0, 0, 4,  Fmt::DEC},
    {"RSV0",      0, 4, 4,  Fmt::DEC},
    {"MSDUType",  1, 0, 8,  Fmt::DEC},
    {"MSDULen",   2, 0, 11, Fmt::DEC},
    {"RSV1",      3, 3, 5,  Fmt::DEC},
};
static const int kMsduBaseSSpecN = int(sizeof(kMsduBaseSSpec) / sizeof(kMsduBaseSSpec[0]));

// ================= 主解析 =================
MsduInfo MsduParser::parse(const QByteArray& body) {
    MsduInfo out;
    out.present = false;
    out.simple_head = false;
    if (body.size() < 8) return out;

    const quint8* p = reinterpret_cast<const quint8*>(body.constData());
    // 单跳帧判定:data[0] bit0 = 1 时是简头(MSDU_BASE_S)
    bool simple = (p[0] & 0x01) != 0;

    if (simple) {
        out.simple_head = true;
        add_fields(out.tree, body, 0, kMsduBaseSSpec, kMsduBaseSSpecN);
        // MSDUType 简头字典(与 Python MSDU_BASE_S 一致):0/128/129
        translate_enum_sparse(out.tree, "MSDUType", {
            {0, QStringLiteral("Find List Message")},
            {128, QStringLiteral("Application Level Message")},
            {129, QStringLiteral("IPv4 Message")},
        });
        quint8 msdu_type = (quint8)get_bits(p, 1, 0, 8);
        int   msdu_len   = (int)get_bits(p, 2, 0, 11);
        QByteArray msdu_body = body.mid(4, msdu_len);
        if (msdu_type == 0) {
            out.summary = QStringLiteral("Find List Message");
        } else {
            out.summary = QStringLiteral("Simple MSDU type %1").arg(msdu_type);
        }
        out.present = true;
        // 简头暂不深解析子字段(回放数据为 0,不出现)
        return out;
    }

    // ---- 标准 MSDU_BASE(16B 头, MACAddrFlag=1 时 28B)----
    if (body.size() < 16) return out;
    add_fields(out.tree, body, 0, kMsduBaseSpec, kMsduBaseSpecN);
    annotate_msdu_base(out.tree);   // 标志/枚举字段加数值解释
    out.msdu_seq = (quint16)get_bits(p, 5, 0, 16);   // MSDU Seq(MSDUIndex)
    // 原始发起/终点 TEI(长头 MSDU_BASE;供列表 Orig 列与方向判定)
    out.msdu_src_tei  = (int)get_bits(p, 0, 4, 12);
    out.msdu_dst_tei  = (int)get_bits(p, 2, 0, 12);
    out.msdu_send_type = (int)get_bits(p, 3, 4, 4);   // 广播类型判定
    quint8 msdu_type   = (quint8)get_bits(p, 7, 0, 8);
    int    msdu_len    = (int)get_bits(p, 8, 0, 11);
    bool   mac_flag    = get_bits(p, 11, 3, 1) != 0;
    int    head_size   = mac_flag ? 28 : 16;

    if (mac_flag && body.size() >= 28) {
        add_fields(out.tree, body, 0, kMsduMacSpec, 2);
        // 提取源/目的 MAC(48-bit),供 TEI→MAC 映射表与列表地址显示
        out.msdu_src_mac = get_bits(p, 16, 0, 48);
        out.msdu_dst_mac = get_bits(p, 22, 0, 48);
    }

    QByteArray msdu_body = body.mid(head_size, msdu_len);
    out.present = true;

    if (msdu_type == 0) {
        // ---- 网络管理消息:MMe_BASE(4B) ----
        if (msdu_body.size() < 4) {
            out.summary = QStringLiteral("MMe (truncated)");
            return out;
        }
        quint8 mm_type = (quint8)get_bits(msdu_body, 0, 0, 8);
        out.summary = mme_type_name(mm_type);
        auto& root = group(out.tree, QStringLiteral("MMe: %1").arg(out.summary));

        // MMe_BASE 头 4B = [MMType 2B(小端)][RSV 2B];真实帧如 08 00 00 00。
        // MMType 字段行(数值 + 类型名,高亮指向 MMe_BASE 前 2 字节)
        {
            MsduFieldNode mt;
            mt.name = QStringLiteral("MMType [16b]");
            quint16 mmv = (quint16)get_bits(msdu_body, 0, 0, 16);
            mt.value = QStringLiteral("%1 - %2").arg(mmv).arg(out.summary);
            mt.rel_start = head_size;   // MMe_BASE 位于 msdu_body[0],相对 body 起点
            mt.rel_len   = 2;           // MMType 占 2 字节
            root.children.append(mt);
        }
        // RSV 16bit(byte2-3)
        MsduFieldNode rsv;
        rsv.name  = QStringLiteral("RSV [16b]");
        rsv.value = QString::number(get_bits(msdu_body, 2, 0, 16));
        rsv.rel_start = head_size + 2;
        rsv.rel_len   = 2;
        root.children.append(rsv);

        // body 从 MMeHeadSize=4 起
        QByteArray b = msdu_body.mid(4);
        switch (mm_type) {
            case MME_ASSOC_REQ: {
                // MMeAssocReq(关联请求),字段按 51321/表60 结构:
                // 固定区(0..23)→ 厂家自定义信息(24-41)→ 站点版本信息
                // (42-51,组)→ 复位计数/代理类型/端到端序号(52..60)→ 管理ID
                add_fields(root.children, b, 0, kAssocReqSpec, kAssocReqSpecN, head_size + 4);
                MsduFieldNode mn;
                mn.name  = QStringLiteral("ManufacturerInfo [144b]");
                mn.value = bytes_hex(b, 24, 18);
                mn.rel_start = head_size + 4 + 24;
                mn.rel_len   = 18;
                root.children.append(mn);
                // 站点版本信息(513211 表66)
                auto& ver = group(root.children, trl::L("站点版本信息"));
                add_fields(ver.children, b, 42, kStaVerSpec, kStaVerSpecN, head_size + 4);
                for (auto& v : ver.children) {
                    // 软件版本号:BCD(与本地通信模块接口协议一致)
                    if (v.name.startsWith(QStringLiteral("SoftwareVersion")))
                        v.value += QStringLiteral(" (BCD)");
                    // 厂商代码:ASCII(表66;值为 2 字节 ASCII)
                    else if (v.name.startsWith(QStringLiteral("ManufacturerID"))) {
                        const QByteArray as = b.mid(48, 2);
                        QString a;
                        for (char c : as) a += c >= 0x20 && c < 0x7F ? c : '.';
                        v.value = QStringLiteral("\"%1\" (0x%2)")
                                      .arg(a).arg(quint16(v.value.toUInt(nullptr, 16)),
                                                  4, 16, QChar('0'));
                    }
                }
                apply_dicts(ver.children);   // 系统启动原因字典(表67)
                add_fields(root.children, b, 0, kAssocReqTailSpec, kAssocReqTailSpecN,
                           head_size + 4);
                apply_dicts(root.children);
                // 代理类型(表69):0=站点动态选择的代理,其它保留
                for (auto& ch : root.children) {
                    if (ch.name.startsWith(QStringLiteral("ProxyType"))) {
                        quint8 pt = (quint8)get_bits(b, 56, 0, 8);
                        ch.value = QStringLiteral("%1 - %2").arg(pt).arg(
                            pt == 0 ? trl::L("站点动态选择的代理")
                                    : trl::L("保留"));
                    }
                }
                MsduFieldNode mid;
                mid.name  = QStringLiteral("ManagementID [192b]");
                mid.value = bytes_hex(b, 64, 24);
                mid.rel_start = head_size + 4 + 64;
                mid.rel_len   = 24;
                root.children.append(mid);
                break;
            }
            case MME_ASSOC_CNF: {
                // MMeAssocCnf(关联确认):固定头到 b[40],RouteInfo 从 b[40] 起
                add_fields(root.children, b, 0, kAssocCnfSpec, kAssocCnfSpecN, head_size + 4);
                apply_dicts(root.children);
                annotate_unit(root.children, "STAReAssocTime", QStringLiteral("ms"));
                // 修正 AssocResult 可读文本
                for (auto& ch : root.children) {
                    if (ch.name.startsWith(QStringLiteral("AssocResult")))
                        ch.value = assoc_result_str((quint8)get_bits(b, 12, 0, 8));
                }
                int off = 40;  // 路由表信息起点(表74;MMeHeadSize 4 + 固定 36)
                if (b.size() >= off + 8) {
                    const int rel0 = head_size + 4 + 40;
                    quint16 sta_sum  = (quint16)get_bits(b, off + 0, 0, 16);
                    quint16 pco_sum  = (quint16)get_bits(b, off + 2, 0, 16);
                    quint16 route_sz = (quint16)get_bits(b, off + 4, 0, 16);
                    quint16 rsv3     = (quint16)get_bits(b, off + 6, 0, 16);
                    // 路由信息头(表74):直连站点数/直连代理数/路由表大小/保留
                    auto& route = group(root.children, QStringLiteral("RouteInfo"),
                                        QStringLiteral("%1 B").arg(route_sz));
                    MsduFieldNode r1;
                    r1.name  = QStringLiteral("StraightSTASum [16b]");
                    r1.value = QString::number(sta_sum);
                    r1.rel_start = rel0 + 0; r1.rel_len = 2;
                    route.children.append(r1);
                    MsduFieldNode r2;
                    r2.name  = QStringLiteral("StraightPCOSum [16b]");
                    r2.value = QString::number(pco_sum);
                    r2.rel_start = rel0 + 2; r2.rel_len = 2;
                    route.children.append(r2);
                    MsduFieldNode r3;
                    r3.name  = QStringLiteral("RouteInfoTableSize [16b]");
                    r3.value = QStringLiteral("%1 B").arg(route_sz);
                    r3.rel_start = rel0 + 4; r3.rel_len = 2;
                    route.children.append(r3);
                    MsduFieldNode r4;
                    r4.name  = QStringLiteral("RSV3 [16b]");
                    r4.value = QString::number(rsv3);
                    r4.rel_start = rel0 + 6; r4.rel_len = 2;
                    route.children.append(r4);
                    off += 8;
                    // 直连站点表(表75 前半):2B/条 = TEI(12b)+链路类型(1b)+保留(3b)
                    for (int i = 0; i < sta_sum && off + 2 <= b.size(); ++i) {
                        quint16 e = (quint16)get_bits(b, off, 0, 16);
                        off += 2;
                        MsduFieldNode sn;
                        sn.name = QStringLiteral("STA[%1]").arg(i);
                        sn.value = QStringLiteral("TEI=%1 LinkType=%2")
                                       .arg(e & 0x0FFF).arg((e >> 12) & 0x01);
                        sn.rel_start = rel0 + 8 + 2 * i;
                        sn.rel_len   = 2;
                        route.children.append(sn);
                    }
                    // 直连代理表(表75 后半):代理 2B(TEI12+链路1+保留3)+子站点数
                    // 2B + 子站点 2B/条(TEI12+保留4b)
                    for (int i = 0; i < pco_sum && off + 4 <= b.size(); ++i) {
                        quint16 tei    = (quint16)get_bits(b, off + 0, 0, 12);
                        quint8  plink  = (quint8)get_bits(b, off + 1, 4, 1);
                        quint16 child  = (quint16)get_bits(b, off + 2, 0, 16);
                        MsduFieldNode pn;
                        pn.name = QStringLiteral("PCO[%1]").arg(tei);
                        pn.value = QStringLiteral("LinkType=%1 childSum=%2")
                                       .arg(plink).arg(child);
                        pn.rel_start = rel0 + 8 + 2 * sta_sum + i * 4;
                        pn.rel_len   = 4;
                        route.children.append(pn);
                        off += 4;
                        for (int j = 0; j < child && off + 2 <= b.size(); ++j) {
                            quint16 ctei = (quint16)get_bits(b, off, 0, 16);
                            off += 2;
                            MsduFieldNode cn;
                            cn.name = QStringLiteral("Child[%1]").arg(j);
                            cn.value = QStringLiteral("TEI=%1").arg(ctei & 0x0FFF);
                            cn.rel_start = pn.rel_start + 4 + 2 * j;
                            cn.rel_len   = 2;
                            pn.children.append(cn);
                        }
                    }
                }
                break;
            }
            case MME_ASSOC_GATHER_IND: {
                // MMeAssocGatherInd(关联汇总指示,5134):固定头 16B(表76)
                // + 站点信息表(表78,每条 8B=MAC6+TEI12b+保留4b,16 起)
                add_fields(root.children, b, 0, kAssocGatherIndSpec,
                           kAssocGatherIndSpecN, head_size + 4);
                apply_dicts(root.children);
                // 结果(51342):固定 0=允许加入网络,其它无效
                for (auto& ch : root.children) {
                    if (!ch.name.startsWith(QStringLiteral("AssocResult"))) continue;
                    quint8 ar = (quint8)get_bits(b, 0, 0, 8);
                    ch.value = QStringLiteral("%1 - %2").arg(ar).arg(
                        ar == 0 ? QStringLiteral("Allowed to join network")
                                : QStringLiteral("Invalid"));
                }
                int sta_num = (int)get_bits(b, 11, 0, 8);  // 汇总站点数(51347)
                int off = 16;                              // 站点信息起点
                if (sta_num > 0 && off + 8 <= b.size()) {
                    auto& sgi = group(root.children,
                                      QStringLiteral("STAInfo [%1]").arg(sta_num));
                    for (int i = 0; i < sta_num && off + 8 <= b.size(); ++i) {
                        const int rel0 = head_size + 4 + off;
                        auto& ns = group(sgi.children,
                                         QStringLiteral("NewSTA[%1]").arg(i));
                        MsduFieldNode mac;
                        mac.name  = QStringLiteral("STAMACAddr [48b]");
                        mac.value = mac_str((quint64)get_bits(b, off, 0, 48));
                        mac.rel_start = rel0; mac.rel_len = 6;
                        ns.children.append(mac);
                        MsduFieldNode tei;
                        tei.name  = QStringLiteral("STATEI [12b]");
                        tei.value = QString::number((quint16)get_bits(b, off + 6, 0, 12));
                        tei.rel_start = rel0 + 6; tei.rel_len = 2;
                        ns.children.append(tei);
                        MsduFieldNode rv;
                        rv.name  = QStringLiteral("RSV0 [4b]");
                        rv.value = QString::number((quint8)get_bits(b, off + 7, 4, 4));
                        rv.rel_start = rel0 + 7; rv.rel_len = 1;
                        ns.children.append(rv);
                        off += 8;
                    }
                }
                break;
            }
            case MME_CHANGE_PROXY_REQ:
                add_fields(root.children, b, 0, kChangeProxyReqSpec, kChangeProxyReqSpecN, head_size + 4);
                apply_dicts(root.children);
                break;
            case MME_CHANGE_PROXY_BITMAP_CNF: {
                // MMeChangeProxyBitMapCnf:固定头到 b[20],随后 BitMap(BitMapSize 字节)
                add_fields(root.children, b, 0, kChangeProxyBitMapCnfSpec, kChangeProxyBitMapCnfSpecN, head_size + 4);
                apply_dicts(root.children);
                int bm_size = (int)get_bits(b, 2, 0, 16);
                if (bm_size > 0 && b.size() >= 20 + bm_size) {
                    auto& bmg = group(root.children, QStringLiteral("ProxyChildSTA BitMap [%1B]").arg(bm_size));
                    QByteArray bm = b.mid(20, bm_size);
                    QString teis;
                    for (int i = 0; i < bm.size(); ++i) {
                        quint8 byte = (quint8)bm[i];
                        for (int j = 0; j < 8; ++j) {
                            if (byte & (1u << j)) {
                                teis += QString("%1, ").arg(8 * i + j);
                            }
                        }
                    }
                    MsduFieldNode bl;
                    bl.name  = QStringLiteral("ChildSTATEI");
                    bl.value = teis.isEmpty() ? QStringLiteral("(none)") : teis;
                    bmg.children.append(bl);
                }
                break;
            }
            case MME_HEARTBEAT_CHECK: {
                // MMeHeartBeatCheck:bitmap → TEI 列表
                add_fields(root.children, b, 0, kHeartBeatSpec, kHeartBeatSpecN, head_size + 4);
                apply_dicts(root.children);
                int bm_size = (int)get_bits(b, 6, 0, 16);
                if (bm_size > 0 && b.size() >= 8 + bm_size) {
                    QByteArray bm = b.mid(8, bm_size);
                    const int rel0 = head_size + 4 + 8;  // bitmap 相对 body 起点
                    auto& hlg = group(root.children, QStringLiteral("DiscoverSTAList"));
                    int order = 0;
                    for (int i = 0; i < bm.size(); ++i) {
                        quint8 byte = (quint8)bm[i];
                        QString teis;
                        for (int j = 0; j < 8; ++j) {
                            if (!(byte & (1u << j))) continue;
                            if (!teis.isEmpty()) teis += QStringLiteral(", ");
                            teis += QString::number(8 * i + j);
                            ++order;
                        }
                        if (teis.isEmpty()) continue;   // 该字节无置位
                        MsduFieldNode dl;
                        dl.name  = QStringLiteral("DiscoverSTATEI[%1]").arg(i);
                        dl.value = teis;
                        dl.rel_start = rel0 + i;   // 该字节
                        dl.rel_len   = 1;
                        hlg.children.append(dl);
                    }
                    if (order == 0) {
                        MsduFieldNode dl;
                        dl.name  = QStringLiteral("DiscoverSTATEI");
                        dl.value = QStringLiteral("(none)");
                        hlg.children.append(dl);
                    }
                }
                break;
            }
            case MME_DISCOVER_NODE_LIST: {
                // MMeDiscoverNodeList
                add_fields(root.children, b, 0, kDiscoverNodeListSpec, kDiscoverNodeListSpecN, head_size + 4);
                // 学习 TEI→MAC:STATEI→MACAddr、CCOMACAddr(TEI=1)
                if (b.size() >= 16) {
                    const quint16 sta_tei = (quint16)get_bits(b, 0, 0, 12);
                    const quint64 sta_mac = get_bits(b, 4, 0, 48);
                    const quint64 cco_mac = get_bits(b, 10, 0, 48);
                    if (sta_tei != 0 && sta_mac)
                        out.tei_mac_pairs.append({sta_tei, sta_mac});
                    if (cco_mac)
                        out.tei_mac_pairs.append({1, cco_mac});
                }
                apply_dicts(root.children);
                // 成功率字段带 %(与 Python log "ProxyCommRate: 85%" 一致)
                annotate_unit(root.children, "ProxyCommRate", QStringLiteral("%"));
                annotate_unit(root.children, "MinCommRate", QStringLiteral("%"));
                annotate_unit(root.children, "ProxyDownCommRate", QStringLiteral("%"));
                annotate_unit(root.children, "EvaluateBeginTimeout", QStringLiteral("s"));
                annotate_unit(root.children, "ProxyChannelQuality", QStringLiteral("dB"));
                int up_route_num = (int)get_bits(b, 23, 0, 8);
                int bitmap_size  = (int)get_bits(b, 26, 0, 16);
                int node_num     = (int)get_bits(b, 20, 0, 16);
                int off = 32;  // 固定头 32B(MMeHead 4 + DiscoverNodeList 28)
                // UpRoute 条目:2B/条 NextHopTEI(0,0,12) + RouteType(1,4,4)
                // 与 Python MMe_UpRouteInfo 一致;RouteTypeDict 含义翻译
                if (up_route_num > 0 && off + up_route_num * 2 <= b.size()) {
                    auto& upg = group(root.children, QStringLiteral("UpRouteEntryList [%1]").arg(up_route_num));
                    for (int i = 0; i < up_route_num; ++i) {
                        quint16 tei = (quint16)get_bits(b, off, 0, 12);
                        quint8  rtype = (quint8)get_bits(b, off + 1, 4, 4);
                        int abs0 = head_size + 4 + off;   // 条目相对 body 起点
                        off += 2;
                        // 每条目分组
                        auto& ug = group(upg.children,
                                         QStringLiteral("UpRoute[%1]").arg(i));
                        MsduFieldNode nh;
                        nh.name  = QStringLiteral("NextHopTEI [12b]");
                        nh.value = QString::number(tei);
                        nh.rel_start = abs0;      // bit0-11 跨 2B
                        nh.rel_len   = 2;
                        ug.children.append(nh);
                        MsduFieldNode rt;
                        rt.name = QStringLiteral("RouteType [4b]");
                        // 表98 路由类型(中文 key,随界面语言;勿用 QLatin1String 直出中文)
                        static const char* rtd[] = {
                            "错误路由类型", "同级路由类型", "上级路由类型",
                            "代理主路径路由类型", "上上级路由类型"};
                        rt.value = (rtype <= 4)
                            ? QStringLiteral("%1 - %2").arg(rtype)
                                .arg(trl::L(QString::fromUtf8(rtd[rtype])))
                            : QString::number(rtype);
                        rt.rel_start = abs0 + 1;  // byte1 高 4bit
                        rt.rel_len   = 1;
                        ug.children.append(rt);
                    }
                }
                // ---- 发现站点列表位图(按字节分字段解析,勿与接收信息合并) ----
                QByteArray bm;
                int bm_base = -1;   // bitmap 相对 body 起点(供高亮)
                if (bitmap_size > 0 && off + bitmap_size <= b.size()) {
                    bm_base = head_size + 4 + off;
                    bm = b.mid(off, bitmap_size);
                    off += bitmap_size;
                }
                bool bm_any = false;
                int  nset = 0;
                QVector<QStringList> per_byte(bm.size());
                for (int i = 0; i < bm.size(); ++i) {
                    quint8 byte = (quint8)bm[i];
                    for (int j = 0; j < 8; ++j) {
                        if (!(byte & (1u << j))) continue;
                        per_byte[i] << QStringLiteral("TEI%1").arg(8 * i + j);
                        ++nset;
                    }
                    if (!per_byte[i].isEmpty()) bm_any = true;
                }
                if (!bm_any) {
                    // 全 0(如 bitmap size=1 且 bitmap[0]=0)→ NULL
                    MsduFieldNode bl;
                    bl.name = QStringLiteral("DiscoverySTAList BitMap [%1b]")
                                  .arg(bm.size());
                    bl.value = QStringLiteral("NULL");
                    if (bm_base >= 0) { bl.rel_start = bm_base; bl.rel_len = bm.size(); }
                    root.children.append(bl);
                } else {
                    // 逐字节一行:DiscoverySTABitMap[i] [8b] = TEI0, TEI7 …
                    auto& bmg = group(root.children,
                                      QStringLiteral("DiscoverySTAList BitMap [%1b]")
                                          .arg(bm.size()));
                    for (int i = 0; i < bm.size(); ++i) {
                        if (per_byte[i].isEmpty()) continue;   // 空字节跳过
                        MsduFieldNode bl;
                        bl.name = QStringLiteral("DiscoverySTABitMap[%1] [8b]").arg(i);
                        bl.value = per_byte[i].join(QStringLiteral(", "));
                        if (bm_base >= 0) { bl.rel_start = bm_base + i; bl.rel_len = 1; }
                        bmg.children.append(bl);
                    }
                }

                // ---- 接收发现列表信息(表99:置位 TEI 各一条,1B 计数) ----
                QByteArray cnts;
                int cnts_base = -1;  // 计数区相对 body 起点(供高亮)
                if (node_num > 0 && off + node_num <= b.size()) {
                    cnts_base = head_size + 4 + off;
                    cnts = b.mid(off, node_num);
                }
                int order = 0;
                if (bm_any && nset > 0) {
                    auto& rgi = group(root.children,
                                      QStringLiteral("ReceivedDiscoveryInfo [%1]")
                                          .arg(nset));
                    for (int i = 0; i < bm.size(); ++i) {
                        quint8 byte = (quint8)bm[i];
                        for (int j = 0; j < 8; ++j) {
                            if (!(byte & (1u << j))) continue;
                            const int tei = 8 * i + j;
                            MsduFieldNode rc;
                            // 每条含义:数量 - TEx
                            rc.name = QStringLiteral("ReceivedDiscoverCount[%1]")
                                          .arg(order);
                            if (order < cnts.size()) {
                                rc.value = QStringLiteral("%1 - TEI%2")
                                               .arg((quint8)cnts[order]).arg(tei);
                                if (cnts_base >= 0) {
                                    rc.rel_start = cnts_base + order;
                                    rc.rel_len   = 1;
                                }
                            } else {
                                rc.value = QStringLiteral("? - TEI%1").arg(tei);
                            }
                            rgi.children.append(rc);
                            ++order;
                        }
                    }
                }
                break;
            }
            case MME_SUCCESS_RATE_REPORT: {
                add_fields(root.children, b, 0, kSuccessRateSpec, kSuccessRateSpecN, head_size + 4);
                apply_dicts(root.children);
                int sta_num = (int)get_bits(b, 2, 0, 16);
                int off = 4;
                if (sta_num > 0) {
                    auto& crg = group(root.children, QStringLiteral("CommRateInfoList [%1]").arg(sta_num));
                    for (int i = 0; i < sta_num && off + 4 <= b.size(); ++i) {
                        quint16 tei = (quint16)get_bits(b, off + 0, 0, 12);
                        quint8  rsv = (quint8)get_bits(b, off + 1, 4, 4);
                        quint8  down = (quint8)b[off + 2];
                        quint8  up   = (quint8)b[off + 3];
                        int abs0 = head_size + 4 + off;   // 条目相对 body 起点
                        off += 4;
                        // 对齐 Python log 权威形态:
                        //   MMe_CommRateInfo STATEI: 2 DownCommRate: 100% UpCommRate: 100%
                        auto& en = group(crg.children,
                                         QStringLiteral("STA[%1]").arg(tei));
                        MsduFieldNode st;
                        st.name  = QStringLiteral("STATEI [12b]");
                        st.value = QString::number(tei);
                        st.rel_start = abs0;
                        st.rel_len   = 2;      // bit0-11 跨 2B
                        en.children.append(st);
                        MsduFieldNode rv;
                        rv.name  = QStringLiteral("RSV0 [4b]");
                        rv.value = QString::number(rsv);
                        rv.rel_start = abs0 + 1;   // 条目 byte1 高 4bit
                        rv.rel_len   = 1;
                        en.children.append(rv);
                        MsduFieldNode dl;
                        dl.name  = QStringLiteral("DownCommRate");
                        dl.value = QStringLiteral("%1%").arg(down);
                        dl.rel_start = abs0 + 2;   // 条目 byte2:下行成功率
                        dl.rel_len   = 1;
                        en.children.append(dl);
                        MsduFieldNode ul;
                        ul.name  = QStringLiteral("UpCommRate");
                        ul.value = QStringLiteral("%1%").arg(up);
                        ul.rel_start = abs0 + 3;   // 条目 byte3:上行成功率
                        ul.rel_len   = 1;
                        en.children.append(ul);
                    }
                }
                break;
            }
            case MME_CHANGE_PROXY_CNF: {
                // MMeChangeProxyCnf(代理变更确认):固定 20B + 子站点 2B×ChildSum
                add_fields(root.children, b, 0, kChangeProxyCnfSpec,
                           kChangeProxyCnfSpecN, head_size + 4);
                apply_dicts(root.children);
                for (auto& ch : root.children) {
                    if (!ch.name.startsWith(QStringLiteral("Result"))) continue;
                    quint8 res = (quint8)get_bits(b, 0, 0, 8);
                    ch.value = QStringLiteral("%1 - %2").arg(res).arg(
                        res == 0 ? QStringLiteral("Success")
                                 : QStringLiteral("Fail"));
                }
                int child_sum = (int)get_bits(b, 16, 0, 16);
                int off = 20;   // 子站点表起点
                if (child_sum > 0 && off + 2 <= b.size()) {
                    auto& cg = group(root.children,
                                     QStringLiteral("ProxyChildSTA [%1]").arg(child_sum));
                    for (int i = 0; i < child_sum && off + 2 <= b.size(); ++i) {
                        quint16 e = (quint16)get_bits(b, off, 0, 16);
                        MsduFieldNode cn;
                        cn.name = QStringLiteral("Child[%1]").arg(i);
                        cn.value = QStringLiteral("TEI=%1 LinkType=%2")
                                       .arg(e & 0x0FFF).arg((e >> 12) & 0x01);
                        cn.rel_start = head_size + 4 + off;
                        cn.rel_len   = 2;
                        cg.children.append(cn);
                        off += 2;
                    }
                }
                break;
            }
            case MME_LEAVE_IND: {
                // MMeLeaveInd(离线指示):固定 16B + 离线站点 MAC 6B×N
                add_fields(root.children, b, 0, kLeaveIndSpec, kLeaveIndSpecN,
                           head_size + 4);
                for (auto& ch : root.children) {
                    if (!ch.name.startsWith(QStringLiteral("Reason"))) continue;
                    quint16 r = (quint16)get_bits(b, 0, 0, 16);
                    static const char* rsn[] = {
                        "CCO decides STA offline",
                        "Network layers exceeded upper limit",
                        "STA not in whitelist"};
                    ch.value = (r <= 2)
                        ? QStringLiteral("%1 - %2").arg(r).arg(QLatin1String(rsn[r]))
                        : QString::number(r);
                }
                int n = (int)get_bits(b, 2, 0, 16);
                int off = 16;
                if (n > 0 && off + 6 <= b.size()) {
                    auto& lg = group(root.children,
                                     QStringLiteral("LeaveSTAMACList [%1]").arg(n));
                    for (int i = 0; i < n && off + 6 <= b.size(); ++i) {
                        MsduFieldNode mac;
                        mac.name  = QStringLiteral("LeaveSTAMAC [48b]");
                        mac.value = mac_str((quint64)get_bits(b, off, 0, 48));
                        mac.rel_start = head_size + 4 + off;
                        mac.rel_len   = 6;
                        lg.children.append(mac);
                        off += 6;
                    }
                }
                break;
            }
            case MME_NETWORK_CONFLICT_REPORT: {
                // MMeNetworkConflictReport(网络冲突上报):固定 8B + 邻居网络号
                // NIDSize(规范=3)×NeighbourNetWorkCount
                add_fields(root.children, b, 0, kNetworkConflictSpec,
                           kNetworkConflictSpecN, head_size + 4);
                int cnt = (int)get_bits(b, 6, 0, 8);
                int nid_sz = (int)get_bits(b, 7, 0, 8);
                int off = 8;
                if (nid_sz > 0 && cnt > 0 && off + nid_sz <= b.size()) {
                    auto& ng = group(root.children,
                                     QStringLiteral("NeighbourNetworkNIDList [%1]")
                                         .arg(cnt));
                    for (int i = 0; i < cnt && off + nid_sz <= b.size(); ++i) {
                        quint32 nid = (quint32)get_bits(b, off, 0, nid_sz * 8);
                        MsduFieldNode nd;
                        nd.name  = QStringLiteral("NeighbourNID[%1]").arg(i);
                        nd.value = QStringLiteral("0x%1")
                                       .arg(nid, nid_sz * 2, 16, QChar('0'));
                        nd.rel_start = head_size + 4 + off;
                        nd.rel_len   = nid_sz;
                        ng.children.append(nd);
                        off += nid_sz;
                    }
                }
                break;
            }
            case MME_RF_CHANNEL_CONFLICT_REPORT: {
                // MMeRFChannelConflictReport(无线信道冲突上报):CCO MAC + 邻居数;
                // 随后 Count 字节信道 + Count 字节 option(每邻居一对)
                add_fields(root.children, b, 0, kRFChannelConflictSpec,
                           kRFChannelConflictSpecN, head_size + 4);
                int cnt = (int)get_bits(b, 6, 0, 8);
                int ch_off = 7;
                if (cnt > 0 && ch_off + cnt * 2 <= b.size()) {
                    auto& rg = group(root.children,
                                     QStringLiteral("NeighbourRFNetworkList [%1]")
                                         .arg(cnt));
                    for (int i = 0; i < cnt; ++i) {
                        quint8 ch = (quint8)b[ch_off + i];
                        quint8 op = (quint8)b[ch_off + cnt + i];
                        MsduFieldNode nd;
                        nd.name  = QStringLiteral("Neighbour[%1]").arg(i);
                        nd.value = QStringLiteral("ch=%1 op=%2").arg(ch).arg(op);
                        nd.rel_start = head_size + 4 + ch_off + i;
                        nd.rel_len   = 1;
                        rg.children.append(nd);
                    }
                }
                break;
            }
            case MME_ROUTE_REQUEST: {
                // MMeRouteRequest(路由请求):头 7B + LoadData(4B×N RoutePath)
                add_fields(root.children, b, 0, kRouteReqReplyHeadSpec,
                           kRouteReqReplyHeadSpecN, head_size + 4);
                add_fields(root.children, b, 0, kRouteRequestTailSpec,
                           kRouteRequestTailSpecN, head_size + 4);
                apply_dicts(root.children);
                annotate_route_loaddata(root.children, b, head_size + 4);
                break;
            }
            case MME_ROUTE_REPLY: {
                // MMeRouteReply(路由回复):头 7B + LoadData(4B×N RoutePath)
                add_fields(root.children, b, 0, kRouteReqReplyHeadSpec,
                           kRouteReqReplyHeadSpecN, head_size + 4);
                add_fields(root.children, b, 0, kRouteReplyTailSpec,
                           kRouteReplyTailSpecN, head_size + 4);
                apply_dicts(root.children);
                annotate_route_loaddata(root.children, b, head_size + 4);
                break;
            }
            case MME_ROUTE_ERROR: {
                // MMeRouteError(路由错误):固定 7B + 不可达站点 2B×N
                add_fields(root.children, b, 0, kRouteErrorSpec,
                           kRouteErrorSpecN, head_size + 4);
                int n = (int)get_bits(b, 6, 0, 8);
                int off = 7;
                if (n > 0 && off + 2 <= b.size()) {
                    auto& ug = group(root.children,
                                     QStringLiteral("UnReachableSTAList [%1]").arg(n));
                    for (int i = 0; i < n && off + 2 <= b.size(); ++i) {
                        quint16 e = (quint16)get_bits(b, off, 0, 16);
                        MsduFieldNode sn;
                        sn.name  = QStringLiteral("UnReachableSTA[%1]").arg(i);
                        sn.value = QStringLiteral("TEI=%1 LinkType=%2")
                                       .arg(e & 0x0FFF).arg((e >> 12) & 0x01);
                        sn.rel_start = head_size + 4 + off;
                        sn.rel_len   = 2;
                        ug.children.append(sn);
                        off += 2;
                    }
                }
                break;
            }
            case MME_ROUTE_ACK: {
                // MMeRouteAck(路由应答):固定 8B
                add_fields(root.children, b, 0, kRouteAckSpec, kRouteAckSpecN,
                           head_size + 4);
                break;
            }
            case MME_LINK_CONFIRM_REQUEST: {
                // MMeLinkConfirmRequest(链路确认请求):固定 7B + 应答站点 2B×N
                add_fields(root.children, b, 0, kLinkConfirmRequestSpec,
                           kLinkConfirmRequestSpecN, head_size + 4);
                int n = (int)get_bits(b, 6, 0, 8);
                int off = 7;
                if (n > 0 && off + 2 <= b.size()) {
                    auto& ag = group(root.children,
                                     QStringLiteral("AckSTAList [%1]").arg(n));
                    for (int i = 0; i < n && off + 2 <= b.size(); ++i) {
                        quint16 e = (quint16)get_bits(b, off, 0, 16);
                        MsduFieldNode sn;
                        sn.name  = QStringLiteral("AckSTA[%1]").arg(i);
                        sn.value = QStringLiteral("TEI=%1 LinkType=%2")
                                       .arg(e & 0x0FFF).arg((e >> 12) & 0x01);
                        sn.rel_start = head_size + 4 + off;
                        sn.rel_len   = 2;
                        ag.children.append(sn);
                        off += 2;
                    }
                }
                break;
            }
            case MME_LINK_CONFIRM_RESPONSE: {
                // MMeLinkConfirmResponse(链路确认回应):固定 8B
                add_fields(root.children, b, 0, kLinkConfirmResponseSpec,
                           kLinkConfirmResponseSpecN, head_size + 4);
                for (auto& ch : root.children) {
                    if (!ch.name.startsWith(QStringLiteral("RoutePriorityFlag")))
                        continue;
                    quint8 f = (quint8)get_bits(b, 3, 0, 1);
                    ch.value = QStringLiteral("%1 - %2").arg(f).arg(
                        f == 1 ? QStringLiteral("Have high priority")
                               : QStringLiteral("Normal priority"));
                }
                break;
            }
            case MME_DIAGNOSE: {
                // MMeDiagnose(网络诊断):芯片厂商 ID(1-HS 2-ES 3-TC 4-LH
                // 5-HT 6-RS 7-SW 8-SC,0 与其它保留)
                add_fields(root.children, b, 0, kDiagnoseSpec, kDiagnoseSpecN,
                           head_size + 4);
                for (auto& ch : root.children) {
                    if (!ch.name.startsWith(QStringLiteral("ChipManufacID")))
                        continue;
                    quint16 v = (quint16)get_bits(b, 0, 0, 16);
                    static const char* mf[] = {
                        "Reserved", "HS", "ES", "TC", "LH", "HT", "RS", "SW", "SC"};
                    ch.value = (v <= 8)
                        ? QStringLiteral("%1 - %2").arg(v).arg(QLatin1String(mf[v]))
                        : QString::number(v);
                }
                break;
            }
            case MME_ZERO_CROSS_NTB_COLLECT_IND: {
                // MMeZeroCrossNTBCollectInd(过零NTB采集指示):固定 8B
                add_fields(root.children, b, 0, kZeroCrossCollectSpec,
                           kZeroCrossCollectSpecN, head_size + 4);
                for (auto& ch : root.children) {
                    if (ch.name.startsWith(QStringLiteral("NTBCollectionMode"))) {
                        quint8 m = (quint8)get_bits(b, 2, 0, 8);
                        ch.value = QStringLiteral("%1 - %2").arg(m).arg(
                            m == 1 ? QStringLiteral("All STAs collect zero-cross NTB")
                                   : QStringLiteral("Appointed STA (STATEI) collects"));
                    } else if (ch.name.startsWith(
                                   QStringLiteral("NTBCollectionPeriod"))) {
                        quint8 p = (quint8)get_bits(b, 3, 0, 8);
                        ch.value = QStringLiteral("%1 - %2").arg(p).arg(
                            p == 1 ? QStringLiteral("One power line cycle")
                                   : QStringLiteral("Half power line cycle"));
                    }
                }
                break;
            }
            case MME_ZERO_CROSS_NTB_REPORT: {
                // MMeZeroCrossNTBReport(过零NTB上报):固定 10B + 差分 NTB
                // (12bit 打包:偶序号 = b0|(b1&0F)<<8 吃1B;奇序号=(b0>>4)|(b1<<4)
                // 吃2B;LineA/B/C 依次各消费各自计数)
                add_fields(root.children, b, 0, kZeroCrossReportSpec,
                           kZeroCrossReportSpecN, head_size + 4);
                const int la = (int)get_bits(b, 3, 0, 8);
                const int lb = (int)get_bits(b, 4, 0, 8);
                const int lc = (int)get_bits(b, 5, 0, 8);
                const int total = (int)get_bits(b, 2, 0, 8);
                int la_show = (la == total && total > 0) ? la - 1 : la;  // 与 Python 一致
                QByteArray tab = b.mid(10);
                int cur = 0;
                auto read12 = [&tab, &cur]() -> quint16 {
                    if (cur >= tab.size() || tab.size() < 2) return 0;
                    quint16 v;
                    if ((cur & 1) == 0) {
                        v = (quint16)(quint8)tab[cur]
                          | (quint16)(((quint8)tab[cur + 1] & 0x0F) << 8);
                        cur += 1;
                    } else {
                        v = (quint16)((quint8)tab[cur] >> 4)
                          | (quint16)((quint8)tab[cur + 1] << 4);
                        cur += 2;
                    }
                    return v;
                };
                auto diff_group = [&](QVector<MsduFieldNode>& out, const char* nm,
                                      int cnt, const QByteArray& src, int rel_off) {
                    if (cnt <= 0) return;
                    auto& g = group(out, QStringLiteral("%1 Diff NTB List [%2]")
                                             .arg(QLatin1String(nm)).arg(cnt));
                    for (int i = 0; i < cnt; ++i) {
                        MsduFieldNode d;
                        d.name  = QStringLiteral("DiffNTB[%1]").arg(i);
                        d.value = QString::number(read12());
                        g.children.append(d);
                    }
                    Q_UNUSED(src); Q_UNUSED(rel_off);
                };
                diff_group(root.children, "LineA", la_show, tab, 10);
                diff_group(root.children, "LineB", lb, tab, 10);
                diff_group(root.children, "LineC", lc, tab, 10);
                break;
            }
            default: {
                MsduFieldNode un;
                un.name = trl::L("(未实现子类型)");
                un.value = QString(b.toHex(' '));
                root.children.append(un);
                break;
            }
        }
    } else if (msdu_type == 48 || msdu_type == 49) {
        // ---- 应用层报文:APP_BASE(4B) ----
        quint16 packet_id = (quint16)get_bits(msdu_body, 1, 0, 16);
        out.summary = QStringLiteral("APP %1 (0x%2)")
                          .arg(packet_id_name(packet_id))
                          .arg(packet_id, 4, 16, QChar('0'));
        auto& root = group(out.tree, QStringLiteral("APP"));
        add_fields(root.children, msdu_body, 0, kAppBaseSpec, kAppBaseSpecN, head_size);
        // PacketID / PortNum 字段行加注释(表 2 报文 ID 含义、报文端口号)
        for (auto& c : root.children) {
            if (c.name.startsWith(QStringLiteral("PacketID")))
                c.value += QStringLiteral(" - %1").arg(packet_id_name(packet_id));
            else if (c.name.startsWith(QStringLiteral("PortNum"))) {
                QString pn = app_port_name((quint8)get_bits(msdu_body, 0, 0, 8));
                if (!pn.isEmpty())
                    c.value += QStringLiteral(" - %1").arg(pn);
            }
        }
        {
            // APP 载荷统一 hex 原文(0x0008 事件上报保留专属名,便于日后专用解析)
            MsduFieldNode raw;
            raw.name = (packet_id == 0x0008)
                ? QStringLiteral("EventPacket Payload")
                : QStringLiteral("Payload");
            raw.value = QString(msdu_body.mid(4).toHex(' '));
            raw.rel_start = head_size + 4;   // msdu_body 在 body 起点 head_size + APP 头 4B
            raw.rel_len   = msdu_body.size() - 4;
            root.children.append(raw);
        }
    } else {
        out.summary = QStringLiteral("MSDUType %1").arg(msdu_type);
    }

    // ---- MSDU 帧尾 4B CRC32 ----
    // 位置:MSDU 数据区(MSDULen)之后紧接 4B;计算范围 = MSDU 数据区 msdu_len 字节
    // (与 Python MSDU_BASE 校验一致:cal_crc32(MSDU_Body, MSDULen+4),
    //  即遍历 MSDULen 字节, poly=0xEDB88320, init=0xFFFFFFFF, 末取反)
    if (msdu_len > 0) {
        const int crc_off = head_size + msdu_len;
        // MSDU 帧总长 = 头 + 数据区 + CRC 4B(不含 PB 填充);供整体高亮
        out.total_len = crc_off + 4;
        if (crc_off + 4 <= body.size()) {
            quint32 stored = (quint8)body[crc_off]
                | ((quint32)(quint8)body[crc_off + 1] << 8)
                | ((quint32)(quint8)body[crc_off + 2] << 16)
                | ((quint32)(quint8)body[crc_off + 3] << 24);
            quint32 calc = crc32_le(
                reinterpret_cast<const quint8*>(msdu_body.constData()),
                msdu_len + 4);
            MsduFieldNode crc;
            crc.name = QStringLiteral("MSDU CRC32");
            crc.value = QStringLiteral("0x%1 %2")
                .arg(stored, 8, 16, QChar('0'))
                .arg(stored == calc ? QStringLiteral("OK")
                                    : QStringLiteral("FAIL"));
            crc.rel_start = crc_off;
            crc.rel_len   = 4;
            out.tree.append(crc);
        }
    }
    return out;
}

// ===== i18n:文件级 中→英 显示词典(仅显示翻译;未命中回退中文) =====
namespace {

struct I18nReg {
    I18nReg() {
        trl::register_en("终端主动抄表", "Terminal meter reading");
        trl::register_en("路由主动抄表", "Router meter reading");
        trl::register_en("终端主动并发抄表", "Terminal concurrent meter reading");
        trl::register_en("校时", "Time sync");
        trl::register_en("站点版本信息", "STA Version Info");
        trl::register_en("错误路由类型", "Incorrect Route");
        trl::register_en("同级路由类型", "Same-level route");
        trl::register_en("上级路由类型", "Upper-level route");
        trl::register_en("代理主路径路由类型", "Proxy main-path route");
        trl::register_en("上上级路由类型", "Upper of upper-level route");
        trl::register_en("站点动态选择的代理", "Proxy chosen by the STA");
        trl::register_en("通信测试", "Comm test");
        trl::register_en("事件上报", "Event report");
        trl::register_en("查询从节点主动注册", "Query slave node registration");
        trl::register_en("启动从节点主动注册", "Start slave node registration");
        trl::register_en("停止从节点主动注册", "Stop slave node registration");
        trl::register_en("确认/否认", "Confirm / Deny");
        trl::register_en("开始升级", "Start upgrade");
        trl::register_en("停止升级", "Stop upgrade");
        trl::register_en("传输文件数据", "Transfer file data");
        trl::register_en("传输文件数据(单播转本地广播)", "Transfer file data (unicast to local broadcast)");
        trl::register_en("查询站点升级状态", "Query node upgrade status");
        trl::register_en("执行升级", "Execute upgrade");
        trl::register_en("查询站点信息", "Query node info");
        trl::register_en("抄控器 CCO", "Meter-reading controller CCO");
        trl::register_en("抄控器数据透传串口转发", "Meter-reading controller transparent serial forwarding");
        trl::register_en("鉴权安全", "Authentication security");
        trl::register_en("台区户变关系识别", "Transformer-area relation detection");
        trl::register_en("查询ID信息", "Query ID info");
        trl::register_en("精准校时", "Precise time sync");
        trl::register_en("配电信息上报", "Distribution info report");
        trl::register_en("存储采集扩展配置", "Storage collection extended configuration");
        trl::register_en("存储数据广播时规", "Storage data broadcast schedule");
        trl::register_en("存储数据同步配置", "Storage data sync configuration");
        trl::register_en("认证", "Authentication");
        trl::register_en("存储 HRF 中继心跳", "Store HRF relay heartbeat");
        trl::register_en("(未实现)", "(Not implemented)");
        trl::register_en("管理/抄表端口", "Management / meter-reading port");
        trl::register_en("升级端口", "Upgrade port");
        trl::register_en("安全端口", "Security port");
        trl::register_en("(未实现子类型)", "(Not implemented subtype)");
    }
};
const I18nReg g_i18n_reg_msdu;

}  // namespace
