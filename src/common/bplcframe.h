/// @file bplcframe.h
/// @brief BPLC/HPLC+HRF 上位机协议帧的基础数据结构定义
/// @details 本文件定义三块内容:
///   - PhysicalMeta:从物理层剥头后提取的元信息(时间戳/信道/媒介等)
///   - BplcFrame:一帧经哨兵切分+反转义后的完整载荷(送入解析器)
///   - MpduInfo:MPDU 控制头解析结果(展示层用)
///   - PacketEntry:Wireshark 风格 PacketList 的一行条目
/// @note 不依赖 Qt MOC,可被非 Qt 模块 include
/// @author BPLC_STA_QtMonitor
/// @date 2026-09-05
#ifndef BPLCFRAME_H
#define BPLCFRAME_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

/// @brief TMI → PB 块大小(字节)。与 51242 物理块大小表一致;
///        信标/单块帧同样按 FCH TMI 查表。-1 = TMI 无效
inline int beacon_pb_size(quint8 tmi) {
    if (tmi == 0 || tmi == 1)                           return 520;
    if (tmi >= 2 && tmi <= 6)                           return 136;
    if (tmi >= 7 && tmi <= 10)                          return 520;
    if (tmi == 11 || tmi == 12)                         return 264;
    if (tmi == 13 || tmi == 14)                         return 72;
    return -1;
}

/// @brief MSDU 跨帧重组状态(SOF 多 PB 块)
struct MsduState {
    QByteArray buffer;
    int        received_count;
    int        expected_len;
    bool       complete;

    MsduState() : received_count(0), expected_len(0), complete(false) {}
};

/// @brief 物理层元信息
struct PhysicalMeta {
    quint32  timestamp;       ///< STA 端 NTB 同步计数,25 kHz tick(40 µs/tick)
    quint8   phr_mcs;         ///< HRF 物理头速率(PLC 帧此字段为占位 0)
    quint8   option;          ///< HRF option(1=1M/2=500k/3=200k;PLC 占位)
    quint16  channel;         ///< HRF:信道索引;PLC:频段号(0-3)
    bool     is_rf;           ///< false=PLC 载波;true=HRF 无线
    bool     has_time_tag;    ///< 是否携带 8 字节 BCD 时间标签(has_time_tag=1)
    QDateTime frame_time;     ///< 帧时间(BCD 解码或 PC 本地时间)
    bool     from_raw;        ///< 是否为裸 hex 文本导入(无哨兵封装)

    PhysicalMeta()
        : timestamp(0), phr_mcs(0), option(0), channel(0),
          is_rf(false), has_time_tag(false), from_raw(false) {}
};

/// @brief 一帧完整载荷(经哨兵切分 + 0x3D 反转义后)
struct BplcFrame {
    PhysicalMeta meta;            ///< 物理层元信息(由 SerialReader 填充部分)
    QByteArray   data;            ///< 反转义 + 去哨兵后的净荷(含 isRF 字节 + PDU)
    QString      error_reason;    ///< 非空:该帧被丢弃时附带原因
    qint64       arrival_ms;      ///< PC 接收时刻(epoch ms),用于 UI 节流

    BplcFrame() : arrival_ms(0) {}
};
Q_DECLARE_METATYPE(BplcFrame)

