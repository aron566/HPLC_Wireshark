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
    MME_ZEROCROSSNTBCOLLECTIND = 0x0062,
    MME_ZEROCROSSNTBREPORT = 0x0063,
    MME_NETDIAGNOSE = 0x0064,
    MME_RFCHANNELCONFLICTREPORT = 0x0070,
    MME_STATIONTEILISTREQUEST = 0x0083,
    MME_STATIONTEILISTRESPONSE = 0x0084,
    MME_CONVERGENCEDATAREPORT = 0x00A0,
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
        case 0x0062: return QStringLiteral("MMeZeroCrossNTBCollectInd");
        case 0x0063: return QStringLiteral("MMeZeroCrossNTBReport");
        case 0x0064: return QStringLiteral("MMeNetDiagnose");
        case 0x0070: return QStringLiteral("MMeRFChannelConflictReport");
        case 0x0083: return QStringLiteral("MMeStationTEIListRequest");
        case 0x0084: return QStringLiteral("MMeStationTEIListResponse");
        case 0x00A0: return QStringLiteral("MMeConvergenceDataReport");
        default: return QStringLiteral("MMe 0x%1").arg(t, 4, 16, QChar('0'));
    }
}

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
    { "MACAddrType", 22, 0, 8, Fmt::HEX12 },
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
    { "LinePhase0", 35, 4, 2, Fmt::DEC },
    { "RSV2", 35, 6, 2, Fmt::HEX4 },
    { "MinCommRate", 36, 0, 8, Fmt::DEC },
    { "ExtBitMapNum", 37, 0, 16, Fmt::DEC },
    { "ExtBitMapSize", 39, 0, 8, Fmt::DEC },
    { "RSV3", 40, 0, 16, Fmt::HEX4 },
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

static const FieldSpec kMMeStationTEIListRequestSpec[] = {
    { "RequestSeq", 0, 0, 8, Fmt::DEC },
    { "InitiatorMAC", 1, 0, 48, Fmt::MAC },
    { "StationNum", 7, 0, 8, Fmt::DEC },
};
static const int kMMeStationTEIListRequestSpecN = int(sizeof(kMMeStationTEIListRequestSpec)/sizeof(kMMeStationTEIListRequestSpec[0]));

static const FieldSpec kMMeStationTEIListResponseSpec[] = {
    { "RequestSeq", 0, 0, 8, Fmt::DEC },
    { "InitiatorMAC", 1, 0, 48, Fmt::MAC },
    { "StationNum", 7, 0, 8, Fmt::DEC },
};
static const int kMMeStationTEIListResponseSpecN = int(sizeof(kMMeStationTEIListResponseSpec)/sizeof(kMMeStationTEIListResponseSpec[0]));

