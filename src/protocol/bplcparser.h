/// @file bplcparser.h
/// @brief BPLC/HPLC+HRF 协议解析器头文件
/// @details 提供 BplcParser 类,把一帧 BplcFrame 解码为 MPDU/MAC 字段,
///          并支持 SOF 多 PB 块的 MSDU 重组。
#ifndef BPLCPARSER_H
#define BPLCPARSER_H

#include "bplcframe.h"
#include "msduparser.h"
#include <QObject>
#include <functional>

class Statistics;

class BplcParser {
public:
    struct Result {
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

        Result() : msdu_raw_base(-1), arrival_us(0), accept(false) {}
    };

    struct Filter {
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

        Filter()
            : enable_type_filter(false),
              allow_beacon(true), allow_sof(true),
              allow_ack(true), allow_coord(true),
              link_hplc(true), link_hrf(true),
              nid_filter(false), nid_mask(0xFFFFFF),
              tei_filter(false) {}
    };

    BplcParser();

    Result parse(const BplcFrame& in, MsduState& msdu, const Filter& f);

private:
    bool decode_envelope(const BplcFrame& in, Result& r);
    bool parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err);
};

Q_DECLARE_METATYPE(BplcParser::Filter)
Q_DECLARE_METATYPE(BplcParser::Result)

#endif // BPLCPARSER_H
