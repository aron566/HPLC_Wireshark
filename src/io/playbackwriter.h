/// @file playbackwriter.h
/// @brief 把已捕获帧写成"回放 bin"格式(与 SerialReader 文件回放同构)
/// @details 每帧 = 0x3C + 转义(data) + 0x3E;
///          data = [dlen2LE][ts4LE][phr_mcs][option][channel][isRF][MPDU]。
///          读取端(serialreader try_extract_frame)只按 0x3C/0x3E 切帧并对
///          0x3D 反转义,dlen 字段不校验;因此 dlen 填 MPDU+6 即可被重新解析。
#ifndef PLAYBACKWRITER_H
#define PLAYBACKWRITER_H

#include "bplcframe.h"
#include <QVector>

namespace playback {

inline QByteArray escape_frame_data(const QByteArray& data) {
    QByteArray esc;
    esc.reserve(data.size() + 8);
    for (char c : data) {
        const quint8 b = static_cast<quint8>(c);
        if (b == 0x3C || b == 0x3E || b == 0x3D) {   // 0x3D + (0xFF ^ b)
            esc.append(char(0x3D)).append(char(0xFF - b));
        } else {
            esc.append(c);
        }
    }
    return esc;
}

/// @brief 单帧 → 回放 bin 帧字节(含 0x3C/0x3E 与转义)
inline QByteArray frame_to_playback(const PacketEntry& e) {
    // 裸 hex 导入帧 raw_bytes 首字节为 isRF(log hex 行语义),其余帧为纯 MPDU
    QByteArray mpdu;
    if (e.meta.from_raw && !e.raw_bytes.isEmpty())
        mpdu = e.raw_bytes.mid(1);
    else
        mpdu = e.raw_bytes;
    if (mpdu.isEmpty()) return {};

    const quint16 dlen = quint16(mpdu.size() + 6);
    const quint32 ts   = quint32(e.epoch_ms & 0xFFFFFFFFu);
    QByteArray data;
    data.reserve(mpdu.size() + 10);
    data.append(char(dlen & 0xFF)).append(char(dlen >> 8));
    data.append(char(ts & 0xFF)).append(char((ts >> 8) & 0xFF))
        .append(char((ts >> 16) & 0xFF)).append(char((ts >> 24) & 0xFF));
    data.append(char(e.meta.phr_mcs)).append(char(e.meta.option))
        .append(char(e.meta.channel)).append(char(e.meta.is_rf ? 1 : 0));
    data.append(mpdu);

    QByteArray out;
    out.append(char(0x3C));
    out.append(escape_frame_data(data));
    out.append(char(0x3E));
    return out;
}

/// @brief 全部帧 → 完整回放 bin 文件内容(空帧自动跳过)
inline QByteArray build_playback_bin(const QVector<PacketEntry>& entries) {
    QByteArray buf;
    buf.reserve(entries.size() * 64);
    for (const PacketEntry& e : entries) {
        const QByteArray fr = frame_to_playback(e);
        if (!fr.isEmpty()) buf.append(fr);
    }
    return buf;
}

}  // namespace playback

#endif // PLAYBACKWRITER_H
