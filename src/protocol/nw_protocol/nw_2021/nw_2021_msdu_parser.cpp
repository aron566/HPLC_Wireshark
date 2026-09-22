/// @file nw_2021_msdu_parser.cpp
/// @brief 南网 NW_2021 MSDU/MAC 层解析实现(头解析 + MMe 管理消息字段)
/// @details 移植自 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG/MSDU_Class.py
///          的 MSDU_Process 流程:
///           1. 判帧类型:Version(bit1-2)=2 单跳(MSDU_BASE_S 4B);=1 标准(MSDU_BASE)
///           2. MAC 帧头(MSDU_BASE 32B 长/12B 短):TEI/SNID/重启次数/广播方向
///           3. MSDU 帧头(长 18B / 短 2B,由 MACHeadFlag 决定):MAC 48b + VLAN + 类型
///           4. VLAN==0x8100 → MMe 管理消息(MMType 16b 分支,字段树)
#include "nw_2021_msdu_parser.h"
#include "common/fieldtools.h"
#include "common/fieldspec.h"
#include "common/crc.h"
#include "i18n.h"

namespace {

/// @brief 南网 MMe 管理消息类型(MMType 16-bit,MMe_BASE 字节1-2)
enum MMeType : quint16 {
    MME_ASSOCREQ = 0x0030,
    MME_ASSOCCNF = 0x0031,
    MME_CHANGEPROXYREQ = 0x0032,
    MME_ASSOCIND = 0x0034,
    MME_CHANGEPROXYCNF = 0x0037,
    MME_ASSOCGATHERIND = 0x003A,
    MME_CHANGEPROXYBITMAPCNF = 0x003B,
    MME_LEAVEIND = 0x0049,
    MME_HEARTBEATCHECK = 0x0051,
    MME_DISCOVERNODELIST = 0x0055,
    MME_DELAYLEAVEIND = 0x005D,
    MME_SUCCESSRATEREPORT = 0x005E,
    MME_NETWORKCONFLICTREPORT = 0x005F,
    MME_ZEROCROSSNTBCOLLECTIND = 0x0062,
    MME_ZEROCROSSNTBREPORT = 0x0063,
    MME_NETDIAGNOSE = 0x0064,
    MME_RFCHANNELCONFLICTREPORT = 0x0070,
};

inline QString mme_type_name(quint16 t) {
    switch (t) {
        case 0x0030: return QStringLiteral("MMeAssocReq");
        case 0x0031: return QStringLiteral("MMeAssocCnf");
        case 0x0032: return QStringLiteral("MMeChangeProxyReq");
        case 0x0034: return QStringLiteral("MMeAssocInd");
        case 0x0037: return QStringLiteral("MMeChangeProxyCnf");
        case 0x003A: return QStringLiteral("MMeAssocGatherInd");
        case 0x003B: return QStringLiteral("MMeChangeProxyBitMapCnf");
        case 0x0049: return QStringLiteral("MMeLeaveInd");
        case 0x0051: return QStringLiteral("MMeHeartBeatCheck");
        case 0x0055: return QStringLiteral("MMeDiscoverNodeList");
        case 0x005D: return QStringLiteral("MMeDelayLeaveInd");
        case 0x005E: return QStringLiteral("MMeSuccessRateReport");
        case 0x005F: return QStringLiteral("MMeNetworkConflictReport");
        case 0x0062: return QStringLiteral("MMeZeroCrossNTBCollectInd");
        case 0x0063: return QStringLiteral("MMeZeroCrossNTBReport");
        case 0x0064: return QStringLiteral("MMeNetDiagnose");
        case 0x0070: return QStringLiteral("MMeRFChannelConflictReport");
        default: return QStringLiteral("MMe 0x%1").arg(t, 4, 16, QChar('0'));
    }
}

// ── 枚举值解释(中文 key,经 trl 注册英文;与国网 apply_dicts 对齐) ──

/// 枚举字典翻译:name 命中则 value 改为 "值 - 释义"
static void translate_enum_i18n(QVector<MsduFieldNode>& nodes, const char* field,
                                const char* const* zh_dict, int dict_size) {
    for (auto& n : nodes) {
        if (!n.name.startsWith(QLatin1String(field))) continue;
        bool ok = false;
        const int v = n.value.toInt(&ok);
        if (ok && v >= 0 && v < dict_size && zh_dict[v])
            n.value = QStringLiteral("%1 - %2").arg(v).arg(trl::L(zh_dict[v]));
    }
}

/// 相线(表15:0 未知 1 A 2 B 3 C)
static const char* kLinePhaseZh[] = { "未知", "A相线", "B相线", "C相线" };
/// 角色(0 未知 1 站点 2 代理站点 4 中央协调器,索引 3 保留无释义)
static const char* kRoleZh[] = { "未知", "站点", "代理站点", nullptr, "中央协调器" };
/// 通信成功率计算完成标志(0 未完成 1 已完成)
static const char* kCommRateCalcZh[] = { "未完成", "已完成" };
/// 上行路由类型(表:0 错误 1 同级 2 上级 3 代理主路径 4 上上级)
static const char* kRouteTypeZh[] = {
    "错误路由类型", "同级路由类型", "上级路由类型", "代理主路径路由类型", "上上级路由类型" };
/// 设备类型(表45:0x01 抄控器 ... 0x07 三相表通信模块,0x00/0x08+ 保留)
static const char* kDeviceTypeZh[] = {
    nullptr, "抄控器", "集中器通信模块", "单相电表通信模块", "中继器",
    "II型采集器", "I型采集器", "三相表通信模块" };
/// MAC 地址类型(表46:0 电能表地址 1 模块本身MAC 2 采集器地址)
static const char* kMACAddrTypeZh[] = { "电能表地址", "模块本身MAC地址", "采集器地址" };
/// 代理类型(表50/63:0x1 保留 0x2 动态代理)
static const char* kProxyTypeZh[] = { nullptr, nullptr, "动态代理" };
/// 支持频段标识(表51:0x0 频段0和1 0x1 频段0/1/2)
static const char* kBandSupportZh[] = { "频段0和频段1", "频段0/1/2" };
/// 系统启动原因(表48:0x0 正常重启)
static const char* kBootReasonZh[] = { "正常重启" };
/// 关联确认结果(表53:0x07 保留)
static const char* kAssocCnfResultZh[] = {
    "关联请求成功", "站点不在白名单中", nullptr, "加入站点个数超过上限",
    "没有设置白名单列表", "代理站点个数超过上限", "子站点个数超过上限",
    nullptr, "重复的MAC地址", "超过拓扑层级", "站点再次关联请求入网成功",
    "新站点试图以自己的子站点为代理入网", "组网拓扑中存在环路", "CCO端未知原因出错" };
/// 关联指示结果(表58:0x07 没有回复,0x0A 再次入网)
static const char* kAssocIndResultZh[] = {
    "关联请求成功", "站点不在白名单中", nullptr, "加入站点个数超过上限",
    "没有设置白名单列表", "代理站点个数超过上限", "子站点个数超过上限",
    "没有回复", "重复的MAC地址", "超过拓扑层级", "曾经入网的站点再次入网",
    "新站点试图以自己的子站点为代理入网", "组网拓扑中存在环路", "CCO端未知原因出错" };
/// 最后一个分包标识(表54/59:0 不是 1 是)
static const char* kLastPacketFlagZh[] = { "不是最后一个分包", "是最后一个分包" };
/// 代理变更原因(表64:0x1 周期 0x2 快速)
static const char* kProxyChangeReasonZh[] = { nullptr, "周期代理变更", "快速代理变更" };
/// 代理变更结果(表67/70:0x0 变更成功)
static const char* kProxyChangeResultZh[] = { "变更成功" };
/// 关联汇总结果(6.4.4.1:固定值 0 允许加入网络)
static const char* kAssocGatherResultZh[] = { "允许加入网络" };
/// 离线原因(表72:0x0 未入网却发报文 0x2 拓扑超限 0x4 立即离线)
static const char* kLeaveReasonZh[] = {
    "站点未入网却收到其报文", nullptr, "拓扑层级超过上限", nullptr, "立即离线" };
/// 延迟离线原因(表74:0x3 不在最新白名单)
static const char* kDelayLeaveReasonZh[] = { nullptr, nullptr, nullptr, "站点不在最新白名单中" };
/// 过零NTB采集站点类型(表85:0 单站点 1 全网站点)
static const char* kNTBCollectModeZh[] = { "单站点", "全网站点" };
/// 过零NTB采集周期(表86:0 半个电力线周期 1 一个电力线周期)
static const char* kNTBCollectPeriodZh[] = { "半个电力线周期", "一个电力线周期" };
/// 芯片厂商ID(表90:0x0000 保留 0x0001 HS ... 0x0008 SC)
static const char* kChipIDZh[] = { "保留", "HS", "ES", "TC", "LH", "HT", "RS", "SW", "SC" };
/// 帧类型域(表4:0 确认/否认 1 数据转发 2 命令 3 主动上报 4 抄控器 5 广播 6 数据订阅 14 厂家调试)
static const char* kPacketTypeZh[] = {
    "确认/否认", "数据转发帧", "命令帧", "主动上报帧",
    "抄控器相关协议", "广播命令帧", "数据订阅路由帧",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "厂家调试" };

/// 文件级中→英翻译注册(匿名命名空间一次性)
struct MMeI18nReg {
    MMeI18nReg() {
        trl::register_en("未知", "Unknown");
        trl::register_en("A相线", "LineA");
        trl::register_en("B相线", "LineB");
        trl::register_en("C相线", "LineC");
        trl::register_en("站点", "STA");
        trl::register_en("代理站点", "PCO");
        trl::register_en("中央协调器", "CCO");
        trl::register_en("未完成", "Not Finished");
        trl::register_en("已完成", "Finished");
        trl::register_en("错误路由类型", "Incorrect Route");
        trl::register_en("同级路由类型", "Same-level Backup Route");
        trl::register_en("上级路由类型", "Upper-level Backup Route");
        trl::register_en("代理主路径路由类型", "Proxy Main Path Route");
        trl::register_en("上上级路由类型", "Upper-of-upper Backup Route");
        trl::register_en(" (高可信)", " (high confidence)");
        trl::register_en(" (中可信)", " (medium confidence)");
        trl::register_en(" (低可信)", " (low confidence)");
        trl::register_en("保留", "Reserved");
        trl::register_en("抄控器", "Central Controller");
        trl::register_en("集中器通信模块", "Concentrator Comm Module");
        trl::register_en("单相电表通信模块", "Meter Comm Module");
        trl::register_en("中继器", "Repeater");
        trl::register_en("II型采集器", "TypeII Data Collector");
        trl::register_en("I型采集器", "TypeI Data Collector");
        trl::register_en("三相表通信模块", "3-Phase Meter Comm Module");
        trl::register_en("电能表地址", "Meter MAC Address");
        trl::register_en("模块本身MAC地址", "Module MAC Address");
        trl::register_en("采集器地址", "Collector MAC Address");
        trl::register_en("动态代理", "Dynamic Proxy");
        trl::register_en("频段0和频段1", "Band0 & Band1");
        trl::register_en("频段0/1/2", "Band0/1/2");
        trl::register_en("正常重启", "Normal Boot");
        trl::register_en("关联请求成功", "Association Success");
        trl::register_en("站点不在白名单中", "STA Not in Whitelist");
        trl::register_en("加入站点个数超过上限", "STA Count Exceeds Limit");
        trl::register_en("没有设置白名单列表", "No Whitelist Configured");
        trl::register_en("代理站点个数超过上限", "PCO Count Exceeds Limit");
        trl::register_en("子站点个数超过上限", "Child STA Count Exceeds Limit");
        trl::register_en("没有回复", "No Reply");
        trl::register_en("重复的MAC地址", "Duplicate MAC Address");
        trl::register_en("超过拓扑层级", "Topology Level Exceeded");
        trl::register_en("站点再次关联请求入网成功", "STA Re-association Success");
        trl::register_en("曾经入网的站点再次入网", "Former STA Rejoined");
        trl::register_en("新站点试图以自己的子站点为代理入网", "STA Uses Own Child as Proxy");
        trl::register_en("组网拓扑中存在环路", "Loop in Network Topology");
        trl::register_en("CCO端未知原因出错", "CCO Unknown Error");
        trl::register_en("不是最后一个分包", "Not Last Fragment");
        trl::register_en("是最后一个分包", "Last Fragment");
        trl::register_en("周期代理变更", "Periodic Proxy Change");
        trl::register_en("快速代理变更", "Fast Proxy Change");
        trl::register_en("变更成功", "Change Success");
        trl::register_en("允许加入网络", "Allow Join");
        trl::register_en("站点未入网却收到其报文", "STA Not Joined but Packet Received");
        trl::register_en("拓扑层级超过上限", "Topology Level Exceeds Limit");
        trl::register_en("立即离线", "Leave Immediately");
        trl::register_en("站点不在最新白名单中", "STA Not in Latest Whitelist");
        trl::register_en("单站点", "Single STA");
        trl::register_en("全网站点", "All STA");
        trl::register_en("半个电力线周期", "Half Power Line Cycle");
        trl::register_en("一个电力线周期", "One Power Line Cycle");
        trl::register_en("通道控制信息", "Channel Control Info");
        trl::register_en("业务报文头", "Business Header");
        trl::register_en("APP层数据", "APP Layer Data");
        trl::register_en("确认/否认", "ACK/NACK");
        trl::register_en("数据转发帧", "Data Forward Frame");
        trl::register_en("命令帧", "Command Frame");
        trl::register_en("主动上报帧", "Event Report Frame");
        trl::register_en("抄控器相关协议", "Reader Protocol");
        trl::register_en("广播命令帧", "Broadcast Command Frame");
        trl::register_en("数据订阅路由帧", "Data Subscription Route Frame");
        trl::register_en("厂家调试", "Vendor Debug");
        trl::register_en("确认", "ACK");
        trl::register_en("否认", "NACK");
        trl::register_en("数据透传至设备", "Data Forward to Device");
        trl::register_en("数据透传至模块", "Data Forward to Module");
        trl::register_en("查询终端搜索结果", "Query Meter Search Result");
        trl::register_en("下发搜索终端列表", "Distribute Meter Search List");
        trl::register_en("文件传输", "File Transmission");
        trl::register_en("允许/禁止从节点事件", "Enable/Disable Node Event");
        trl::register_en("从节点重启", "Reboot Node");
        trl::register_en("从节点信息查询", "Query Node Info");
        trl::register_en("下发通信地址映射表列表", "Distribute Address Map");
        trl::register_en("查询从节点运行状态信息", "Query Node Running Status");
        trl::register_en("查询从节点信道信息", "Query Node Channel Info");
        trl::register_en("台区户变关系/相位识别", "Station/Phase Identification");
        trl::register_en("测试帧", "Test Frame");
        trl::register_en("电表事件主动上报", "Meter Event Report");
        trl::register_en("停上电事件上报", "Power On/Off Event Report");
        trl::register_en("设备事件主动上报", "Device Event Report");
        trl::register_en("通信模块事件上报", "Comm Module Event Report");
        trl::register_en("抄控器-CCO协议", "Reader-CCO Protocol");
        trl::register_en("数据透传串口转发", "Data Forward via UART");
    }
} mme_i18n_reg;

static const FieldSpec kMMeAssocReqSpec[] = {
    { "STAMACAddr", 0, 0, 48, Fmt::MAC },
    { "CandidateTEI0", 6, 0, 12, Fmt::DEC },
    { "LinkType0", 7, 4, 1, Fmt::DEC },
    { "MMeAssocReqRSV0", 7, 5, 3, Fmt::HEX4 },
    { "CandidateTEI1", 8, 0, 12, Fmt::DEC },
    { "LinkType1", 9, 4, 1, Fmt::DEC },
    { "MMeAssocReqRSV1", 9, 5, 3, Fmt::HEX4 },
    { "CandidateTEI2", 10, 0, 12, Fmt::DEC },
    { "LinkType2", 11, 4, 1, Fmt::DEC },
    { "MMeAssocReqRSV2", 11, 5, 3, Fmt::HEX4 },
    { "CandidateTEI3", 12, 0, 12, Fmt::DEC },
    { "LinkType3", 13, 4, 1, Fmt::DEC },
    { "MMeAssocReqRSV3", 13, 5, 3, Fmt::HEX4 },
    { "CandidateTEI4", 14, 0, 12, Fmt::DEC },
    { "LinkType4", 15, 4, 1, Fmt::DEC },
    { "MMeAssocReqRSV4", 15, 5, 3, Fmt::HEX4 },
    { "LinePhase0", 16, 0, 8, Fmt::DEC },
    { "CandidateLinePhase1", 17, 0, 8, Fmt::DEC },
    { "CandidateLinePhase2", 18, 0, 8, Fmt::DEC },
    { "DeviceType", 19, 0, 8, Fmt::DEC },
    { "MMeAssocReqRSV5", 20, 0, 8, Fmt::HEX4 },
    { "MMeAssocReqRSV6", 21, 0, 8, Fmt::HEX4 },
    { "MACAddrType", 22, 0, 8, Fmt::DEC },
    { "ModuleType", 23, 0, 2, Fmt::DEC },
    { "Link", 23, 2, 5, Fmt::DEC },
    { "RSV2", 23, 7, 1, Fmt::HEX4 },
    { "STAAssocRandomData", 24, 0, 32, Fmt::DEC },
    { "HardRstCount", 56, 0, 16, Fmt::DEC },
    { "SoftRstCount", 58, 0, 16, Fmt::DEC },
    { "ProxyType", 60, 0, 8, Fmt::DEC },
    { "NetSN", 61, 0, 8, Fmt::DEC },
    { "RSV3", 62, 0, 1, Fmt::HEX4 },
    { "MMeVersion", 62, 1, 4, Fmt::DEC },
    { "RSV4", 62, 5, 3, Fmt::HEX4 },
    { "BandSupport", 63, 0, 2, Fmt::DEC },
    { "RSV5", 63, 2, 6, Fmt::HEX4 },
    { "EndSequence", 64, 0, 32, Fmt::DEC },
};
static const int kMMeAssocReqSpecN = int(sizeof(kMMeAssocReqSpec)/sizeof(kMMeAssocReqSpec[0]));

static const FieldSpec kMMeAssocCnfSpec[] = {
    { "STAMACAddr", 0, 0, 48, Fmt::MAC },
    { "AssocResult", 6, 0, 8, Fmt::DEC },
    { "STALevel", 7, 0, 8, Fmt::DEC },
    { "STATEI", 8, 0, 12, Fmt::DEC },
    { "RSV0", 9, 4, 4, Fmt::HEX4 },
    { "ProxyTEI", 10, 0, 16, Fmt::DEC },
    { "TotalPacketNum", 12, 0, 8, Fmt::DEC },
    { "PacketIndex", 13, 0, 8, Fmt::DEC },
    { "LastPacketFlag", 14, 0, 8, Fmt::DEC },
    { "LinkType", 15, 0, 1, Fmt::DEC },
    { "CarrierFreq", 15, 1, 2, Fmt::DEC },
    { "RSV1", 15, 3, 5, Fmt::HEX4 },
    { "STAAssocRandomData", 16, 0, 32, Fmt::DEC },
    { "STAReAssocTime", 20, 0, 32, Fmt::DEC },
    { "EndSequence", 24, 0, 32, Fmt::DEC },
    { "PathSequence", 28, 0, 32, Fmt::DEC },
    { "NetSN", 32, 0, 8, Fmt::DEC },
    { "MMeVersion", 33, 0, 4, Fmt::DEC },
    { "detechFlag", 33, 4, 1, Fmt::DEC },
    { "RSV2", 33, 5, 19, Fmt::HEX4 },
};
static const int kMMeAssocCnfSpecN = int(sizeof(kMMeAssocCnfSpec)/sizeof(kMMeAssocCnfSpec[0]));

static const FieldSpec kMMeChangeProxyReqSpec[] = {
    { "STATEI", 0, 0, 16, Fmt::DEC },
    { "NewProxyTEI0", 2, 0, 12, Fmt::DEC },
    { "LinkType0", 3, 4, 1, Fmt::DEC },
    { "MMeChangeProxyReqRSV1", 3, 5, 3, Fmt::HEX4 },
    { "NewProxyTEI1", 4, 0, 12, Fmt::DEC },
    { "LinkType1", 5, 4, 1, Fmt::DEC },
    { "MMeChangeProxyReqRSV2", 5, 5, 3, Fmt::HEX4 },
    { "NewProxyTEI2", 6, 0, 12, Fmt::DEC },
    { "LinkType2", 7, 4, 1, Fmt::DEC },
    { "MMeChangeProxyReqRSV3", 7, 5, 3, Fmt::HEX4 },
    { "NewProxyTEI3", 8, 0, 12, Fmt::DEC },
    { "LinkType3", 9, 4, 1, Fmt::DEC },
    { "MMeChangeProxyReqRSV4", 9, 5, 3, Fmt::HEX4 },
    { "NewProxyTEI4", 10, 0, 12, Fmt::DEC },
    { "LinkType4", 11, 4, 1, Fmt::DEC },
    { "MMeChangeProxyReqRSV5", 11, 5, 3, Fmt::HEX4 },
    { "OldProxyTEI", 12, 0, 16, Fmt::DEC },
    { "ProxyType", 14, 0, 8, Fmt::DEC },
    { "Reason", 15, 0, 8, Fmt::DEC },
    { "LinePhase0", 16, 0, 8, Fmt::DEC },
    { "CandidateLinePhase1", 17, 0, 8, Fmt::DEC },
    { "CandidateLinePhase2", 18, 0, 8, Fmt::DEC },
    { "Link", 19, 0, 5, Fmt::DEC },
    { "RSV0", 19, 5, 3, Fmt::HEX4 },
    { "EndSequence", 20, 0, 32, Fmt::DEC },
    { "NetSN", 24, 0, 8, Fmt::DEC },
    { "RSV1", 25, 0, 120, Fmt::HEX4 },
};
static const int kMMeChangeProxyReqSpecN = int(sizeof(kMMeChangeProxyReqSpec)/sizeof(kMMeChangeProxyReqSpec[0]));

static const FieldSpec kMMeAssocIndSpec[] = {
    { "AssocResult", 0, 0, 8, Fmt::DEC },
    { "STALevel", 1, 0, 8, Fmt::DEC },
    { "STAMACAddr", 2, 0, 48, Fmt::MAC },
    { "CCOMACAddr", 8, 0, 48, Fmt::MAC },
    { "STATEI", 14, 0, 12, Fmt::DEC },
    { "RSV0", 15, 4, 4, Fmt::HEX4 },
    { "ProxyTEI", 16, 0, 16, Fmt::DEC },
    { "LinkType", 18, 0, 1, Fmt::DEC },
    { "CarrierFreq", 18, 1, 2, Fmt::DEC },
    { "RSV1", 18, 3, 21, Fmt::HEX4 },
    { "TotalPacketNum", 21, 0, 8, Fmt::DEC },
    { "PacketIndex", 22, 0, 8, Fmt::DEC },
    { "LastPacketFlag", 23, 0, 8, Fmt::DEC },
    { "STAAssocRandomData", 24, 0, 32, Fmt::DEC },
    { "NetSN", 45, 0, 8, Fmt::DEC },
    { "RSV3", 46, 0, 16, Fmt::HEX4 },
    { "STAReAssocTime", 48, 0, 32, Fmt::DEC },
    { "EndSequence", 52, 0, 32, Fmt::DEC },
    { "RSV4", 56, 0, 64, Fmt::HEX4 },
};
static const int kMMeAssocIndSpecN = int(sizeof(kMMeAssocIndSpec)/sizeof(kMMeAssocIndSpec[0]));

static const FieldSpec kMMeChangeProxyCnfSpec[] = {
    { "Result", 0, 0, 32, Fmt::DEC },
    { "TotalPacketNum", 4, 0, 8, Fmt::DEC },
    { "PacketIndex", 5, 0, 8, Fmt::DEC },
    { "STATEI", 6, 0, 16, Fmt::DEC },
    { "MMeChangeProxyCnfRSV1", 7, 5, 3, Fmt::HEX4 },
    { "ProxyTEI", 8, 0, 16, Fmt::DEC },
    { "ChildSum", 10, 0, 16, Fmt::DEC },
    { "RSV", 12, 0, 8, Fmt::HEX4 },
    { "NetSN", 13, 0, 8, Fmt::DEC },
    { "LinkType", 14, 0, 1, Fmt::DEC },
    { "RSV0", 14, 1, 7, Fmt::HEX4 },
    { "RSV1", 15, 0, 8, Fmt::HEX4 },
    { "EndSequence", 16, 0, 32, Fmt::DEC },
    { "PathSequence", 20, 0, 32, Fmt::DEC },
    { "RSV2", 24, 0, 64, Fmt::HEX4 },
};
static const int kMMeChangeProxyCnfSpecN = int(sizeof(kMMeChangeProxyCnfSpec)/sizeof(kMMeChangeProxyCnfSpec[0]));

static const FieldSpec kMMeAssocGatherIndSpec[] = {
    { "AssocResult", 0, 0, 8, Fmt::DEC },
    { "STALevel", 1, 0, 8, Fmt::DEC },
    { "CCOMACAddr", 2, 0, 48, Fmt::MAC },
    { "ProxyTEI", 8, 0, 12, Fmt::DEC },
    { "RSV1", 9, 4, 4, Fmt::HEX4 },
    { "NetSN", 10, 0, 8, Fmt::DEC },
    { "NewSTANumber", 11, 0, 8, Fmt::DEC },
    { "CarrierFreq", 12, 0, 2, Fmt::DEC },
    { "RSV2", 12, 2, 6, Fmt::HEX4 },
    { "RSV3", 13, 0, 120, Fmt::HEX4 },
};
static const int kMMeAssocGatherIndSpecN = int(sizeof(kMMeAssocGatherIndSpec)/sizeof(kMMeAssocGatherIndSpec[0]));

static const FieldSpec kMMeChangeProxyBitMapCnfSpec[] = {
    { "Result", 0, 0, 32, Fmt::DEC },
    { "STATEI", 4, 0, 16, Fmt::DEC },
    { "ProxyTEI", 6, 0, 16, Fmt::DEC },
    { "NetSN", 8, 0, 8, Fmt::DEC },
    { "LinkType", 139, 0, 1, Fmt::DEC },
    { "RSV0", 139, 1, 7, Fmt::HEX4 },
    { "EndSequence", 140, 0, 32, Fmt::DEC },
    { "PathSequence", 144, 0, 32, Fmt::DEC },
};
static const int kMMeChangeProxyBitMapCnfSpecN = int(sizeof(kMMeChangeProxyBitMapCnfSpec)/sizeof(kMMeChangeProxyBitMapCnfSpec[0]));

static const FieldSpec kMMeLeaveIndSpec[] = {
    { "LeaveSTATEI", 0, 0, 16, Fmt::DEC },
    { "Reason", 2, 0, 16, Fmt::DEC },
    { "LeaveSTAMAC", 4, 0, 48, Fmt::MAC },
    { "ProxyTEI", 10, 0, 16, Fmt::DEC },
    { "RSV0", 12, 0, 64, Fmt::HEX4 },
};
static const int kMMeLeaveIndSpecN = int(sizeof(kMMeLeaveIndSpec)/sizeof(kMMeLeaveIndSpec[0]));

static const FieldSpec kMMeHeartBeatCheckSpec[] = {
    { "OriginalSourceTEI", 0, 0, 16, Fmt::DEC },
    { "DiscoverCountTEI", 2, 0, 16, Fmt::DEC },
    { "DiscoverCount", 4, 0, 32, Fmt::DEC },
};
static const int kMMeHeartBeatCheckSpecN = int(sizeof(kMMeHeartBeatCheckSpec)/sizeof(kMMeHeartBeatCheckSpec[0]));

static const FieldSpec kMMeDiscoverNodeListSpec[] = {
    { "STATEI", 0, 0, 16, Fmt::DEC },
    { "Role", 2, 0, 8, Fmt::DEC },
    { "Level", 3, 0, 8, Fmt::DEC },
    { "MACAddr", 4, 0, 48, Fmt::MAC },
    { "ProxyTEI", 10, 0, 16, Fmt::DEC },
    { "RSV0", 12, 0, 31, Fmt::HEX4 },
    { "CommRateCalculateFinish", 15, 7, 1, Fmt::DEC },
    { "ProxyCommRate", 16, 0, 32, Fmt::DEC },
    { "ProxyDownCommRate", 20, 0, 32, Fmt::DEC },
    { "DiscoverNodeNum", 24, 0, 16, Fmt::DEC },
    { "SendDiscoveryPacketCount", 26, 0, 16, Fmt::DEC },
    { "UpRouteEntryNum", 28, 0, 16, Fmt::DEC },
    { "UpRouteEntrySize", 30, 0, 8, Fmt::DEC },
    { "RSV1", 31, 0, 16, Fmt::HEX4 },
    { "RoutePeriodLeftTime", 33, 0, 16, Fmt::DEC },
    { "CandidateLinePhase2", 35, 0, 2, Fmt::DEC },
    { "CandidateLinePhase1", 35, 2, 2, Fmt::DEC },
    { "CandidateLinePhase0", 35, 4, 2, Fmt::DEC },
    { "RSV2", 35, 6, 2, Fmt::HEX4 },
    { "MinCommRate", 36, 0, 8, Fmt::DEC },
    { "RSV3", 37, 0, 40, Fmt::HEX12 },
};
static const int kMMeDiscoverNodeListSpecN = int(sizeof(kMMeDiscoverNodeListSpec)/sizeof(kMMeDiscoverNodeListSpec[0]));

static const FieldSpec kMMeDelayLeaveIndSpec[] = {
    { "Reason", 0, 0, 16, Fmt::DEC },
    { "LeaveSTANum", 2, 0, 16, Fmt::DEC },
    { "LeaveDelayTime", 4, 0, 16, Fmt::DEC },
    { "RSV0", 6, 0, 80, Fmt::HEX4 },
};
static const int kMMeDelayLeaveIndSpecN = int(sizeof(kMMeDelayLeaveIndSpec)/sizeof(kMMeDelayLeaveIndSpec[0]));

static const FieldSpec kMMeSuccessRateReportSpec[] = {
    { "ProxySTATEI", 0, 0, 16, Fmt::DEC },
    { "STANumber", 2, 0, 16, Fmt::DEC },
};
static const int kMMeSuccessRateReportSpecN = int(sizeof(kMMeSuccessRateReportSpec)/sizeof(kMMeSuccessRateReportSpec[0]));

static const FieldSpec kMMeZeroCrossNTBCollectIndSpec[] = {
    { "STATEI", 0, 0, 16, Fmt::DEC },
    { "NTBCollectionMode", 2, 0, 8, Fmt::DEC },
    { "NTBCollectionPeriod", 3, 0, 8, Fmt::DEC },
    { "NTBCollectionQuantity", 4, 0, 8, Fmt::DEC },
};
static const int kMMeZeroCrossNTBCollectIndSpecN = int(sizeof(kMMeZeroCrossNTBCollectIndSpec)/sizeof(kMMeZeroCrossNTBCollectIndSpec[0]));

static const FieldSpec kMMeZeroCrossNTBReportSpec[] = {
    { "STATEI", 0, 0, 16, Fmt::DEC },
    { "TotalCount", 2, 0, 8, Fmt::DEC },
    { "RSV0", 3, 0, 8, Fmt::HEX4 },
    { "NTBBase", 4, 0, 32, Fmt::DEC },
};
static const int kMMeZeroCrossNTBReportSpecN = int(sizeof(kMMeZeroCrossNTBReportSpec)/sizeof(kMMeZeroCrossNTBReportSpec[0]));

static const FieldSpec kMMeNetDiagnoseSpec[] = {
    { "ChipID", 0, 0, 16, Fmt::DEC },
};
static const int kMMeNetDiagnoseSpecN = int(sizeof(kMMeNetDiagnoseSpec)/sizeof(kMMeNetDiagnoseSpec[0]));

static const FieldSpec kMMeRFChannelConflictReportSpec[] = {
    { "CCOMACAddr", 0, 0, 48, Fmt::MAC },
    { "NeighbourNetWorkCount", 6, 0, 8, Fmt::DEC },
};
static const int kMMeRFChannelConflictReportSpecN = int(sizeof(kMMeRFChannelConflictReportSpec)/sizeof(kMMeRFChannelConflictReportSpec[0]));

// 网络冲突上报(表 204):冲突网络 CCO MAC 6B + 邻居个数 1B + 邻居 SNID 位图 2B
static const FieldSpec kMMeNetworkConflictReportSpec[] = {
    { "CCOMACAddr", 0, 0, 48, Fmt::MAC },
    { "NeighbourNetWorkCount", 6, 0, 8, Fmt::DEC },
    { "NeighbourNIDBitMap", 7, 0, 16, Fmt::HEX4 },
};
static const int kMMeNetworkConflictReportSpecN = int(sizeof(kMMeNetworkConflictReportSpec)/sizeof(kMMeNetworkConflictReportSpec[0]));



// ── 南网 MMe 变长区字段表 ─────────────────────────────
// MMeAssocReq:STAVersionInfo 10B(相对 MMe 数据起点 byte 52 = MMeHeadSize+46)
static const FieldSpec kAssocReqSTAVerSpec[] = {
    { "BootReason",       0, 0, 8,  Fmt::DEC },
    { "BootVersion",      1, 0, 8,  Fmt::DEC },
    { "SoftwareVersion",  2, 0, 16, Fmt::DEC },
    { "VersionDataYear",  4, 0, 7,  Fmt::DEC },
    { "VersionDataMonth", 4, 7, 4,  Fmt::DEC },
    { "VersionDataDay",   5, 3, 5,  Fmt::DEC },
    { "ManufacturerID",   6, 0, 16, Fmt::HEX4 },
    { "ChipID",           8, 0, 16, Fmt::HEX4 },
};
static const int kAssocReqSTAVerSpecN = int(sizeof(kAssocReqSTAVerSpec)/sizeof(kAssocReqSTAVerSpec[0]));
// MMeAssocCnf/Ind:RouteInfo 汇总头(相对 RouteInfo 起点,各消息 base 不同)
static const FieldSpec kRouteInfoHeadSpec[] = {
    { "StraightSTASum",     0, 0, 16, Fmt::DEC },
    { "StraightPCOSum",     2, 0, 16, Fmt::DEC },
    { "RouteInfoTableSize", 4, 0, 16, Fmt::DEC },
    { "RSV3",               6, 0, 16, Fmt::HEX4 },
};
static const int kRouteInfoHeadSpecN = int(sizeof(kRouteInfoHeadSpec)/sizeof(kRouteInfoHeadSpec[0]));

// ── 南网 APP 应用层头(APP_BASE, 12B) ──────────────────
// 通道控制信息(表1:报文端口号 1B + 报文标识符 2B + 保留 1B)
static const FieldSpec kChannelCtrlInfoSpec[] = {
    { "PortNum",       0, 0, 8,  Fmt::HEX4 },
    { "PacketID",      1, 0, 16, Fmt::HEX4 },
    { "RSVBits0",      3, 0, 8,  Fmt::HEX4 },
};
static const int kChannelCtrlInfoSpecN = int(sizeof(kChannelCtrlInfoSpec)/sizeof(kChannelCtrlInfoSpec[0]));

// 业务报文头(表2:控制域 2B + 业务标识 1B + 应用版本号 1B + 帧序号 2B + 帧长 2B)
static const FieldSpec kBusinessHeaderSpec[] = {
    { "PacketType",    0, 0, 4,  Fmt::DEC },
    { "RSVBits1",      0, 4, 8,  Fmt::HEX4 },
    { "ExtBusinessFlag", 1, 4, 1, Fmt::BOOL_Y },
    { "RespondFlag",     1, 5, 1, Fmt::BOOL_Y },
    { "StartFlag",       1, 6, 1, Fmt::BOOL_Y },
    { "TransDirectionFlag", 1, 7, 1, Fmt::BOOL_Y },
    { "BusinessID",    2, 0, 8,  Fmt::HEX4 },
    { "AppVersion",    3, 0, 8,  Fmt::DEC },
    { "PacketSN",      4, 0, 16, Fmt::DEC },
    { "PacketLen",     6, 0, 16, Fmt::DEC },
};
static const int kBusinessHeaderSpecN = int(sizeof(kBusinessHeaderSpec)/sizeof(kBusinessHeaderSpec[0]));



// 追加变长 hex 载荷节点(offset 起;max_len<0 到末尾,否则按 max_len 截断)
static void append_payload_hex(MsduInfo& out, const QByteArray& app, int offset,
                               int max_len, const QString& label, int rel_base = 0) {
    if (app.size() <= offset) return;
    int avail = app.size() - offset;
    if (max_len >= 0 && avail > max_len) avail = max_len;
    if (avail <= 0) return;
    MsduFieldNode& n = group(out.tree, QStringLiteral("%1 [%2 B]").arg(label).arg(avail),
        QString::fromLatin1(app.mid(offset, avail).toHex(' ').toUpper()));
    n.rel_start = rel_base + offset; n.rel_len = avail;
}

// ── 南网 APP 应用层解析入口 ────────────────────────────
static QString app_type_name(quint8 packet_type) {
    switch (packet_type) {
    case 0x0: return QStringLiteral("ACK/NACK");
    case 0x1: return QStringLiteral("DataForward");
    case 0x2: return QStringLiteral("Command");
    case 0x3: return QStringLiteral("EventReport");
    case 0x4: return QStringLiteral("ReaderFrame");
    case 0xE: return QStringLiteral("Test");
    case 0xF: return QStringLiteral("FactoryFrame");
    default:  return QStringLiteral("PacketType 0x%1").arg(packet_type, 1, 16);
    }
}


// ── 业务标识(表9)值解释:依赖帧类型,返回空串表示保留/无释义 ──
static QString business_id_name(quint8 port_num, quint8 packet_type, quint8 business_id) {
    switch (packet_type) {
    case 0x0:  // 确认/否认
        if (business_id == 0x00) return trl::L("确认");
        if (business_id == 0x01) return trl::L("否认");
        break;
    case 0x1:  // 数据转发
        if (business_id == 0x00) return trl::L("数据透传至设备");
        if (business_id == 0x01) return trl::L("数据透传至模块");
        break;
    case 0x2:  // 命令
        switch (business_id) {
        case 0x00: return trl::L("查询终端搜索结果");
        case 0x01: return trl::L("下发搜索终端列表");
        case 0x02: return trl::L("文件传输");
        case 0x03: return trl::L("允许/禁止从节点事件");
        case 0x04: return trl::L("从节点重启");
        case 0x05: return trl::L("从节点信息查询");
        case 0x06: return trl::L("下发通信地址映射表列表");
        case 0x07: return trl::L("查询从节点运行状态信息");
        case 0x08: return trl::L("查询从节点信道信息");
        case 0x10: return trl::L("台区户变关系/相位识别");
        case 0xF0: return trl::L("测试帧");
        default: break;
        }
        break;
    case 0x3:  // 主动上报
        if (business_id == 0x00) return trl::L("电表事件主动上报");
        if (business_id == 0x01) return trl::L("停上电事件上报");
        if (business_id == 0x02) return trl::L(port_num == 0x13 ? "通信模块事件上报" : "设备事件主动上报");
        break;
    case 0x4:  // 抄控器协议
        if (business_id == 0x00) return trl::L("抄控器-CCO协议");
        if (business_id == 0x01) return trl::L("数据透传串口转发");
        break;
    case 0x5:  // 广播命令
        switch (business_id) {
        case 0x04: return trl::L("从节点重启");
        case 0x05: return trl::L("从节点信息查询");
        case 0x07: return trl::L("查询从节点运行状态信息");
        case 0x08: return trl::L("查询从节点信道信息");
        default: break;
        }
        break;
    default: break;
    }
    return QString();
}

static void parse_app(MsduInfo& out, const QByteArray& app, int rel_base) {
    if (app.size() < 12) { out.summary = QStringLiteral("APP (truncated)"); return; }
    // 通道控制信息(表1,4B):报文端口号 + 报文标识符 + 保留
    MsduFieldNode& cci = group(out.tree, QStringLiteral("通道控制信息 [4B]"));
    cci.rel_start = rel_base; cci.rel_len = 4;
    add_fields(cci.children, app, 0, kChannelCtrlInfoSpec, kChannelCtrlInfoSpecN, rel_base);
    // 业务报文头(表2,8B):控制域 + 业务标识 + 应用版本号 + 帧序号 + 帧长
    MsduFieldNode& bh = group(out.tree, QStringLiteral("业务报文头 [8B]"));
    bh.rel_start = rel_base + 4; bh.rel_len = 8;
    add_fields(bh.children, app, 4, kBusinessHeaderSpec, kBusinessHeaderSpecN, rel_base);
    const quint8 port_num    = (quint8)get_bits(app, 0, 0, 8);
    const quint8 packet_type = (quint8)get_bits(app, 4, 0, 4);
    const quint8 business_id = (quint8)get_bits(app, 6, 0, 8);
    // 帧类型域 + 业务标识值解释(表4/表9)
    translate_enum_i18n(bh.children, "PacketType", kPacketTypeZh, 15);
    const QString bid_name = business_id_name(port_num, packet_type, business_id);
    if (!bid_name.isEmpty()) {
        for (auto& n : bh.children) {
            if (n.name.startsWith(QLatin1String("BusinessID")))
                n.value = QStringLiteral("0x%1 - %2")
                              .arg(business_id, 2, 16, QChar('0')).arg(bid_name);
        }
    }
    out.summary = QStringLiteral("APP %1 (BID=0x%2)").arg(app_type_name(packet_type))
                      .arg(business_id, 2, 16, QChar('0'));
    // 业务数据单元(业务报文头之后,含业务扩展域)统一以"APP层数据"字段展示
    append_payload_hex(out, app, 12, -1, QStringLiteral("APP层数据"), rel_base);
}



// ── 南网 MMe 循环结构条目表 ──────────────────────────
static const FieldSpec kSTATEISpec[] = {
    { "STATEI", 0, 0, 12, Fmt::DEC },
    { "LinkType", 1, 4, 1, Fmt::BOOL_Y },
    { "RSV", 1, 5, 3, Fmt::HEX4 },
};
static const int kSTATEISpecN = int(sizeof(kSTATEISpec)/sizeof(kSTATEISpec[0]));
static const FieldSpec kStraightPCOSpec[] = {
    { "PCOTEI", 0, 0, 12, Fmt::DEC },
    { "LinkType", 1, 4, 1, Fmt::BOOL_Y },
    { "RSV", 1, 5, 3, Fmt::HEX4 },
    { "PCOChildSum", 2, 0, 16, Fmt::DEC },
};
static const int kStraightPCOSpecN = int(sizeof(kStraightPCOSpec)/sizeof(kStraightPCOSpec[0]));
static const FieldSpec kSTAInfoSpec[] = {
    { "STAMACAddr", 0, 0, 48, Fmt::MAC },
    { "STATEI", 6, 0, 12, Fmt::DEC },
    { "RSV", 7, 4, 4, Fmt::HEX4 },
};
static const int kSTAInfoSpecN = int(sizeof(kSTAInfoSpec)/sizeof(kSTAInfoSpec[0]));
static const FieldSpec kUpRouteInfoSpec[] = {
    { "NextHopTEI", 0, 0, 12, Fmt::DEC },
    { "RSV", 1, 4, 4, Fmt::HEX4 },
    { "RouteType", 2, 0, 8, Fmt::DEC },
};
static const int kUpRouteInfoSpecN = int(sizeof(kUpRouteInfoSpec)/sizeof(kUpRouteInfoSpec[0]));
static const FieldSpec kCommRateInfoSpec[] = {
    { "STATEI", 0, 0, 16, Fmt::DEC },
    { "DownCommRate", 2, 0, 8, Fmt::DEC },
    { "UpCommRate", 3, 0, 8, Fmt::DEC },
};
static const int kCommRateInfoSpecN = int(sizeof(kCommRateInfoSpec)/sizeof(kCommRateInfoSpec[0]));

}  // namespace