static const FieldSpec kMMeConvergenceDataReportSpec[] = {
    { "STATEI", 0, 0, 12, Fmt::DEC },
    { "RSV", 0, 12, 4, Fmt::HEX4 },
    { "PackIndex", 2, 0, 8, Fmt::DEC },
    { "PackCount", 3, 0, 8, Fmt::DEC },
    { "SourceAddr", 0, 0, 48, Fmt::MAC },
    { "DestinationAddr", 6, 0, 48, Fmt::MAC },
    { "RSV0", 12, 0, 8, Fmt::HEX4 },
    { "BusCode", 13, 0, 8, Fmt::DEC },
    { "ForwardDataLen", 14, 0, 16, Fmt::DEC },
};
static const int kMMeConvergenceDataReportSpecN = int(sizeof(kMMeConvergenceDataReportSpec)/sizeof(kMMeConvergenceDataReportSpec[0]));


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
        out.present = true;
        return out;
    }

    // 标准 MAC 帧头 MSDU_BASE:MACHeadFlag(0,0,1) 决定长(32B)/短(12B)
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
                if (mme.size() >= 6) {
                    const quint16 mm_type = (quint16)get_bits(mme, 1, 0, 16);
                    out.summary = mme_type_name(mm_type);
                    switch (mm_type) {
        case MME_ASSOCREQ: add_fields(out.tree, mme, 6, kMMeAssocReqSpec, kMMeAssocReqSpecN); break;
        case MME_ASSOCCNF: add_fields(out.tree, mme, 6, kMMeAssocCnfSpec, kMMeAssocCnfSpecN); break;
        case MME_CHANGEPROXYREQ: add_fields(out.tree, mme, 6, kMMeChangeProxyReqSpec, kMMeChangeProxyReqSpecN); break;
        case MME_ASSOCIND: add_fields(out.tree, mme, 6, kMMeAssocIndSpec, kMMeAssocIndSpecN); break;
        case MME_CHANGEPROXYCNF: add_fields(out.tree, mme, 6, kMMeChangeProxyCnfSpec, kMMeChangeProxyCnfSpecN); break;
        case MME_ASSOCGATHERIND: add_fields(out.tree, mme, 6, kMMeAssocGatherIndSpec, kMMeAssocGatherIndSpecN); break;
        case MME_CHANGEPROXYBITMAPCNF: add_fields(out.tree, mme, 6, kMMeChangeProxyBitMapCnfSpec, kMMeChangeProxyBitMapCnfSpecN); break;
        case MME_LEAVEIND: add_fields(out.tree, mme, 6, kMMeLeaveIndSpec, kMMeLeaveIndSpecN); break;
        case MME_HEARTBEATCHECK: add_fields(out.tree, mme, 6, kMMeHeartBeatCheckSpec, kMMeHeartBeatCheckSpecN); break;
        case MME_DISCOVERNODELIST: add_fields(out.tree, mme, 6, kMMeDiscoverNodeListSpec, kMMeDiscoverNodeListSpecN); break;
        case MME_DELAYLEAVEIND: add_fields(out.tree, mme, 6, kMMeDelayLeaveIndSpec, kMMeDelayLeaveIndSpecN); break;
        case MME_SUCCESSRATEREPORT: add_fields(out.tree, mme, 6, kMMeSuccessRateReportSpec, kMMeSuccessRateReportSpecN); break;
        case MME_ZEROCROSSNTBCOLLECTIND: add_fields(out.tree, mme, 6, kMMeZeroCrossNTBCollectIndSpec, kMMeZeroCrossNTBCollectIndSpecN); break;
        case MME_ZEROCROSSNTBREPORT: add_fields(out.tree, mme, 6, kMMeZeroCrossNTBReportSpec, kMMeZeroCrossNTBReportSpecN); break;
        case MME_NETDIAGNOSE: add_fields(out.tree, mme, 6, kMMeNetDiagnoseSpec, kMMeNetDiagnoseSpecN); break;
        case MME_RFCHANNELCONFLICTREPORT: add_fields(out.tree, mme, 6, kMMeRFChannelConflictReportSpec, kMMeRFChannelConflictReportSpecN); break;
        case MME_STATIONTEILISTREQUEST: add_fields(out.tree, mme, 6, kMMeStationTEIListRequestSpec, kMMeStationTEIListRequestSpecN); break;
        case MME_STATIONTEILISTRESPONSE: add_fields(out.tree, mme, 6, kMMeStationTEIListResponseSpec, kMMeStationTEIListResponseSpecN); break;
        case MME_CONVERGENCEDATAREPORT: add_fields(out.tree, mme, 6, kMMeConvergenceDataReportSpec, kMMeConvergenceDataReportSpecN); break;                        default: break;
                    }
                } else {
                    out.summary = QStringLiteral("MMe (truncated)");
                }
            } else {
                out.summary = QStringLiteral("APP 0x%1").arg(out.msdu_type, 4, 16, QChar('0'));
            }
        }
    } else {
        // MSDU_SHORTHEAD(2B):VLAN 8b + MSDU 类型 8b
        if (msdu_body.size() >= 2) {
            const quint8* q = reinterpret_cast<const quint8*>(msdu_body.constData());
            out.vlan_tag  = (quint32)get_bits(q, 0, 0, 8);
            out.msdu_type = (quint16)get_bits(q, 1, 0, 8);
            out.summary   = QStringLiteral("APP 0x%1").arg(out.msdu_type, 2, 16, QChar('0'));
        }
    }
    out.present = true;
    return out;
}
