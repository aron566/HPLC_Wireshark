/// @file dissect.h
/// @brief 插件解剖器通用结果类型（Wireshark 风格，见 docs/DISSECTOR_ARCH.md）
/// @details 主程序把去封装后的净荷字节 + FrameContext 交给解剖器插件，
///          插件返回 DissectResult：列表标量 + 通用字段树 + topo 载荷。
/// @note FrameContext / DissectField 定义在 bplcframe.h（与 MsduFieldNode
///       同层，供 PacketEntry/ParseResult 直接内嵌，避免头文件循环）；
///       字段树坐标以 DissectResult::dissected_bytes 为坐标系（解剖器
///       自行剥除媒介/传输前缀后的字节，与内置解析器 payload_for_log
///       及 Hex 高亮坐标一致）。
#ifndef BPLC_DISSECT_H
#define BPLC_DISSECT_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include "bplcframe.h"   // FrameContext / DissectField / MpduInfo / TopoEvent / TeiMacPair

/// @brief 解剖器返回的通用结构化结果
struct DissectResult {
    bool    accept = false;
    QString reject_reason;
    MpduInfo mpdu;              ///< 列表/统计/过滤所需的公共头标量
    bool    msdu_present = false;
    QString msdu_summary;       ///< 概要文本（进 search_text）
    QByteArray msdu_body;       ///< 重组后的 MSDU（若有）
    int     msdu_raw_base = -1; ///< msdu_body[0] 在 dissected_bytes 中的偏移，-1=跨帧
    QByteArray dissected_bytes; ///< 实际解剖的字节（剥前缀后）；字段坐标基准，
                                ///< 空则宿主回退用输入 payload 本身
    /// 解剖器从净荷精化出的帧外信息（媒介头里的 NTB/通道/RF 标志等）：
    /// has_refined_ctx 时，其 ntb/channel/is_rf 覆盖宿主 ctx 的粗值，
    /// 供 Physical 段展示（与内置解析器 ParseResult.meta 的精化口径一致）。
    bool         has_refined_ctx = false;
    FrameContext refined_ctx;
    TopoEvent topo_event;       ///< topo 载荷（kind=Other 表示无）
    QVector<TeiMacPair> tei_mac_pairs;
    QVector<DissectField> fields; ///< 协议段字段树（Frame/Physical 公共段除外）
};

#endif // BPLC_DISSECT_H