MsduInfo NW_2021_MsduParser::parse(const QByteArray& body) {
    MsduInfo out;
    if (body.size() < 4) return out;
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    // 帧类型:Version 字段(bit1-2)。2=单跳帧(MSDU_BASE_S 4B),1=标准帧(MSDU_BASE)
    const quint8 version = (quint8)get_bits(p, 0, 1, 2);

    if (version == 2) {
        // 单跳 MAC 帧头 MSDU_BASE_S(4B):MACHeadFlag Version RSV0 MSDU_Type(1,0,8) MSDULen(2,0,16)
        out.simple_head = true;
        out.msdu_type   = (quint16)get_bits(p, 1, 0, 8);
        const quint16 msdu_len = (quint16)get_bits(p, 2, 0, 16);
        out.total_len = 4 + msdu_len + 4;
        // MSDU 帧尾 4B CRC32:位置 MSDU 载荷(单跳头 4B 之后 msdu_len 字节)之后;
        // 计算覆盖 MSDU 载荷(不含单跳头)
        const int crc_off = 4 + msdu_len;
        if (msdu_len > 0 && crc_off + 4 <= body.size()) {
            quint32 stored = (quint8)body[crc_off]
                | ((quint32)(quint8)body[crc_off + 1] << 8)
                | ((quint32)(quint8)body[crc_off + 2] << 16)
                | ((quint32)(quint8)body[crc_off + 3] << 24);
            quint32 calc = crc32_le(p + 4, msdu_len + 4);
            MsduFieldNode crc;
            crc.name = QStringLiteral("MSDU CRC32");
            crc.value = QStringLiteral("0x%1 %2")
                .arg(stored, 8, 16, QChar('0'))
                .arg(stored == calc ? QStringLiteral("OK") : QStringLiteral("FAIL"));
            crc.rel_start = crc_off;
            crc.rel_len   = 4;
            out.tree.append(crc);
        }
        out.present = true;
        return out;
    }

    // 标准 MAC 帧头 MSDU_BASE:MACHeadFlag(0,0,1) 决定长(32B)/短(12B)
    // 短头至少 12 字节(读到 msdu_seq 在 byte 10-11),不足则无法解析。
    if (body.size() < 12) return out;
    const quint8 mac_head_flag = (quint8)get_bits(p, 0, 0, 1);
    const quint16 msdu_len     = (quint16)get_bits(p, 2, 0, 16);

    out.msdu_dst_tei        = (int)get_bits(p, 4, 0, 12);
    out.msdu_src_tei        = (int)get_bits(p, 5, 4, 12);
    out.restart_count       = (quint8)get_bits(p, 7, 4, 4);
    out.broadcast_direction = (quint8)get_bits(p, 8, 4, 4);
    out.msdu_send_type      = (int)get_bits(p, 9, 0, 3);
    out.msdu_seq            = (quint16)get_bits(p, 10, 0, 16);

    const int mac_hdr_len = (mac_head_flag == 0) ? 32 : 12;
    if (body.size() < mac_hdr_len + msdu_len) return out;
    const QByteArray msdu_body = body.mid(mac_hdr_len, msdu_len);
    out.total_len = mac_hdr_len + msdu_len + 4;

    // MSDU 帧头(长 18B / 短 2B,由 MACHeadFlag 决定)
    if (mac_head_flag == 0) {
        // MSDU_LONGHEAD(18B):原始目的/源 MAC 48b + VLAN 32b + MSDU 类型 16b
        if (msdu_body.size() >= 18) {
            const quint8* q = reinterpret_cast<const quint8*>(msdu_body.constData());
            out.msdu_dst_mac = get_bits(q, 0, 0, 48);
            out.msdu_src_mac = get_bits(q, 6, 0, 48);
            out.vlan_tag     = (quint32)get_bits(q, 12, 0, 32);
            out.msdu_type    = (quint16)get_bits(q, 16, 0, 16);
            // VLAN 0x8100 = 长帧头管理消息(MMe);否则抄表业务(APP)
            if (out.vlan_tag == 0x8100) {
                const QByteArray mme = msdu_body.mid(18);   // MMe 数据(帧头 18B 之后)
                // MMe 头相对重组块体(msdu_body[0])的字节偏移:MAC 帧头 mac_hdr_len + MSDU 帧头 18
                // (add_fields 的 rel_start 基准须对齐 tree_msdu_raw_of 的重组块体起点)
                const int mme_rel_base = mac_hdr_len + 18;
                if (mme.size() >= 6) {
                    const quint16 mm_type = (quint16)get_bits(mme, 1, 0, 16);
                    out.summary = mme_type_name(mm_type);
                    switch (mm_type) {
        case MME_ASSOCREQ: {
            add_fields(out.tree, mme, 6, kMMeAssocReqSpec, kMMeAssocReqSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "LinePhase0", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CandidateLinePhase1", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CandidateLinePhase2", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "DeviceType", kDeviceTypeZh, 8);
            translate_enum_i18n(out.tree, "MACAddrType", kMACAddrTypeZh, 3);
            translate_enum_i18n(out.tree, "ProxyType", kProxyTypeZh, 3);
            translate_enum_i18n(out.tree, "BandSupport", kBandSupportZh, 2);
            if (mme.size() >= 52) {
                MsduFieldNode& info = group(out.tree, QStringLiteral("ManufacturerInfo [144b]"),
                    QString::fromLatin1(mme.mid(34, 18).toHex(' ').toUpper()));
                info.rel_start = mme_rel_base + (34); info.rel_len = 18;
            }
            if (mme.size() >= 62) {
                add_fields(out.tree, mme, 52, kAssocReqSTAVerSpec, kAssocReqSTAVerSpecN, mme_rel_base);
                translate_enum_i18n(out.tree, "BootReason", kBootReasonZh, 1);
            }
            break;
        }
        case MME_ASSOCCNF: {
            add_fields(out.tree, mme, 6, kMMeAssocCnfSpec, kMMeAssocCnfSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "AssocResult", kAssocCnfResultZh, 14);
            translate_enum_i18n(out.tree, "LastPacketFlag", kLastPacketFlagZh, 2);
            annotate_unit(out.tree, "STAReAssocTime", QStringLiteral("ms"));
            const int rb = 42;
            if (mme.size() >= rb + 8) {
                add_fields(out.tree, mme, rb, kRouteInfoHeadSpec, kRouteInfoHeadSpecN, mme_rel_base);
                const quint16 sta_sum = (quint16)get_bits(mme, rb, 0, 16);
                const quint16 pco_sum = (quint16)get_bits(mme, rb + 2, 0, 16);
                int off = rb + 8;
                for (int i = 0; i < sta_sum && off + 2 <= mme.size(); ++i) {
                    MsduFieldNode& n = group(out.tree, QStringLiteral("StraightSTA[%1]").arg(i));
                    n.rel_start = mme_rel_base + (off); n.rel_len = 2;
                    add_fields(n.children, mme, off, kSTATEISpec, kSTATEISpecN, mme_rel_base);
                    off += 2;
                }
                for (int i = 0; i < pco_sum && off + 4 <= mme.size(); ++i) {
                    MsduFieldNode& n = group(out.tree, QStringLiteral("StraightPCO[%1]").arg(i));
                    n.rel_start = mme_rel_base + (off); n.rel_len = 4;
                    add_fields(n.children, mme, off, kStraightPCOSpec, kStraightPCOSpecN, mme_rel_base);
                    const quint16 child_sum = (quint16)get_bits(mme, off + 2, 0, 16);
                    int coff = off + 4;
                    for (int c = 0; c < child_sum && coff + 2 <= mme.size(); ++c) {
                        MsduFieldNode& cn = group(n.children, QStringLiteral("Child[%1]").arg(c));
                        cn.rel_start = mme_rel_base + (coff); cn.rel_len = 2;
                        add_fields(cn.children, mme, coff, kSTATEISpec, kSTATEISpecN, mme_rel_base);
                        coff += 2;
                    }
                    off = coff;
                }
            }
            break;
        }
        case MME_CHANGEPROXYREQ: {
            add_fields(out.tree, mme, 6, kMMeChangeProxyReqSpec, kMMeChangeProxyReqSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "ProxyType", kProxyTypeZh, 3);
            translate_enum_i18n(out.tree, "Reason", kProxyChangeReasonZh, 3);
            translate_enum_i18n(out.tree, "LinePhase0", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CandidateLinePhase1", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CandidateLinePhase2", kLinePhaseZh, 4);
            break;
        }
        case MME_ASSOCIND: {
            add_fields(out.tree, mme, 6, kMMeAssocIndSpec, kMMeAssocIndSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "AssocResult", kAssocIndResultZh, 14);
            translate_enum_i18n(out.tree, "LastPacketFlag", kLastPacketFlagZh, 2);
            annotate_unit(out.tree, "STAReAssocTime", QStringLiteral("ms"));
            const int rb = 70;
            if (mme.size() >= rb + 8) {
                add_fields(out.tree, mme, rb, kRouteInfoHeadSpec, kRouteInfoHeadSpecN, mme_rel_base);
                const quint16 sta_sum = (quint16)get_bits(mme, rb, 0, 16);
                const quint16 pco_sum = (quint16)get_bits(mme, rb + 2, 0, 16);
                int off = rb + 8;
                for (int i = 0; i < sta_sum && off + 2 <= mme.size(); ++i) {
                    MsduFieldNode& n = group(out.tree, QStringLiteral("StraightSTA[%1]").arg(i));
                    n.rel_start = mme_rel_base + (off); n.rel_len = 2;
                    add_fields(n.children, mme, off, kSTATEISpec, kSTATEISpecN, mme_rel_base);
                    off += 2;
                }
                for (int i = 0; i < pco_sum && off + 4 <= mme.size(); ++i) {
                    MsduFieldNode& n = group(out.tree, QStringLiteral("StraightPCO[%1]").arg(i));
                    n.rel_start = mme_rel_base + (off); n.rel_len = 4;
                    add_fields(n.children, mme, off, kStraightPCOSpec, kStraightPCOSpecN, mme_rel_base);
                    const quint16 child_sum = (quint16)get_bits(mme, off + 2, 0, 16);
                    int coff = off + 4;
                    for (int c = 0; c < child_sum && coff + 2 <= mme.size(); ++c) {
                        MsduFieldNode& cn = group(n.children, QStringLiteral("Child[%1]").arg(c));
                        cn.rel_start = mme_rel_base + (coff); cn.rel_len = 2;
                        add_fields(cn.children, mme, coff, kSTATEISpec, kSTATEISpecN, mme_rel_base);
                        coff += 2;
                    }
                    off = coff;
                }
            }
            break;
        }
        case MME_CHANGEPROXYCNF: {
            add_fields(out.tree, mme, 6, kMMeChangeProxyCnfSpec, kMMeChangeProxyCnfSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "Result", kProxyChangeResultZh, 1);
            const quint16 child_sum = (quint16)get_bits(mme, 16, 0, 16);
            int off = 38;
            for (int i = 0; i < child_sum && off + 2 <= mme.size(); ++i) {
                MsduFieldNode& n = group(out.tree, QStringLiteral("ChildSTA[%1]").arg(i));
                n.rel_start = mme_rel_base + (off); n.rel_len = 2;
                add_fields(n.children, mme, off, kSTATEISpec, kSTATEISpecN, mme_rel_base);
                off += 2;
            }
            break;
        }
        case MME_ASSOCGATHERIND: {
            add_fields(out.tree, mme, 6, kMMeAssocGatherIndSpec, kMMeAssocGatherIndSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "AssocResult", kAssocGatherResultZh, 1);
            const quint8 sta_num = (quint8)get_bits(mme, 17, 0, 8);
            int off = 34;  // 消息体 byte 28(MMeHeadSize 6 + 28),RSV0(15B) 之后
            for (int i = 0; i < sta_num && off + 8 <= mme.size(); ++i) {
                MsduFieldNode& n = group(out.tree, QStringLiteral("NewSTA[%1]").arg(i));
                n.rel_start = mme_rel_base + (off); n.rel_len = 8;
                add_fields(n.children, mme, off, kSTAInfoSpec, kSTAInfoSpecN, mme_rel_base);
                off += 8;
            }
            break;
        }
        case MME_CHANGEPROXYBITMAPCNF: {
            add_fields(out.tree, mme, 6, kMMeChangeProxyBitMapCnfSpec, kMMeChangeProxyBitMapCnfSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "Result", kProxyChangeResultZh, 1);
            // 子站点位图(byte 9-138,130 字节):空字节不显示,非空字节按 bitmap[索引][8b] 显示
            if (mme.size() >= 145) {
                auto& bmg = group(out.tree, QStringLiteral("ChildSTA BitMap [130B]"));
                const int bm_base = 15;  // mme[15] = MMe头6B + 固定头9B(Result4+STATEI2+ProxyTEI2+NetSN1)
                // 组节点覆盖整个 bitmap 区域,点击高亮全部所属字节
                bmg.rel_start = mme_rel_base + bm_base; bmg.rel_len = 130;
                for (int i = 0; i < 130; ++i) {
                    const quint8 byte = (quint8)mme[bm_base + i];
                    if (byte == 0) continue;   // 空字节不显示
                    QStringList teis;
                    for (int j = 0; j < 8; ++j) {
                        if (byte & (1u << j)) teis << QStringLiteral("TEI%1").arg(8 * i + j);
                    }
                    MsduFieldNode bl;
                    bl.name = QStringLiteral("bitmap[%1][8b]").arg(i);   // 当前字节在 bitmap 中的索引
                    bl.value = teis.join(QStringLiteral(", "));
                    bl.rel_start = mme_rel_base + (bm_base + i); bl.rel_len = 1;
                    bmg.children.append(bl);
                }
            }
            break;
        }
        case MME_LEAVEIND: {
            add_fields(out.tree, mme, 6, kMMeLeaveIndSpec, kMMeLeaveIndSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "Reason", kLeaveReasonZh, 5);
            break;
        }
        case MME_HEARTBEATCHECK: {
            add_fields(out.tree, mme, 6, kMMeHeartBeatCheckSpec, kMMeHeartBeatCheckSpecN, mme_rel_base);
            // 可发现站点 TEI 位图(byte 8-137,130B):空字节不显示,非空字节按 bitmap[索引][8b] 显示
            if (mme.size() >= 145) {
                auto& bmg = group(out.tree, QStringLiteral("DiscoverableSTA BitMap [130B]"));
                const int bm_base = 14;  // mme[14] = MMe头6B + 固定头8B(OSTEI2+DCTEI2+DCount4)
                bmg.rel_start = mme_rel_base + bm_base; bmg.rel_len = 130;
                for (int i = 0; i < 130; ++i) {
                    const quint8 byte = (quint8)mme[bm_base + i];
                    if (byte == 0) continue;   // 空字节不显示
                    QStringList teis;
                    for (int j = 0; j < 8; ++j) {
                        if (byte & (1u << j)) teis << QStringLiteral("TEI%1").arg(8 * i + j);
                    }
                    MsduFieldNode bl;
                    bl.name = QStringLiteral("bitmap[%1][8b]").arg(i);
                    bl.value = teis.join(QStringLiteral(", "));
                    bl.rel_start = mme_rel_base + (bm_base + i); bl.rel_len = 1;
                    bmg.children.append(bl);
                }
                // 保留(byte 138,1B)
                MsduFieldNode rsv;
                rsv.name = QStringLiteral("RSV [8b]");
                rsv.value = QStringLiteral("0x%1").arg((quint8)mme[bm_base + 130], 2, 16, QChar('0'));
                rsv.rel_start = mme_rel_base + (bm_base + 130); rsv.rel_len = 1;
                out.tree.append(rsv);
            }
            break;
        }
        case MME_DISCOVERNODELIST: {
            add_fields(out.tree, mme, 6, kMMeDiscoverNodeListSpec, kMMeDiscoverNodeListSpecN, mme_rel_base);
            // 值解释(对齐国网:Role/LinePhase/CommRateCalculateFinish 双语,成功率带 %)
            translate_enum_i18n(out.tree, "Role", kRoleZh, 5);
            translate_enum_i18n(out.tree, "CandidateLinePhase0", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CandidateLinePhase1", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CandidateLinePhase2", kLinePhaseZh, 4);
            translate_enum_i18n(out.tree, "CommRateCalculateFinish", kCommRateCalcZh, 2);
            // 相线可信程度说明(6.8.14):高可信/中可信/低可信依次递减
            for (auto& n : out.tree) {
                if (n.name.startsWith(QLatin1String("CandidateLinePhase0")))
                    n.value += trl::L(" (高可信)");
                else if (n.name.startsWith(QLatin1String("CandidateLinePhase1")))
                    n.value += trl::L(" (中可信)");
                else if (n.name.startsWith(QLatin1String("CandidateLinePhase2")))
                    n.value += trl::L(" (低可信)");
            }
            annotate_unit(out.tree, "ProxyCommRate", QStringLiteral("%"));
            annotate_unit(out.tree, "ProxyDownCommRate", QStringLiteral("%"));
            annotate_unit(out.tree, "MinCommRate", QStringLiteral("%"));
            annotate_unit(out.tree, "RoutePeriodLeftTime", QStringLiteral("s"));
            annotate_unit(out.tree, "UpRouteEntrySize", QStringLiteral("bit"));
            const quint16 route_num = (quint16)get_bits(mme, 34, 0, 16);  // UpRouteEntryNum
            const quint16 node_num  = (quint16)get_bits(mme, 30, 0, 16);  // DiscoverNodeNum
            int off = 48;  // 固定头 42B(MMeHead 6 + 消息体 42)
            // 上行路由条目(3B/条:NextHopTEI 12b + RSV 4b + RouteType 8b)
            for (int i = 0; i < route_num && off + 3 <= mme.size(); ++i) {
                MsduFieldNode& n = group(out.tree, QStringLiteral("UpRoute[%1]").arg(i));
                n.rel_start = mme_rel_base + (off); n.rel_len = 3;
                add_fields(n.children, mme, off, kUpRouteInfoSpec, kUpRouteInfoSpecN, mme_rel_base);
                translate_enum_i18n(n.children, "RouteType", kRouteTypeZh, 5);
                off += 3;
            }
            // 发现站点列表位图(128B 按位解析,bit 位置 = TEI)
            QByteArray bm;
            int bm_base = -1;
            if (off + 128 <= mme.size()) {
                bm_base = off;
                bm = mme.mid(off, 128);
                off += 128;
            }
            bool bm_any = false;
            int nset = 0;
            QVector<QStringList> per_byte(bm.size());
            for (int i = 0; i < bm.size(); ++i) {
                const quint8 byte = (quint8)bm[i];
                for (int j = 0; j < 8; ++j) {
                    if (!(byte & (1u << j))) continue;
                    per_byte[i] << QStringLiteral("TEI%1").arg(8 * i + j);
                    ++nset;
                }
                if (!per_byte[i].isEmpty()) bm_any = true;
            }
            // 逐字节显示(空字节不显示,非空字节按 bitmap[索引][8b] 显示,对应 hex 高亮)
            auto& bmg = group(out.tree,
                QStringLiteral("DiscoverySTAList BitMap [%1b]").arg(bm.size()));
            // 组节点覆盖整个 bitmap 区域,点击高亮全部所属字节
            if (bm_base >= 0) { bmg.rel_start = mme_rel_base + bm_base; bmg.rel_len = bm.size(); }
            for (int i = 0; i < bm.size(); ++i) {
                if (per_byte[i].isEmpty()) continue;   // 空字节不显示
                MsduFieldNode bl;
                bl.name = QStringLiteral("bitmap[%1][8b]").arg(i);   // 当前字节在 bitmap 中的索引
                bl.value = per_byte[i].join(QStringLiteral(", "));
                if (bm_base >= 0) { bl.rel_start = mme_rel_base + (bm_base + i); bl.rel_len = 1; }
                bmg.children.append(bl);
            }
            // 收到发现列表信息(置位 TEI 各一条,1B 计数)
            QByteArray cnts;
            int cnts_base = -1;
            if (node_num > 0 && off + node_num <= mme.size()) {
                cnts_base = off;
                cnts = mme.mid(off, node_num);
            }
            if (bm_any && nset > 0) {
                auto& rgi = group(out.tree,
                    QStringLiteral("ReceivedDiscoveryInfo [%1]").arg(nset));
                int order = 0;
                for (int i = 0; i < bm.size(); ++i) {
                    const quint8 byte = (quint8)bm[i];
                    for (int j = 0; j < 8; ++j) {
                        if (!(byte & (1u << j))) continue;
                        const int tei = 8 * i + j;
                        MsduFieldNode rc;
                        rc.name = QStringLiteral("ReceivedDiscoverCount[%1]").arg(order);
                        if (order < cnts.size()) {
                            rc.value = QStringLiteral("%1 - TEI%2")
                                           .arg((quint8)cnts[order]).arg(tei);
                            if (cnts_base >= 0) { rc.rel_start = mme_rel_base + (cnts_base + order); rc.rel_len = 1; }
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
        case MME_DELAYLEAVEIND: {
            add_fields(out.tree, mme, 6, kMMeDelayLeaveIndSpec, kMMeDelayLeaveIndSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "Reason", kDelayLeaveReasonZh, 4);
            annotate_unit(out.tree, "LeaveDelayTime", QStringLiteral("s"));
            const quint16 sta_num = (quint16)get_bits(mme, 8, 0, 16);
            int off = 22;  // 消息体 byte 16(MMeHeadSize 6 + 16),RSV0(10B) 之后
            for (int i = 0; i < sta_num && off + 6 <= mme.size(); ++i) {
                MsduFieldNode& n = group(out.tree, QStringLiteral("LeaveSTA[%1]").arg(i),
                    mac_str(get_bits(mme, off, 0, 48)));
                n.rel_start = mme_rel_base + (off); n.rel_len = 6;
                off += 6;
            }
            break;
        }
        case MME_SUCCESSRATEREPORT: {
            add_fields(out.tree, mme, 6, kMMeSuccessRateReportSpec, kMMeSuccessRateReportSpecN, mme_rel_base);
            const quint16 sta_num = (quint16)get_bits(mme, 8, 0, 16);
            int off = 10;
            for (int i = 0; i < sta_num && off + 4 <= mme.size(); ++i) {
                MsduFieldNode& n = group(out.tree, QStringLiteral("CommRate[%1]").arg(i));
                n.rel_start = mme_rel_base + (off); n.rel_len = 4;
                add_fields(n.children, mme, off, kCommRateInfoSpec, kCommRateInfoSpecN, mme_rel_base);
                annotate_unit(n.children, "DownCommRate", QStringLiteral("%"));
                annotate_unit(n.children, "UpCommRate", QStringLiteral("%"));
                off += 4;
            }
            break;
        }
        case MME_ZEROCROSSNTBCOLLECTIND: {
            add_fields(out.tree, mme, 6, kMMeZeroCrossNTBCollectIndSpec, kMMeZeroCrossNTBCollectIndSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "NTBCollectionMode", kNTBCollectModeZh, 2);
            translate_enum_i18n(out.tree, "NTBCollectionPeriod", kNTBCollectPeriodZh, 2);
            break;
        }
        case MME_ZEROCROSSNTBREPORT: add_fields(out.tree, mme, 6, kMMeZeroCrossNTBReportSpec, kMMeZeroCrossNTBReportSpecN, mme_rel_base); break;
        case MME_NETDIAGNOSE: {
            add_fields(out.tree, mme, 6, kMMeNetDiagnoseSpec, kMMeNetDiagnoseSpecN, mme_rel_base);
            translate_enum_i18n(out.tree, "ChipID", kChipIDZh, 9);
            // DiagInfo 变长诊断数据(mme byte 8 起 = 消息体 byte 2)
            if (mme.size() > 8) {
                MsduFieldNode& d = group(out.tree, QStringLiteral("DiagInfo [%1 B]").arg(mme.size() - 8),
                    QString::fromLatin1(mme.mid(8).toHex(' ').toUpper()));
                d.rel_start = mme_rel_base + (8); d.rel_len = mme.size() - 8;
            }
            break;
        }
        case MME_RFCHANNELCONFLICTREPORT: {
            add_fields(out.tree, mme, 6, kMMeRFChannelConflictReportSpec, kMMeRFChannelConflictReportSpecN, mme_rel_base);
            // 邻居网络条目(表 213,交错):每条 2B = 信道号 1B + option 2bit + 保留 6bit
            const quint8 n = (quint8)get_bits(mme, 12, 0, 8);
            for (int i = 0; i < n && 13 + 2 * i + 1 < mme.size(); ++i) {
                MsduFieldNode& e = group(out.tree, QStringLiteral("Neighbour[%1]").arg(i));
                e.rel_start = mme_rel_base + (13 + 2 * i); e.rel_len = 2;
                MsduFieldNode ch;
                ch.name = QStringLiteral("Channel [8b]");
                ch.value = QString::number((quint8)get_bits(mme, 13 + 2 * i, 0, 8));
                ch.rel_start = mme_rel_base + (13 + 2 * i); ch.rel_len = 1;
                e.children.append(ch);
                MsduFieldNode op;
                op.name = QStringLiteral("Option [2b]");
                op.value = QString::number((quint8)get_bits(mme, 13 + 2 * i + 1, 0, 2));
                op.rel_start = mme_rel_base + (13 + 2 * i + 1); op.rel_len = 1;
                e.children.append(op);
            }
            break;
        }
        case MME_NETWORKCONFLICTREPORT:
            add_fields(out.tree, mme, 6, kMMeNetworkConflictReportSpec, kMMeNetworkConflictReportSpecN, mme_rel_base);
            break;
                        default: break;
                    }
                } else {
                    out.summary = QStringLiteral("MMe (truncated)");
                }
            } else {
                parse_app(out, msdu_body.mid(18), mac_hdr_len + 18);   // 长帧头 APP 数据(帧头 18B 之后)
            }
        }
    } else {
        // MSDU_SHORTHEAD(2B):VLAN 8b + MSDU 类型 8b
        if (msdu_body.size() >= 2) {
            const quint8* q = reinterpret_cast<const quint8*>(msdu_body.constData());
            out.vlan_tag  = (quint32)get_bits(q, 0, 0, 8);
            out.msdu_type = (quint16)get_bits(q, 1, 0, 8);
            parse_app(out, msdu_body.mid(2), mac_hdr_len + 2);   // 短帧头 APP 数据(帧头 2B 之后)
        }
    }
    // ---- MSDU 帧尾 4B CRC32 ----
    // 位置:MSDU 载荷(MAC 帧头 mac_hdr_len 之后 msdu_len 字节)之后紧接 4B;
    // 计算覆盖 MSDU 载荷(不含 MAC 帧头),poly=0xEDB88320
    const int crc_off = mac_hdr_len + msdu_len;
    if (msdu_len > 0 && crc_off + 4 <= body.size()) {
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
            .arg(stored == calc ? QStringLiteral("OK") : QStringLiteral("FAIL"));
        crc.rel_start = crc_off;
        crc.rel_len   = 4;
        out.tree.append(crc);
    }
    out.present = true;
    return out;
}
