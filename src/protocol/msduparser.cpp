/// @file msduparser.cpp
/// @brief MSDU 解析器实现
/// @details 字段位域坐标与 Python MSDU_Class.py 的 BitDefine 定义一一对应。
///          解析 = 公共头(MSDU_BASE/MSDU_BASE_S) + 类型分支(MMe / APP)。
#include "msduparser.h"
#include "beaconparser.h"
#include <QtEndian>
#include <cstdint>

// ---------- 工具:取位域(与 bplcparser 同算法,本地独立实现) ----------
namespace {

quint64 get_bits(const quint8* d, int start_byte, int start_bit, int bit_len) {
    int bits_occupied = bit_len + start_bit;
    int bytes_floor = bits_occupied / 8;
    int bytes_occupied = bytes_floor + ((bits_occupied % 8) != 0 ? 1 : 0);
    quint64 result = 0;
    for (int i = 0; i < bytes_occupied; ++i) {
        quint8 b = d[start_byte + i];
        if (i == 0) b = (quint8)((b >> start_bit) << start_bit);
        if (i == bytes_occupied - 1) {
            int keep = bits_occupied - (bytes_occupied - 1) * 8;
            if (keep < 8) b = (quint8)(b & ((1u << keep) - 1u));
        }
        result |= (quint64(b) << (8 * i));
    }
    return result >> start_bit;
}

quint64 get_bits(const QByteArray& d, int start_byte, int start_bit, int bit_len) {
    return get_bits(reinterpret_cast<const quint8*>(d.constData()), start_byte, start_bit, bit_len);
}

QString hex6(quint64 v) { return QString("0x%1").arg(v, 6, 16, QChar('0')); }
QString hex8(quint64 v) { return QString("0x%1").arg(v, 8, 16, QChar('0')); }
QString hex12(quint64 v) { return QString("0x%1").arg(v, 12, 16, QChar('0')); }
QString hex4(quint64 v) { return QString("0x%1").arg(v, 4, 16, QChar('0')); }

QString mac_str(quint64 v) {
    // 48-bit MAC:Python big_small_end_convert 语义 = 帧内原始字节序
    // (BitDefine 小端读出后转回大端字节串再反转 = 数据顺序)
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QString("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += ':';
    }
    return s;
}

/// 一个位域字段说明:名 / 相对字节 / 位偏移 / 位长 / 格式化
enum class Fmt { DEC, HEX6, HEX8, HEX12, HEX4, MAC, BOOL_Y };

struct FieldSpec {
    const char* name;
    int  byte;
    int  bit;
    int  len;
    Fmt  fmt;
};

QString fmt_val(Fmt f, quint64 v) {
    switch (f) {
        case Fmt::DEC:   return QString::number(v);
        case Fmt::HEX6:  return hex6(v);
        case Fmt::HEX8:  return hex8(v);
        case Fmt::HEX12: return hex12(v);
        case Fmt::HEX4:  return hex4(v);
        case Fmt::MAC:   return mac_str(v);
        case Fmt::BOOL_Y:return v ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return QString::number(v);
}

void add_fields(QVector<MsduFieldNode>& out, const QByteArray& d, int base,
                const FieldSpec* specs, int n, int rel_base = 0) {
    for (int i = 0; i < n; ++i) {
        const FieldSpec& s = specs[i];
        quint64 v = get_bits(d, base + s.byte, s.bit, s.len);
        MsduFieldNode node;
        node.name = QStringLiteral("%1 [%2b]").arg(QLatin1String(s.name)).arg(s.len);
        node.value = fmt_val(s.fmt, v);
        // 位域覆盖的字节区间(相对 msdu_body 起点;供 UI 高亮换算)
        int first_bit = (rel_base + base + s.byte) * 8 + s.bit;
        int last_bit  = first_bit + s.len - 1;
        node.rel_start = first_bit / 8;
        node.rel_len   = last_bit / 8 - node.rel_start + 1;
        out.append(node);
    }
}

/// 追加一个分组节点并返回其子容器引用
MsduFieldNode& group(QVector<MsduFieldNode>& out, const QString& name, const QString& val = QString()) {
    MsduFieldNode g;
    g.name = name;
    g.value = val;
    out.append(g);
    return out.last();
}

}  // namespace

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
        case 0x00: return QStringLiteral("MMeAssocReq");
        case 0x01: return QStringLiteral("MMeAssocCnf");
        case 0x02: return QStringLiteral("MMeAssocGatherInd");
        case 0x03: return QStringLiteral("MMeChangeProxyReq");
        case 0x04: return QStringLiteral("MMeChangeProxyCnf");
        case 0x05: return QStringLiteral("MMeChangeProxyBitMapCnf");
        case 0x06: return QStringLiteral("MMeLeaveInd");
        case 0x07: return QStringLiteral("MMeHeartBeatCheck");
        case 0x08: return QStringLiteral("MMeDiscoveryNodeList");
        case 0x09: return QStringLiteral("MMeSuccessRateReport");
        case 0x0a: return QStringLiteral("MMeNetworkConflictReport");
        case 0x0b: return QStringLiteral("MMeZeroCrossNTBCollectInd");
        case 0x0c: return QStringLiteral("MMeZeroCrossNTBReport");
        case 0x4f: return QStringLiteral("MMeDiagnose");
        case 0x50: return QStringLiteral("MMeRouteRequest");
        case 0x51: return QStringLiteral("MMeRouteReply");
        case 0x52: return QStringLiteral("MMeRouteError");
        case 0x53: return QStringLiteral("MMeRouteAck");
        case 0x54: return QStringLiteral("MMeLinkConfirmRequest");
        case 0x55: return QStringLiteral("MMeLinkConfirmResponse");
        case 0x80: return QStringLiteral("MMeRFChannelConflictReport");
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
        case 0x0001: return QStringLiteral("终端主动抄表");
        case 0x0002: return QStringLiteral("路由主动抄表");
        case 0x0003:
        case 0x00B3: return QStringLiteral("终端主动并发抄表");
        case 0x0004: return QStringLiteral("校时");
        case 0x0006: return QStringLiteral("通信测试");
        case 0x0008: return QStringLiteral("事件上报");
        case 0x0011: return QStringLiteral("查询从节点主动注册");
        case 0x0012: return QStringLiteral("启动从节点主动注册");
        case 0x0013: return QStringLiteral("停止从节点主动注册");
        case 0x0020: return QStringLiteral("确认/否认");
        case 0x0030: return QStringLiteral("开始升级");
        case 0x0031: return QStringLiteral("停止升级");
        case 0x0032: return QStringLiteral("传输文件数据");
        case 0x0033: return QStringLiteral("传输文件数据(单播转本地广播)");
        case 0x0034: return QStringLiteral("查询站点升级状态");
        case 0x0035: return QStringLiteral("执行升级");
        case 0x0036: return QStringLiteral("查询站点信息");
        case 0x0040: return QStringLiteral("抄控器 CCO");
        case 0x0041: return QStringLiteral("抄控器数据透传串口转发");
        case 0x00A0: return QStringLiteral("鉴权安全");
        case 0x00A1: return QStringLiteral("台区户变关系识别");
        case 0x00A2: return QStringLiteral("查询ID信息");
        case 0x00A3: return QStringLiteral("精准校时");
        case 0x00A4: return QStringLiteral("配电信息上报");
        // 以下为 Python 已实现但"表 2"未单列的应用报文(扩展补充)
        case 0x00B0: return QStringLiteral("存储采集扩展配置");
        case 0x00B1: return QStringLiteral("存储数据广播时规");
        case 0x00B2: return QStringLiteral("存储数据同步配置");
        case 0x00C1: return QStringLiteral("认证");
        case 0x00CC: return QStringLiteral("存储 HRF 中继心跳");
        default:     return QStringLiteral("(未实现)");
    }
}