/// @brief MPDU 控制头解析结果(展示层使用)
struct MpduInfo {
    quint8  frame_type;    ///< 定界符类型:0=BEACON 1=SOF 2=ACK 3=COORD 5=SEARCH 6=SWITCH
    quint8  net_type;      ///< 网络类型(0/1)
    quint32 net_id;        ///< 24-bit 网络标识
    quint8  version;       ///< 标准版本号(0/1)
    quint16 src_tei;       ///< 源 TEI(BEACON/SOF/ACK 解析后)
    quint16 dst_tei;       ///< 目的 TEI(SOF/ACK 解析后)
    quint8  link_id;       ///< SOF 链路标识符(8-bit)
    quint16 frame_len;     ///< 帧长(×10µs,12-bit,SOF)
    quint8  pb_num;        ///< 物理块个数(SOF 时,1-4)
    quint16 symbol_num;    ///< 符号数 9-bit
    bool    bc_flag;       ///< SOF 广播标志位
    bool    re_send_flag;  ///< SOF 重发标志位
    bool    encryp_flag;   ///< SOF 加密标志位
    quint8  tmi;           ///< 分级拷贝基本模式(决定 PB Size)
    quint8  tmi_ext;       ///< 分级拷贝扩展模式
    quint16 pb_size;       ///< 由 TMI/TMI_EXT 查表得出的 PB 块大小(72/136/264/520)
    bool    fch_crc_ok;    ///< 控制头 CRC24 校验结果
    bool    pb_crc_ok;     ///< PB 块 CRC24 校验结果(SOF 时,全部块)
    int     pb_index;      ///< PB 序号(分片重组时)
    quint8  pb_head;       ///< PB 头原始字节(bit7=END bit6=START 低6位=seq;SOF 时,首块)
    QVector<quint8> pb_heads;    ///< 各 PB 块头原始字节(按块序,SOF 时)
    QVector<bool>   pb_crc_oks;  ///< 各 PB 块 CRC24 校验结果(按块序,SOF 时)

    // ---- BEACON(帧内时间戳/相线)----
    quint32 beacon_timestamp; ///< 信标帧内时间戳 32-bit(b4..b7)
    quint8  beacon_line;      ///< 相线 2-bit(b11,1):0=Unknown 1=A 2=B 3=C

    // ---- BEACON 载荷区(16B FCH 之后,经 PBSize 定位)----
    quint8  beacon_type;      ///< 信标类型 3-bit(载荷区 0,0):0=STA 1=PCO 2=CCO
    quint8  beacon_netsn;     ///< 组网序列号(载荷区 1,0,8)
    quint64 beacon_cco_mac;   ///< CCO MAC 48-bit(载荷区 2,0,48,帧序显示)
    quint32 beacon_period_cnt;///< 信标周期计数(载荷区 8,0,32)
    quint8  beacon_rf_channel;///< 无线信道号(载荷区 12,0,8)
    quint8  beacon_rf_option; ///< 无线 Option(载荷区 13,0,2)
    int     beacon_item_num;  ///< 管理条目总数(载荷区 20 处 1B)

    // ---- COORD(网间协调帧)----
    quint16 coord_duration;   ///< 协调时隙占用时长 16-bit(ms)
    quint16 coord_shift;      ///< 下次时隙偏移 16-bit(ms)
    quint32 coord_neighbour_nid; ///< 邻居网络号 24-bit
    quint8  coord_rf_channel; ///< 无线信道号 8-bit(表26 本网络无线信道编号)
    quint8  coord_rsv0;       ///< 保留 4-bit(表26 字节12 bit0-3)

    // ---- ACK 扩展帧类型(12,0,4):0=常规 ACK 1=Search 2=Sync 3=切频 ----
    quint8  ack_ext_type;     ///< ACK 扩展类型
    quint8  ack_rx_res;       ///< RxRes 4-bit:0=接收成功(所有 PB CRC 通过) 1=接收失败(≥1 块 CRC 未过)
    quint8  ack_rx_status;    ///< RxStatus 4-bit:PB CRC 通过位图(bit i=第 i+1 块)
    quint8  ack_rx_pb_num;    ///< 接收 PB 数 3-bit
    quint8  ack_rsv0;         ///< RSV0 5-bit(8,3,5)
    quint8  ack_channel_quality; ///< 信道质量 8-bit(dB)
    quint8  ack_sta_load;     ///< STA 负载 8-bit
    quint8  ack_rsv1;         ///< RSV1 8-bit(11,0,8)
    quint64 ack_dst_addr;     ///< 目标 MAC 48-bit(Search/切频)
    quint16 ack_search_tei;   ///< Search STA TEI 12-bit
    quint8  ack_search_freq;  ///< Search 频点 4-bit
    quint32 ack_sync_timestamp; ///< Sync 时间戳 32-bit
    quint16 ack_sync_tei;     ///< Sync TEI 12-bit

