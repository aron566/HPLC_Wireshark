/// @file nw_2021_parser.h
/// @brief 南网 NW_2021 双模协议解析器头文件
/// @details 提供 NW_2021_Parser 类(IProtocolParser 实现)。南网 2021 报批版
///          链路层与国网 GW_2022 差异:FCH 网络标识为 SNID 4b(非 NID 24b),
///          各帧型字段坐标不同,MSDU 头用 MAC 48b + VLAN 标签。
///          字段坐标参考 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG。
#ifndef NW_2021_PARSER_H
#define NW_2021_PARSER_H

#include "bplcframe.h"
#include "iprotocolparser.h"

class Statistics;

/// @brief 南网双模 2021 报批版协议解析器
class NW_2021_Parser : public IProtocolParser {
public:
    using Result = ParseResult;
    using Filter = ParseFilter;

    NW_2021_Parser();

    ProtocolVariant variant() const override { return ProtocolVariant::NW_2021; }

    Result parse(const BplcFrame& in, MsduState& msdu, const Filter& f) override;

private:
    bool decode_envelope(const BplcFrame& in, Result& r);
    bool parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err);
};

#endif // NW_2021_PARSER_H