/// APP PortNum → 端口注释(表 2 报文端口号列)
static QString app_port_name(quint8 port) {
    switch (port) {
        case 0x11: return QStringLiteral("管理/抄表端口");
        case 0x12: return QStringLiteral("升级端口");
        case 0x1A: return QStringLiteral("安全端口");
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

/// 字段单位注释:命中 name 的节点 value 追加单位后缀(如 % / ms / s)
/// starts=true 时按前缀匹配;false 时按整名精确匹配
static void annotate_unit(QVector<MsduFieldNode>& nodes, const char* field,
                          const QString& unit, bool starts = true) {
    for (auto& n : nodes) {
        if (starts ? n.name.startsWith(QLatin1String(field))
                   : (n.name == QLatin1String(field)))
            n.value += unit;
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
    {"BootReason",        42, 0, 8,  Fmt::DEC},
    {"BootVersion",       43, 0, 8,  Fmt::DEC},
    {"SoftwareVersion",   44, 0, 16, Fmt::DEC},
    {"VersionDataYear",   46, 0, 7,  Fmt::DEC},
    {"VersionDataMonth",  46, 7, 4,  Fmt::DEC},
    {"VersionDataDay",    47, 3, 5,  Fmt::DEC},
    {"ManufacturerID",    48, 0, 16, Fmt::HEX4},
    {"ChipID",            50, 0, 16, Fmt::HEX4},
    {"HardRstCount",      52, 0, 16, Fmt::DEC},
    {"SoftRstCount",      54, 0, 16, Fmt::DEC},
    {"ProxyType",         56, 0, 8,  Fmt::DEC},
    {"RSV7",              57, 0, 24, Fmt::DEC},
    {"EndSequence",       60, 0, 32, Fmt::HEX8},
};
static const int kAssocReqSpecN = int(sizeof(kAssocReqSpec) / sizeof(kAssocReqSpec[0]));

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
// beacon_crc32 定义于本文件后部(BEACON 载荷区),此处前置声明供 MSDU CRC 校验复用
static quint32 beacon_crc32(const quint8* d, int len);

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
    quint8 msdu_type   = (quint8)get_bits(p, 7, 0, 8);
    int    msdu_len    = (int)get_bits(p, 8, 0, 11);
    bool   mac_flag    = get_bits(p, 11, 3, 1) != 0;
    int    head_size   = mac_flag ? 28 : 16;

    if (mac_flag && body.size() >= 28) {
        add_fields(out.tree, body, 0, kMsduMacSpec, 2);
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
            case 0x00: {
                // MMeAssocReq(关联请求)
                add_fields(root.children, b, 0, kAssocReqSpec, kAssocReqSpecN, head_size + 4);
                apply_dicts(root.children);
                MsduFieldNode mn;
                mn.name  = QStringLiteral("ManufacturerInfo [144b]");
                mn.value = bytes_hex(b, 24, 18);
                root.children.append(mn);
                MsduFieldNode mid;
                mid.name  = QStringLiteral("ManagementID [192b]");
                mid.value = bytes_hex(b, 64, 24);
                root.children.append(mid);
                break;
            }
            case 0x01: {
                // MMeAssocCnf(关联确认):固定头到 b[40],RouteInfo 从 b[40] 起
                add_fields(root.children, b, 0, kAssocCnfSpec, kAssocCnfSpecN, head_size + 4);
                apply_dicts(root.children);
                annotate_unit(root.children, "STAReAssocTime", QStringLiteral("ms"));
                // 修正 AssocResult 可读文本
                for (auto& ch : root.children) {
                    if (ch.name.startsWith(QStringLiteral("AssocResult")))
                        ch.value = assoc_result_str((quint8)get_bits(b, 12, 0, 8));
                }
                int off = 40;  // RouteInfo 起点(MMeHeadSize 4 + 固定 36)
                if (b.size() >= off + 8) {
                    quint16 sta_sum   = (quint16)get_bits(b, off + 0, 0, 16);
                    quint16 pco_sum   = (quint16)get_bits(b, off + 2, 0, 16);
                    quint16 route_sz  = (quint16)get_bits(b, off + 4, 0, 16);
                    off += 8;
                    MsduFieldNode rt;
                    rt.name = QStringLiteral("RouteInfo [%1B]").arg(route_sz);
                    rt.value = QStringLiteral("STA=%1 PCO=%2")
                                   .arg(sta_sum).arg(pco_sum);
                    root.children.append(rt);
                    auto& route = root.children.last();
                    // Straight STA 表:2B/条
                    for (int i = 0; i < sta_sum && off + 2 <= b.size(); ++i) {
                        quint16 e = (quint16)get_bits(b, off, 0, 16);
                        off += 2;
                        MsduFieldNode sn;
                        sn.name = QStringLiteral("STA[%1]").arg(i);
                        sn.value = QStringLiteral("TEI=%1 LinkType=%2")
                                       .arg(e & 0x0FFF).arg((e >> 12) & 0x01);
                        route.children.append(sn);
                    }
                    // Straight PCO 表:4B + 2B×ChildSum /条
                    for (int i = 0; i < pco_sum && off + 4 <= b.size(); ++i) {
                        quint16 tei     = (quint16)get_bits(b, off + 0, 0, 12);
                        quint16 child   = (quint16)get_bits(b, off + 2, 0, 16);
                        off += 4;
                        MsduFieldNode pn;
                        pn.name = QStringLiteral("PCO[%1]").arg(tei);
                        pn.value = QStringLiteral("childSum=%1").arg(child);
                        route.children.append(pn);
                        for (int j = 0; j < child && off + 2 <= b.size(); ++j) {
                            quint16 ctei = (quint16)get_bits(b, off, 0, 16);
                            off += 2;
                            MsduFieldNode cn;
                            cn.name = QStringLiteral("Child[%1]").arg(j);
                            cn.value = QStringLiteral("TEI=%1 LinkType=%2")
                                           .arg(ctei & 0x0FFF).arg((ctei >> 12) & 0x01);
                            pn.children.append(cn);
                        }
                    }
                }
                break;
            }
            case 0x03:
                add_fields(root.children, b, 0, kChangeProxyReqSpec, kChangeProxyReqSpecN, head_size + 4);
                apply_dicts(root.children);
                break;
            case 0x05: {
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
            case 0x07: {
                // MMeHeartBeatCheck:bitmap → TEI 列表
                add_fields(root.children, b, 0, kHeartBeatSpec, kHeartBeatSpecN, head_size + 4);
                apply_dicts(root.children);
                int bm_size = (int)get_bits(b, 6, 0, 16);
                if (bm_size > 0 && b.size() >= 8 + bm_size) {
                    QByteArray bm = b.mid(8, bm_size);
                    QString teis;
                    for (int i = 0; i < bm.size(); ++i) {
                        quint8 byte = (quint8)bm[i];
                        for (int j = 0; j < 8; ++j) {
                            if (byte & (1u << j)) teis += QString("%1, ").arg(8 * i + j);
                        }
                    }
                    auto& hlg = group(root.children, QStringLiteral("DiscoverSTAList"));
                    MsduFieldNode dl;
                    dl.name  = QStringLiteral("DiscoverSTATEI");
                    dl.value = teis.isEmpty() ? QStringLiteral("(none)") : teis;
                    hlg.children.append(dl);
                }
                break;
            }
            case 0x08: {
                // MMeDiscoverNodeList
                add_fields(root.children, b, 0, kDiscoverNodeListSpec, kDiscoverNodeListSpecN, head_size + 4);
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
                        static const char* rtd[] = {
                            "Incorrect Route",
                            "The Backup Route of the same level",
                            "Upper-level Backup route",
                            "route of the proxy main path",
                            "Upper of the upper-level Backup route"};
                        rt.value = (rtype <= 4)
                            ? QStringLiteral("%1 - %2").arg(rtype)
                                                      .arg(QLatin1String(rtd[rtype]))
                            : QString::number(rtype);
                        rt.rel_start = abs0 + 1;  // byte1 高 4bit
                        rt.rel_len   = 1;
                        ug.children.append(rt);
                    }
                }
                // Discovery STAList BitMap:逐 bit → STA TEI
                QByteArray bm;
                int bm_base = -1;   // bitmap 相对 body 起点(供高亮)
                if (bitmap_size > 0 && off + bitmap_size <= b.size()) {
                    bm_base = head_size + 4 + off;
                    bm = b.mid(off, bitmap_size);
                    off += bitmap_size;
                }
                // ReceivedDiscoveryInfo:DiscoverNodeNum 字节(按 bitmap 置位顺序配对)
                QByteArray cnts;
                int cnts_base = -1;  // 计数区相对 body 起点(供高亮)
                if (node_num > 0 && off + node_num <= b.size()) {
                    cnts_base = head_size + 4 + off;
                    cnts = b.mid(off, node_num);
                }
                // 逐置位 bit:先展示位图中的 TEI,再展示该 TEI 的发现报文数量
                // 与 Python log 权威一致:
                //   MMeDiscoverNodeList DiscoveredSTATEI: 1;  ReceivedDiscoverCount: 57
                int order = 0;
                MsduFieldNode* dng = nullptr;
                for (int i = 0; i < bm.size(); ++i) {
                    quint8 byte = (quint8)bm[i];
                    for (int j = 0; j < 8; ++j) {
                        if (!(byte & (1u << j))) continue;
                        if (!dng)
                            dng = &group(root.children,
                                QStringLiteral("DiscoveredNodeList [%1]").arg(node_num));
                        auto& dn = group(dng->children,
                                         QStringLiteral("Discovered[%1]").arg(order));
                        // TEI 由位图 bit 位置决定:TEI = 8*i+j
                        MsduFieldNode st;
                        st.name  = QStringLiteral("STATEI [1b]");
                        st.value = QString::number(8 * i + j);
                        if (bm_base >= 0) {
                            st.rel_start = bm_base + i;   // 位图该 bit 所在字节
                            st.rel_len   = 1;
                        }
                        dn.children.append(st);
                        // 对应发现报文数量(置位顺序取 cnts)
                        MsduFieldNode ct;
                        ct.name = QStringLiteral("ReceivedDiscoverCount [8b]");
                        if (order < cnts.size()) {
                            ct.value = QString::number((quint8)cnts[order]);
                            if (cnts_base >= 0) {
                                ct.rel_start = cnts_base + order;
                                ct.rel_len   = 1;
                            }
                        } else {
                            ct.value = QStringLiteral("?");
                        }
                        dn.children.append(ct);
                        ++order;
                    }
                }
                break;
            }
            case 0x09: {
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
                        en.children.append(dl);
                        MsduFieldNode ul;
                        ul.name  = QStringLiteral("UpCommRate");
                        ul.value = QStringLiteral("%1%").arg(up);
                        en.children.append(ul);
                    }
                }
                break;
            }
            default: {
                MsduFieldNode un;
                un.name = QStringLiteral("(未实现子类型)");
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
        if (packet_id == 0x0008) {
            // APP_EventPacket 事件上报:直接给报文载荷原文(协议未细分公开字段)
            QByteArray d = msdu_body.mid(4);
            MsduFieldNode raw;
            raw.name  = QStringLiteral("EventPacket Payload");
            raw.value = QString(d.toHex(' '));
            raw.rel_start = head_size + 4;   // msdu_body 在 body 起点 head_size + APP 头 4B
            raw.rel_len   = d.size();
            root.children.append(raw);
        } else {
            MsduFieldNode raw;
            raw.name  = QStringLiteral("Payload");
            raw.value = QString(msdu_body.mid(4).toHex(' '));
            raw.rel_start = head_size + 4;
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
            quint32 calc = beacon_crc32(
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

// ================= BEACON 载荷区(MPDU_BEACON_LOAD) =================
// gbBeaconMPDU = payload[16 : 16+PBSize-3](PBSize 由 FCH 内 TMI 决定,回放 TMI=4→136B)。
// 载荷固定头字段相对 gb 起点。解析输出与 Python log 对照:
//   BeaconType/NetWorkingFlag/SimpleBeaconFlag/AssociationFlag/BeaconCEFlag
//   NetSN/CCO_MAC/BeaconPeriodCount/NetRfChannel/NetRfOption + ItemNum + 各条目
static quint32 beacon_crc32(const quint8* d, int len) {
    // 与 BPLCMonitor/cal_crc32 一致:poly=0xEDB88320, init=0xFFFFFFFF, 最后取反
    const quint32 poly = 0xEDB88320;
    quint32 crc = 0xFFFFFFFF;
    for (int i = 0; i < len - 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            quint32 bit_in  = (d[i] >> j) & 0x1;
            quint32 bit_lsb = crc & 0x1;
            crc >>= 1;
            if (bit_in ^ bit_lsb) crc ^= poly;
        }
    }
    return (~crc) & 0xFFFFFFFF;
}

static int beacon_pb_size(quint8 tmi) {
    if (tmi == 0 || tmi == 1)                           return 520;
    if (tmi >= 2 && tmi <= 6)                           return 136;
    if (tmi >= 7 && tmi <= 10)                          return 520;
    if (tmi == 11 || tmi == 12)                         return 264;
    if (tmi == 13 || tmi == 14)                         return 72;
    return -1;
}

// 载荷固定头(相对 gb)
static const FieldSpec kBeaconLoadSpec[] = {
    {"BeaconType",        0, 0, 3,  Fmt::DEC},
    {"NetWorkingFlag",    0, 3, 1,  Fmt::DEC},
    {"SimpleBeaconFlag",  0, 4, 1,  Fmt::DEC},
    {"AssociationFlag",   0, 6, 1,  Fmt::DEC},
    {"BeaconCEFlag",      0, 7, 1,  Fmt::DEC},
    {"NetSN",             1, 0, 8,  Fmt::DEC},
    {"CCO_MACAddr",       2, 0, 48, Fmt::MAC},
    {"BeaconPeriodCount", 8, 0, 32, Fmt::DEC},
    {"NetRfChannel",     12, 0, 8,  Fmt::DEC},
    {"NetRfOption",      13, 0, 2,  Fmt::DEC},
};
static const int kBeaconLoadSpecN = int(sizeof(kBeaconLoadSpec) / sizeof(kBeaconLoadSpec[0]));

// STA Cap 条目(相对条目数据)
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
};
static const int kStaCapSpecN = int(sizeof(kStaCapSpec) / sizeof(kStaCapSpec[0]));

// Route Param 条目(相对条目数据)
static const FieldSpec kRouteParamSpec[] = {
    {"RoutePeriod",            0, 0, 16, Fmt::DEC},
    {"NextRouteEstimationTime",2, 0, 16, Fmt::DEC},
    {"PCODiscoveryListPeriod", 4, 0, 16, Fmt::DEC},
    {"STADiscoveryListPeriod", 6, 0, 16, Fmt::DEC},
};
static const int kRouteParamSpecN = int(sizeof(kRouteParamSpec) / sizeof(kRouteParamSpec[0]));

// TSA 条目头(相对条目数据)
static const FieldSpec kTsaHeadSpec[] = {
    {"NonCCOBeaconNum",        0, 0, 8,  Fmt::DEC},
    {"CCOBeaconNum",           1, 0, 4,  Fmt::DEC},
    {"CSMALineSupportNum",     1, 4, 2,  Fmt::DEC},
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
        quint32 calc = beacon_crc32(
            reinterpret_cast<const quint8*>(gb.constData()), gb.size());
        crc_ok = (stored == calc);
    }
    out.summary = QStringLiteral("BEACON Load CRC32:%1")
                      .arg(crc_ok ? QStringLiteral("OK") : QStringLiteral("FAIL"));

    auto& root = group(out.tree, QStringLiteral("Beacon Load [%1B]").arg(gb.size()));
    MsduFieldNode crc;
    crc.name  = QStringLiteral("BeaconCRC32 [32b]");
    crc.value = crc_ok ? QStringLiteral("OK") : QStringLiteral("FAIL");
    root.children.append(crc);

    // 固定头(0..19)
    add_fields(root.children, gb, 0, kBeaconLoadSpec, kBeaconLoadSpecN);

    quint8 beacon_type = (quint8)get_bits(gb, 0, 0, 3);
    const char* btype_names[] = {"STA Beacon", "PCO Beacon", "CCO Beacon"};
    for (auto& ch : root.children) {
        if (ch.name.startsWith(QStringLiteral("BeaconType"))) {
            ch.value = QStringLiteral("%1 - %2").arg(
                ch.value,
                QString(beacon_type <= 2
                            ? QLatin1String(btype_names[beacon_type])
                            : QStringLiteral("?")));
        }
    }

    // 管理区 = gb[20 : -4]:首字节 ItemNum,随后 head(1B)+len(1B)+内容
    int item_num_off = 20;
    if (gb.size() <= item_num_off + 1) return out;
    quint8 item_num = (quint8)gb[item_num_off];
    MsduFieldNode in;
    in.name  = QStringLiteral("ItemNum [8b]");
    in.value = QString::number(item_num);
    in.rel_start = item_num_off;
    in.rel_len   = 1;
    root.children.append(in);

    int pos = item_num_off + 1;
    for (int n = 0; n < item_num && pos < gb.size() - 4; ++n) {
        quint8 head = (quint8)gb[pos++];
        if (pos >= gb.size() - 4) break;
        int item_len;
        if (head == 0xC0) {  // TSA:长度 2B LE,len-3 = 内容长
            if (pos + 1 >= gb.size() - 4) break;
            item_len = (quint8)gb[pos] | ((quint8)gb[pos + 1] << 8);
            pos += 2;
            item_len -= 3;
        } else {
            item_len = (quint8)gb[pos++] - 2;
        }
        if (item_len <= 0 || pos + item_len > gb.size() - 4) break;
        QByteArray it = gb.mid(pos, item_len);
        int abs0 = pos;  // 条目数据在 gb 中的绝对偏移(供高亮)
        pos += item_len;

        auto& grp = group(root.children,
                          QStringLiteral("Item[%1] type=0x%2 (%3B)")
                              .arg(n).arg(head, 2, 16, QChar('0')).arg(item_len));
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
                        static const char* nm[] = {"All Lines","LineA","LineB","LineC"};
                        ch.value = QStringLiteral("%1 - %2").arg(l)
                            .arg(QLatin1String(nm[l <= 3 ? l : 0]));
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
            case 0xC0: {  // TSA:头 20B + 槽信息
                add_fields(grp.children, it, 0, kTsaHeadSpec, kTsaHeadSpecN, abs0);
                // TSA 时长字段单位 ms(与 Python log "BeaconSlotLen: 30ms" 一致)
                annotate_unit(grp.children, "BeaconSlotLen", QStringLiteral("ms"));
                annotate_unit(grp.children, "CSMASlotSplitLen", QStringLiteral("ms"));
                annotate_unit(grp.children, "TDMASlotLen", QStringLiteral("ms"));
                annotate_unit(grp.children, "BeaconPeriod [", QStringLiteral("ms"));
                annotate_unit(grp.children, "RfBeaconSlotLen", QStringLiteral("ms"));
                int noncco = (int)get_bits(it, 0, 0, 8);
                int csma   = (int)get_bits(it, 1, 4, 2);
                int bind   = (int)get_bits(it, 6, 0, 8);
                int o = 20;
                if (beacon_type != 0 && noncco > 0) {  // 非 CCO 信标槽(2B/条)
                    auto& ng = group(grp.children,
                        QStringLiteral("NonCCOBeaconInfo [%1]").arg(noncco));
                    for (int i = 0; i < noncco && o + 2 <= it.size(); ++i) {
                        MsduFieldNode en;
                        en.name = QStringLiteral("NonCCO[%1]").arg(i);
                        quint16 tei = (quint16)get_bits(it, o, 0, 12);
                        quint8 bt  = (quint8)get_bits(it, o + 1, 4, 1);
                        quint8 rf  = (quint8)get_bits(it, o + 1, 5, 3);
                        en.value = QStringLiteral("TEI=%1 BeaconType=%2 RF=%3")
                                       .arg(tei).arg(bt).arg(rf);
                        en.rel_start = abs0 + o;
                        en.rel_len   = 2;
                        ng.children.append(en);
                        o += 2;
                    }
                }
                if (csma > 0) {
                    auto& cg = group(grp.children,
                        QStringLiteral("CSMASlotInfo [%1]").arg(csma));
                    for (int i = 0; i < csma && o + 4 <= it.size(); ++i) {
                        MsduFieldNode en;
                        en.name = QStringLiteral("CSMA[%1]").arg(i);
                        quint32 len = (quint32)get_bits(it, o, 0, 24);
                        quint8  ln  = (quint8)get_bits(it, o + 3, 0, 2);
                        static const char* nm[] = {"All Lines","LineA","LineB","LineC"};
                        en.value = QStringLiteral("len=%1ms line=%2")
                                       .arg(len)
                                       .arg(QLatin1String(nm[ln <= 3 ? ln : 0]));
                        en.rel_start = abs0 + o;
                        en.rel_len   = 4;
                        cg.children.append(en);
                        o += 4;
                    }
                }
                if (bind > 0) {
                    auto& bg = group(grp.children,
                        QStringLiteral("BindingCSMASlotInfo [%1]").arg(bind));
                    for (int i = 0; i < bind && o + 4 <= it.size(); ++i) {
                        MsduFieldNode en;
                        en.name = QStringLiteral("Binding[%1]").arg(i);
                        quint32 len = (quint32)get_bits(it, o, 0, 24);
                        quint8  ln  = (quint8)get_bits(it, o + 3, 0, 2);
                        static const char* nm[] = {"All Lines","LineA","LineB","LineC"};
                        en.value = QStringLiteral("len=%1ms line=%2")
                                       .arg(len)
                                       .arg(QLatin1String(nm[ln <= 3 ? ln : 0]));
                        en.rel_start = abs0 + o;
                        en.rel_len   = 4;
                        bg.children.append(en);
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
    return out;
}
