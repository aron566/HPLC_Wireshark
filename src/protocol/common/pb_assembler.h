/// @file pb_assembler.h
/// @brief 多物理块(PB)组装公共模块(国网/南网共用,协议无关)
/// @details 块级 mechanics 抽出共用:逐块 CRC24 校验、块体提取与 raw 偏移映射、
///          聚合级联记录拆分(南网 SPLC 表48)。重组策略由各协议 parser 自持,
///          行为与抽取前等价:
///            - 南网 SPLC:顺序拼接(分片重组)/ 聚合级联逐帧展示
///              (块 = PB头 4B(表47) + 块体 + 保留 1B + CRC24 3B,§5.1.3.2.1;
///               聚合标志 = PB头 byte2 bit0,表47)
///            - 国网 HDC 2.0:序列号状态机(START 0x40 / END 0x80 / seq 0x3F)
///              (块 = pb_head 1B + 块体 + CRC24 3B)
///          CRC24 恒覆盖块内前 pb_size-3 字节(块头+块体+尾保留),存储于末 3B
///          低字节在前(crc.h crc24_lsb)。
/// @note  header-only(同 crc.h/fieldtools.h 惯例:协议无关小工具免 .cpp,
///        手工枚举源文件的测试 .pro 无需逐一追加)。
#ifndef PROTOCOL_PB_ASSEMBLER_H
#define PROTOCOL_PB_ASSEMBLER_H

#include <QByteArray>
#include <QVector>
#include "crc.h"

namespace pb_asm {

/// @brief PB 块物理布局(块 = 块头 + 块体 + 尾保留 + CRC24)
struct PbLayout {
    int head_len;        ///< 块头长度(南网 4B 表47 / 国网 1B)
    int tail_rsv_len;    ///< 块尾保留长度(南网 1B / 国网 0)
    int crc_len;         ///< CRC24 长度(恒 3,低字节在前)
    int agg_flag_byte;   ///< 聚合标志块内字节(南网 2,表47;无聚合概念置 -1)
    int agg_flag_bit;    ///< 聚合标志位(南网 bit0,表47;无聚合概念置 -1)
    int block_base;      ///< 首块在 raw 中起点(普通/无线=FCH 16;DL-OFDMA=32)
    /// 块体长度 = pb_size - 头 - 尾保留 - CRC
    int body_len(int pb_size) const
        { return pb_size - head_len - tail_rsv_len - crc_len; }
};

/// @brief 单块解析结果(块体 raw 偏移供高亮映射)
struct PbBlock {
    int    raw_offset;      ///< 块起点(raw 偏移)
    int    body_raw_offset; ///< 块体起点(raw 偏移 = 块起点 + 块头长度)
    int    body_len;        ///< 块体长度
    quint8 head_byte;       ///< 块头首字节(国网 pb_head 起始/结束/序号;南网序列号低 8b)
    bool   crc_ok;          ///< CRC24 校验结果
    bool   aggregated;      ///< 聚合标志(布局无聚合字段时恒 false)
};

/// @brief 级联 MAC 帧(南网 SPLC 表48:级联头 2B + 级联数据块)
struct CascadeMac {
    QByteArray mac;       ///< 级联 MAC 帧字节(独立帧,逐帧解析展示)
    int body_start;       ///< 级联头在级联体流(msdu_body)中的偏移
    int mac_len;          ///< 级联数据块长度 = MAC 帧长度(高亮 rel_len)
    bool start_flag;      ///< 级联块起始标识(表48 体1 bit4)
    bool end_flag;        ///< 级联块结束标识(表48 体1 bit5)
};

/// @brief 组装结果
struct AssembleResult {
    QVector<PbBlock> blocks;       ///< 完整容纳块的逐块结果(依序)
    bool truncated = false;        ///< 帧长不足以容纳全部声明块
    bool any_aggregated = false;   ///< 任一块聚合标志=1(表47)
    bool all_crc_ok = true;        ///< 全部完整块 CRC24 通过
    QByteArray msdu_body;          ///< 全部块体依序拼接(分片重组缓冲/级联体流)
    QVector<CascadeMac> cascade;   ///< 聚合级联拆分结果(any_aggregated 时有效)
};

/// @brief 逐块 CRC24 + 块体提取 + 聚合级联拆分
/// @details
/// - 块体依序拼接 → result.msdu_body(重组缓冲 = 级联体流;树层 MsduRawMap 按
///   块换算 rel→raw,跨块多段高亮,见 protocol_tree_builder.h)
/// - 任一块聚合标志=1 → 把 msdu_body 视为连续级联记录流(记录可跨块体边界;
///   实测锚点:3×64B 体流 4 条记录 [2d 10]/[2d 00]/[2d 00]/[2d 20],首记录
///   Start=1、末记录 End=1)按表48 逐条拆分:级联头 2B = 级联数据块长度 12b
///   (LSB 在前:体 o 全 + 体 o+1 低 nib)+ 起始标识(体 o+1 bit4)/结束标识
///   (bit5)/保留(bit6-7),其后为级联长度的独立 MAC 帧;截断/长度非法即停
inline AssembleResult assemble_pb_blocks(const QByteArray& raw, int pb_num,
                                         int pb_size, const PbLayout& layout) {
    AssembleResult res;
    if (pb_num <= 0
        || pb_size <= layout.head_len + layout.tail_rsv_len + layout.crc_len)
        return res;
    const int body_len = layout.body_len(pb_size);
    const int crc_at   = pb_size - layout.crc_len;
    for (int i = 0; i < pb_num; ++i) {
        const int block_start = layout.block_base + i * pb_size;
        if (block_start + pb_size > raw.size()) {
            res.truncated = true;
            break;
        }
        const quint8* blk = reinterpret_cast<const quint8*>(raw.constData())
                            + block_start;
        const quint32 calc = crc24_lsb(blk, pb_size);
        const quint32 rx   = (quint32)blk[crc_at]
                           | ((quint32)blk[crc_at + 1] << 8)
                           | ((quint32)blk[crc_at + 2] << 16);
        PbBlock b;
        b.raw_offset      = block_start;
        b.body_raw_offset = block_start + layout.head_len;
        b.body_len        = body_len;
        b.head_byte       = blk[0];
        b.crc_ok          = (calc == rx);
        b.aggregated      = (layout.agg_flag_byte >= 0 && layout.agg_flag_bit >= 0)
                                ? (((blk[layout.agg_flag_byte]
                                     >> layout.agg_flag_bit) & 0x01) != 0)
                                : false;
        res.all_crc_ok    = res.all_crc_ok && b.crc_ok;
        res.any_aggregated = res.any_aggregated || b.aggregated;
        res.blocks.append(b);
        res.msdu_body += raw.mid(b.body_raw_offset, body_len);
    }
    if (res.any_aggregated) {
        int o = 0;
        while (o + 2 <= res.msdu_body.size()) {
            const int clen = (quint8)res.msdu_body[o]
                           | (((quint8)res.msdu_body[o + 1] & 0x0F) << 8);
            if (clen <= 0 || o + 2 + clen > res.msdu_body.size())
                break;   // 截断/长度非法防御
            CascadeMac c;
            c.body_start = o;
            c.mac_len    = clen;
            c.start_flag = (res.msdu_body[o + 1] & 0x10) != 0;
            c.end_flag   = (res.msdu_body[o + 1] & 0x20) != 0;
            c.mac        = res.msdu_body.mid(o + 2, clen);
            res.cascade.append(c);
            o += 2 + clen;
        }
    }
    return res;
}

}  // namespace pb_asm

#endif  // PROTOCOL_PB_ASSEMBLER_H
