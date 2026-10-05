/// @file iprotocolparser.h
/// @brief 协议解析器抽象接口 + 协议无关的解析结果/过滤条件
/// @details 国网/南网各实现一个 IProtocolParser 子类,工厂按 variant 实例化。
///          ParseResult/ParseFilter 是协议无关的展示结构(字段由公共
///          MpduInfo/MsduInfo 承载,南网仅多 snid/vlan_tag 等字段),UI/过滤/
///          导出/序列化统一消费,不感知具体协议。
#ifndef IPROTOCOLPARSER_H
#define IPROTOCOLPARSER_H

#include "bplcframe.h"
#include "common/protocolvariant.h"
#include <QList>
#include <QMetaType>

/// @brief 协议无关的解析结果(展示层/过滤/导出统一消费)
struct ParseResult {
    PhysicalMeta meta;
    MpduInfo     mpdu;
    QByteArray   msdu_body;
    MsduInfo     msdu;          ///< MSDU/MAC 层解析结果(SOF 重组完整时填充)
    int          msdu_raw_base; ///< msdu_body[0] 在 payload_for_log 中的偏移;-1=跨帧
    MsduInfo     beacon;        ///< BEACON 载荷区解析结果(仅 BEACON 帧)
    qint64       arrival_us;    ///< 帧起始 0x3C 接收时刻(单调 µs,实时串口)
    QByteArray   raw_wire;      ///< 原始串口帧(0x3C...0x3E 原样,含转义)
    bool         accept;
    QString      reject_reason;
    QByteArray   payload_for_log;

    ParseResult() : msdu_raw_base(-1), arrival_us(0), accept(false) {}
};

/// @brief 协议无关的过滤条件(帧类型/链路/NID/TEI)
struct ParseFilter {
    bool           enable_type_filter;
    bool           allow_beacon;
    bool           allow_sof;
    bool           allow_ack;
    bool           allow_coord;
    bool           link_hplc;
    bool           link_hrf;
    bool           nid_filter;
    quint32        nid_mask;
    QList<quint32> nid_list;
    bool           tei_filter;
    QList<quint16> tei_list;

    ParseFilter()
        : enable_type_filter(false),
          allow_beacon(true), allow_sof(true),
          allow_ack(true), allow_coord(true),
          link_hplc(true), link_hrf(true),
          nid_filter(false), nid_mask(0xFFFFFF),
          tei_filter(false) {}
};

/// @brief 协议解析器抽象接口(国网 GW_2022 / 南网 NW_2021 实现)
class IProtocolParser {
public:
    virtual ~IProtocolParser() = default;
    /// @brief 本解析器对应的协议变体
    virtual ProtocolVariant variant() const = 0;
    /// @brief 解析一帧 BplcFrame → ParseResult
    virtual ParseResult parse(const BplcFrame& in, MsduState& msdu,
                              const ParseFilter& f) = 0;
};

Q_DECLARE_METATYPE(ParseResult)
Q_DECLARE_METATYPE(ParseFilter)

#endif // IPROTOCOLPARSER_H
