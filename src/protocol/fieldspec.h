/// @file fieldspec.h
/// @brief 字段树构建公共工具(各帧解析模块共用)
/// @details 位域取值/字段说明表(FieldSpec)/字段节点树(add_fields/group)/
///          单位注释/CRC32 校验。从原 msduparser.cpp 匿名工具区提取为
///          公共 inline 头,供 msduparser / beaconparser 等帧解析模块使用。
/// @note 头文件常驻 inline,无 .cpp;改动本文件后依赖模块需重编。
#ifndef FIELDSPEC_H
#define FIELDSPEC_H

#include "bplcframe.h"
#include <QByteArray>
#include <QString>
#include <QVector>

/// 取位域值(LSB 位序,与 Python BitDefine 一致)
inline quint64 get_bits(const quint8* d, int start_byte, int start_bit, int bit_len) {
    quint64 v = 0;
    int total = start_byte * 8 + start_bit;
    for (int i = 0; i < bit_len; ++i) {
        int byte = (total + i) / 8;
        int bit  = (total + i) % 8;
        if ((d[byte] >> bit) & 1) v |= (1ULL << i);
    }
    return v;
}

inline quint64 get_bits(const QByteArray& d, int start_byte, int start_bit, int bit_len) {
    return get_bits(reinterpret_cast<const quint8*>(d.constData()),
                    start_byte, start_bit, bit_len);
}

inline QString hex6(quint64 v)  { return QString("0x%1").arg(v, 6, 16, QChar('0')); }
inline QString hex8(quint64 v)  { return QString("0x%1").arg(v, 8, 16, QChar('0')); }
inline QString hex12(quint64 v) { return QString("0x%1").arg(v, 12, 16, QChar('0')); }
inline QString hex4(quint64 v)  { return QString("0x%1").arg(v, 4, 16, QChar('0')); }

/// 48-bit MAC:帧内原始字节序显示
inline QString mac_str(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QString("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += ':';
    }
    return s;
}

/// 一个位域字段说明:名 / 相对字节 / 位偏移 / 位长 / 格式化
enum class Fmt { DEC, HEX6, HEX8, HEX12, HEX4, MAC, BOOL_Y };

struct FieldSpec {
    const char* name;
    int  byte;
    int  bit;
    int  len;
    Fmt  fmt;
};

inline QString fmt_val(Fmt f, quint64 v) {
    switch (f) {
        case Fmt::DEC:    return QString::number(v);
        case Fmt::HEX6:   return hex6(v);
        case Fmt::HEX8:   return hex8(v);
        case Fmt::HEX12:  return hex12(v);
        case Fmt::HEX4:   return hex4(v);
        case Fmt::MAC:    return mac_str(v);
        case Fmt::BOOL_Y: return v ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return QString::number(v);
}

/// 按 FieldSpec 表批量生成字段节点;rel 坐标相对 rel_base(供 UI 高亮换算)
inline void add_fields(QVector<MsduFieldNode>& out, const QByteArray& d, int base,
                       const FieldSpec* specs, int n, int rel_base = 0) {
    for (int i = 0; i < n; ++i) {
        const FieldSpec& s = specs[i];
        quint64 v = get_bits(d, base + s.byte, s.bit, s.len);
        MsduFieldNode node;
        node.name = QStringLiteral("%1 [%2b]").arg(QLatin1String(s.name)).arg(s.len);
        node.value = fmt_val(s.fmt, v);
        // 位域覆盖的字节区间(相对数据起点;供 UI 高亮换算)
        int first_bit = (rel_base + base + s.byte) * 8 + s.bit;
        int last_bit  = first_bit + s.len - 1;
        node.rel_start = first_bit / 8;
        node.rel_len   = last_bit / 8 - node.rel_start + 1;
        out.append(node);
    }
}

/// 追加一个分组节点并返回其子容器引用
inline MsduFieldNode& group(QVector<MsduFieldNode>& out, const QString& name,
                            const QString& val = QString()) {
    MsduFieldNode g;
    g.name = name;
    g.value = val;
    out.append(g);
    return out.last();
}

/// 字段单位注释:命中 name 的节点 value 追加单位后缀(如 % / ms / s)
/// starts=true 时按前缀匹配;false 时按整名精确匹配
inline void annotate_unit(QVector<MsduFieldNode>& nodes, const char* field,
                          const QString& unit, bool starts = true) {
    for (auto& n : nodes) {
        if (starts ? n.name.startsWith(QLatin1String(field))
                   : (n.name == QLatin1String(field)))
            n.value += unit;
    }
}

/// 通用 CRC32(poly=0xEDB88320, init=0xFFFFFFFF, LSB 先行, 末取反);
/// 遍历前 len-4 字节,存储值为末 4B(小端)——MSDU/信标载荷 CRC32 同算法
inline quint32 crc32_le(const quint8* d, int len) {
    const quint32 poly = 0xEDB88320;
    quint32 crc = 0xFFFFFFFF;
    for (int i = 0; i < len - 4; ++i) {
        for (int j = 0; j < 8; ++j) {
            quint32 bit_in  = (d[i] >> j) & 0x1;
            quint32 bit_lsb = crc & 0x1;
            crc >>= 1;
            if (bit_in ^ bit_lsb) crc ^= poly;
        }
    }
    return (~crc) & 0xFFFFFFFF;
}

#endif // FIELDSPEC_H
