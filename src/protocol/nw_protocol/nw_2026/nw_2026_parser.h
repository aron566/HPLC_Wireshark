/// @file nw_2026_parser.h
/// @brief 南网 NW_2026 双模协议解析器头文件
/// @details 提供 NW_2026_Parser 类(IProtocolParser 实现)。南网 2021 报批版
///          链路层与国网 GW_2022 差异:FCH 网络标识为 SNID 4b(非 NID 24b),
///          各帧型字段坐标不同,MSDU 头用 MAC 48b + VLAN 标签。
///          字段坐标参考 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG。
#ifndef NW_2026_PARSER_H
#define NW_2026_PARSER_H

#include "bplcframe.h"
#include "iprotocolparser.h"
#include "../nw_2021/nw_2021_parser.h"   // BPLC 版本委托解析器(成员缓存)

class Statistics;

/// @brief 南网双模 2021 报批版协议解析器
class NW_2026_Parser : public IProtocolParser {
public:
    using Result = ParseResult;
    using Filter = ParseFilter;

    NW_2026_Parser();

    ProtocolVariant variant() const override { return ProtocolVariant::NW_2026; }

    Result parse(const BplcFrame& in, MsduState& msdu, const Filter& f) override;

private:
    bool decode_envelope(const BplcFrame& in, Result& r);
    bool parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err);

    /// @brief BPLC 版委托解析器(标准版本号≠2 帧整帧转交;见 parse() 版本自适应段)
    /// @details NW_2021_Parser 无状态,实例缓存免逐帧栈上构造;
    ///          解析器由 DispatcherWorker 单线程持有,成员缓存无并发问题。
    NW_2021_Parser m_legacy;
};

#endif // NW_2026_PARSER_H
