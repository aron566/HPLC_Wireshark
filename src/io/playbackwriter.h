/// @file playbackwriter.h
/// @brief 把已捕获帧写成"回放 bin"格式(与 SerialReader 文件回放同构)
/// @details 每帧 = 0x3C + 转义(data) + 0x3E;
///          data = [dlen2LE][ts4LE][phr_mcs][option][channel][isRF][MPDU]。
///          读取端(serialreader try_extract_frame)只按 0x3C/0x3E 切帧并对
///          0x3D 反转义,dlen 字段不校验;因此 dlen 填 MPDU+6 即可被重新解析。
///
///          导出的文件在 data 最前面带 8 字节 BCD 绝对时间标签
///          (年月日时分秒毫秒,与解码器 has_time_tag 头同构):
///          data = [bcd_time8][dlen2LE][ts4LE][phr][option][channel][isRF][MPDU]。
///          回放读取端用 looks_like_bcd_time() 自动识别该字段并恢复每帧
///          原始捕获时刻;老文件(无该字段)回退到本地时间。
#ifndef PLAYBACKWRITER_H
#define PLAYBACKWRITER_H

#include "bplcframe.h"
#include <QVector>
#include <QDateTime>

namespace playback {

/// @brief 是否为合法 BCD 字节(两个 4bit 位均 <=9)
inline bool bcd_ok(quint8 byte, int max_dec) {
    const int lo = byte & 0x0F;
    const int hi = byte >> 4;
    return hi <= 9 && lo <= 9 && (hi * 10 + lo) <= max_dec;
}

/// @brief 自动识别 data 开头是否带 8B BCD 时间标签(年份/月/日/时/分/秒/毫秒)
/// @note  判定失败(误判旧格式)的概率极低:8 字节同时满足 BCD 日期时间约束
///        的组合很少;识别仅用于"文件回放/导出"的 bin,不参与串口帧。
inline bool looks_like_bcd_time(const QByteArray& d) {
    if (d.size() < 28) return false;
    const auto at = [&d](int i) { return static_cast<quint8>(d[i]); };
    // 前 8B:yy(00-99) mo(1-12) dd(1-31) hh(0-23) mm(0-59) ss(0-59) msH(0-9) msL(0-99)
    if (!bcd_ok(at(0), 99)) return false;
    if (!bcd_ok(at(1), 12) || at(1) == 0) return false;
    if (!bcd_ok(at(2), 31) || at(2) == 0) return false;
    if (!bcd_ok(at(3), 23)) return false;
    if (!bcd_ok(at(4), 59)) return false;
    if (!bcd_ok(at(5), 59)) return false;
    if (!bcd_ok(at(6), 9)) return false;   // 毫秒百位
    if (!bcd_ok(at(7), 99)) return false;
    return true;
}

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

/// @brief 0x3D 反转义(与 escape_frame_data 互逆):0x3D xx → 0xFF ^ xx
inline QByteArray unescape_frame_data(const QByteArray& esc) {
    QByteArray out;
    out.reserve(esc.size());
    for (int i = 0; i < esc.size(); ++i) {
        const quint8 b = static_cast<quint8>(esc[i]);
        if (b == 0x3D && i + 1 < esc.size()) {
            ++i;
            out.append(char(0xFF - static_cast<quint8>(esc[i])));
        } else {
            out.append(esc[i]);
        }
    }
    return out;
}

/// @brief epoch_ms → 8B BCD 时间标签(本地时间,与解码器 has_time_tag 同构)
inline QByteArray bcd_time_tag(qint64 epoch_ms) {
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(epoch_ms);
    const QDate date = dt.date();
    const QTime time = dt.time();
    const auto bcd = [](int v) { return static_cast<char>(((v / 10) << 4) | (v % 10)); };
    QByteArray out;
    out.reserve(8);
    out.append(bcd(date.year() - 2000)).append(bcd(date.month()))
       .append(bcd(date.day())).append(bcd(time.hour()))
       .append(bcd(time.minute())).append(bcd(time.second()))
       .append(bcd(time.msec() / 100))          // 毫秒百位(0-9)
       .append(bcd(time.msec() % 100));          // 毫秒低两位(0-99)
    return out;
}

/// @brief 单帧 → 回放 bin 帧字节(含 0x3C/0x3E 转义与 8B 起始时间标签)
inline QByteArray frame_to_playback(const PacketEntry& e) {
    // raw_bytes = 纯 MPDU(串口/回放/裸 hex 导入均已在解析层剥去封装头)
    const QByteArray mpdu = e.raw_bytes;
    if (mpdu.isEmpty()) return {};

    // dlen = 数据长度字段(2B LE,反转义后偏移 0-1)。定义:从 phr_mcs 字段
    // (偏移 6)到帧末的字节数 = 物理元数据 4B + MPDU 长(与固件帧 dlen=MPDU+4
    // 一致)。本监控器读取端不校验该字段,仅按 0x3C/0x3E 切帧。
    // ts 域 = epoch ms 低 32 位(导出时刻);绝对时刻由 8B BCD 时间标签承载
    const quint16 dlen = quint16(mpdu.size() + 4);
    const quint32 ts   = quint32(e.epoch_ms & 0xFFFFFFFFu);
    QByteArray data;
    data.reserve(mpdu.size() + 18);
    // 起始(帧)时间标签:回放时据此恢复原始捕获时刻
    data.append(bcd_time_tag(e.epoch_ms));
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
/// @details 文件**头部先写一次 8B BCD 时间标注**(首帧本地时刻,独立于帧,
///          不参与 0x3C/0x3E 切帧),随后各帧 = 实时原始帧 raw_wire 逐字节
///          原样(0x3C...0x3E),与实时捕获完全一致,无任何逐帧附加值。
///          无 raw_wire 的旧数据回退到 frame_to_playback(重组+每帧 BCD)。
inline QByteArray build_playback_bin(const QVector<PacketEntry>& entries) {
    QByteArray buf;
    buf.reserve(entries.size() * 72);
    if (entries.isEmpty()) return buf;
    // 文件头 8B BCD 时间标注(首帧本地时刻;读取端据此 seek 跳过)
    buf.append(bcd_time_tag(entries.first().epoch_ms));
    for (const PacketEntry& e : entries) {
        if (!e.raw_wire.isEmpty()) {
            buf.append(e.raw_wire);   // 逐字节原样
        } else {
            const QByteArray fr = frame_to_playback(e);
            if (!fr.isEmpty()) buf.append(fr);
        }
    }
    return buf;
}

/// @brief 导出裸数据 hex 文本(与 RawHex 导入对称,每行一完整 0x3C 原始帧):
///        [0x3C][esc(data)][0x3E],data = [dlen 2B][ts 4B=epoch ms 低32]
///        [phr_mcs][option][channel][isRF][MPDU]。
///        回放端识别 0x3C 行 → 反转义还原 data;ts 还原每帧捕获时刻(与 bin
///        同构,仅无 8B BCD 标签;时间精度同文件毫秒)。
inline QByteArray build_raw_hex_text(const QVector<PacketEntry>& entries) {
    QByteArray buf;
    if (entries.isEmpty()) return buf;
    // 首帧显式本地时间戳头(首次本地时间戳机制):TIME: yyyy-MM-dd HH:mm:ss.zzz
    const QDateTime first = QDateTime::fromMSecsSinceEpoch(entries.first().epoch_ms);
    buf.append(QStringLiteral("TIME: %1\n")
                   .arg(first.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                   .toUtf8());
    for (const PacketEntry& e : entries) {
        if (e.raw_bytes.isEmpty()) continue;
        const QByteArray mpdu = e.raw_bytes;
        const quint16 dlen = quint16(mpdu.size() + 4);
        const quint32 ts   = quint32(e.epoch_ms & 0xFFFFFFFFu);
        QByteArray data;
        data.reserve(mpdu.size() + 10);
        data.append(char(dlen & 0xFF)).append(char(dlen >> 8));
        data.append(char(ts & 0xFF)).append(char((ts >> 8) & 0xFF))
            .append(char((ts >> 16) & 0xFF)).append(char((ts >> 24) & 0xFF));
        data.append(char(e.meta.phr_mcs)).append(char(e.meta.option))
            .append(char(e.meta.channel)).append(char(e.meta.is_rf ? 1 : 0));
        data.append(mpdu);
        // 0x3C + 转义(data) + 0x3E
        QByteArray frame;
        frame.reserve(data.size() + 2);
        frame.append(char(0x3C));
        frame.append(escape_frame_data(data));
        frame.append(char(0x3E));
        QByteArray line;
        for (char c : frame)
            line += QStringLiteral(" 0x%1")
                        .arg(quint8(c), 2, 16, QChar('0')).toLatin1();
        buf.append(line.mid(1));   // 去掉行首空格
        buf.append('\n');
    }
    return buf;
}

}  // namespace playback

#endif // PLAYBACKWRITER_H
