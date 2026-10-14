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
#include <QMap>
#include <QString>
#include <QVector>

#include "bplcframe.h"   // FrameContext / DissectField / MpduInfo / TopoEvent / TeiMacPair

/// @brief 解剖器向主界面声明的一列 PacketList 展示/导出能力
/// @details 主界面不再假定插件协议沿用内置列集:活动协议为插件解剖器
///          时,#/Time/Delta 三列仍由宿主提供,其余列按本描述排序显示;
///          每帧的文本值由 DissectResult::column_values 按 id 返回。
///          CSV 导出复用同一组当前可见列,保证界面与导出字段一致。
struct DissectColumn {
    QString id;                    ///< 稳定列 id(同一解剖器内唯一,值映射键)
    QString title;                 ///< 中文列标题
    QString title_en;              ///< 英文列标题(英文界面优先,空则用 title)
    int     width = 100;           ///< 建议初始列宽(px)
    quint32 alignment = 0x0081;    ///< Qt::Alignment 位掩码(默认左对齐+垂直居中)
    bool    default_visible = true;///< 是否默认显示(用户可在表头菜单调整)
};

/// @brief 解剖器向主界面声明的一条报文着色规则
/// @details 着色配置与列能力同构:插件列出每一种报文的颜色设置项,
///          经 Hello 握手声明告知主界面;解剖时每帧用 DissectResult::
///          color_id 引用本表中的 id,主界面按此查表着色(前景/背景)。
///          插件未声明(空表)时主界面沿用内置着色逻辑。颜色为 ARGB
///          (0xAARRGGBB);foreground/background 为 0 表示不指定该项
///          (主界面用其默认前景/无背景)。
struct DissectColorRule {
    QString id;                    ///< 稳定规则 id(同一解剖器内唯一,逐帧引用键)
    QString name;                  ///< 报文类型名(中文)
    QString name_en;               ///< 报文类型名(英文,空则用 name)
    quint32 foreground = 0;        ///< 前景色 ARGB,0=不指定
    quint32 background = 0;        ///< 背景色 ARGB,0=不指定
};

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
    /// 本帧的插件列文本值:键为 dissector_columns() 声明的列 id。
    /// 缺失 id 按空字符串显示/导出;多余 id 由宿主忽略。
    QMap<QString, QString> column_values;
    /// 本帧命中的着色规则 id(dissector_color_rules() 声明的 id);
    /// 空或未声明的 id 时主界面回退内置着色逻辑。
    QString color_id;
};

#endif // BPLC_DISSECT_H