    MpduInfo()
        : frame_type(0), net_type(0), net_id(0), version(0),
          src_tei(0), dst_tei(0), link_id(0), frame_len(0),
          pb_num(0), symbol_num(0), bc_flag(false), re_send_flag(false),
          encryp_flag(false), tmi(0), tmi_ext(0), pb_size(0),
          fch_crc_ok(false), pb_crc_ok(false), pb_index(0), pb_head(0),
          beacon_timestamp(0), beacon_line(0),
          beacon_type(0), beacon_netsn(0), beacon_cco_mac(0),
          beacon_period_cnt(0), beacon_rf_channel(0), beacon_rf_option(0),
          beacon_item_num(0),
          coord_duration(0), coord_shift(0), coord_neighbour_nid(0),
          coord_rf_channel(0), coord_rsv0(0),
          ack_ext_type(0), ack_rx_res(0), ack_rx_status(0), ack_rx_pb_num(0),
          ack_rsv0(0), ack_channel_quality(0), ack_sta_load(0), ack_rsv1(0),
          ack_dst_addr(0),
          ack_search_tei(0), ack_search_freq(0),
          ack_sync_timestamp(0), ack_sync_tei(0) {}

    /// @brief 取 FrameType 对应的可读字符串
    QString frame_type_name() const {
        switch (frame_type) {
            case 0: return QStringLiteral("BEACON");
            case 1: return QStringLiteral("SOF");
            case 2: return QStringLiteral("ACK");
            case 3: return QStringLiteral("COORD");
            case 5: return QStringLiteral("SEARCH");
            case 6: return QStringLiteral("SWITCH");
            default: return QStringLiteral("UNKNOWN(%1)").arg(frame_type);
        }
    }
};

/// @brief 一个解析出的字段(层级节点;自递归树)
struct MsduFieldNode {
    QString       name;      ///< 字段名(含 [Nbit] 标注)
    QString       value;     ///< 值文本
    QVector<MsduFieldNode> children;  ///< 子字段(分组/数组条目)
    int           rel_start; ///< 相对 msdu_body 的起始字节(-1=无对应字节,如分组头)
    int           rel_len;   ///< 覆盖字节数

    MsduFieldNode() : rel_start(-1), rel_len(0) {}
};

/// @brief MSDU 解析结果(由 MsduParser 填充)
struct MsduInfo {
    bool    present;         ///< 本帧携带完整 MSDU(重组完成)
    bool    simple_head;     ///< 是否为 MSDU_BASE_S 简头(单跳)
    quint16 msdu_seq;        ///< MSDU 序号(MSDU_BASE 的 MSDUIndex,16-bit)
    int     total_len;       ///< MSDU 帧总长(头+数据+CRC,不含 PB 填充;-1=未知)
    QString summary;         ///< 概要,如 "MMeDiscoverNodeList" / "APP EventPacket"
    QVector<MsduFieldNode> tree;  ///< 字段树(协议树直接挂载显示)

    MsduInfo() : present(false), simple_head(false), msdu_seq(0),
                 total_len(-1) {}
};

/// @brief Wireshark 风格 PacketList 的一行条目
struct PacketEntry {
    int          index;        ///< 序号(从 1 开始累加)
    qint64       epoch_ms;     ///< PC 收到时刻(epoch ms)
    qint64       delta_us;     ///< 与上一帧的时间差(µs;NTB 40µs 分辨,不可比时=ms×1000)
    bool         accepted;     ///< true=accepted, false=dropped(CRC 错或格式异常)
    QString      reason;       ///< dropped 时填原因
    PhysicalMeta meta;         ///< 物理层元信息
    MpduInfo     mpdu;         ///< MPDU 解析结果
    QByteArray   msdu_body;    ///< 重组后的 MSDU body(若完成)
    MsduInfo     msdu;         ///< MSDU/MAC 层字段解析(若重组完成)
    MsduInfo     beacon;       ///< BEACON 载荷区字段解析(仅 BEACON 帧)
    int          msdu_raw_base;///< MSDU body 在 raw_bytes 中的偏移;-1=不在本帧(跨帧重组)
    QByteArray   raw_bytes;    ///< 原始字节,用于 HexView 显示

    PacketEntry()
        : index(0), epoch_ms(0), delta_us(0), accepted(false),
          msdu_raw_base(-1) {}
};
Q_DECLARE_METATYPE(PacketEntry)

#endif // BPLCFRAME_H
