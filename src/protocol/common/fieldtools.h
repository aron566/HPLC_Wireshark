/// @file fieldtools.h
/// @brief 位域取值/设置 + 通用十六进制/MAC 格式化(协议无关公共工具)
/// @details 从原 fieldspec.h 抽取的纯位域工具。位序为 LSB(与 Python BitDefine 一致):
///          start_byte/start_bit 为起始坐标,bit_len 为位长,跨字节按位连续。
#ifndef FIELDTOOLS_H
#define FIELDTOOLS_H

#include <QByteArray>
#include <QString>

/// 取位域值(LSB 位序)
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

/// 置位域值(LSB 位序),仅修改 [start_byte,start_bit) 起的 bit_len 位
inline void set_bits(quint8* d, int start_byte, int start_bit, int bit_len, quint64 v) {
    int total = start_byte * 8 + start_bit;
    for (int i = 0; i < bit_len; ++i) {
        int byte = (total + i) / 8;
        int bit  = (total + i) % 8;
        if ((v >> i) & 1) d[byte] |=  (1 << bit);
        else              d[byte] &= ~(1 << bit);
    }
}

/// 十六进制格式化(固定位宽,补零)
inline QString hex6(quint64 v)  { return QString("0x%1").arg(v, 6, 16, QChar('0')); }
inline QString hex8(quint64 v)  { return QString("0x%1").arg(v, 8, 16, QChar('0')); }
inline QString hex12(quint64 v) { return QString("0x%1").arg(v, 12, 16, QChar('0')); }
inline QString hex4(quint64 v)  { return QString("0x%1").arg(v, 4, 16, QChar('0')); }

/// 48-bit MAC:帧内原始字节序显示(低字节在前)
inline QString mac_str(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QString("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += ':';
    }
    return s;
}

#endif // FIELDTOOLS_H
