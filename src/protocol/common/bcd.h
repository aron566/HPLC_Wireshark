/// @file bcd.h
/// @brief BCD 编解码工具(协议无关公共)
/// @details 帧时间标签 8B BCD(yy/mm/dd/hh/mm/ss/ms 十位/ms 个位)解码用。
#ifndef PROTOCOL_BCD_H
#define PROTOCOL_BCD_H

#include <QtGlobal>

/// 单字节 BCD → 十进制(0x59 → 59)
inline int bcd2dec(quint8 b) {
    return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

#endif // PROTOCOL_BCD_H
