/// @file roundtrip.cpp
/// @brief 导出/导入 round-trip 无损验证:
///        正向  A.bin → 导出裸hex → 导入裸hex → 导出 B.bin → 比对 B.bin == A.bin
///        反向  A.txt → 导出bin → 导入bin → 导出 B.txt → 比对 B.txt == A.txt
/// @note  与 serialreader 同构的切帧/断段逻辑,但 NTB 异常断点的时刻用上一帧
///        时刻(确定性)代替 currentMSecsSinceEpoch——round-trip 需确定性,否则
///        两次解析的本地时刻不同会导致 8B BCD/TIME 头字节不一致(假失败)。
#include <QCoreApplication>
#include <QFile>
#include <QBuffer>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QRegularExpression>
#include <cstdio>
#include "bplcframe.h"
#include "playbackwriter.h"

static qint64 bcd_ms_of(const QByteArray& b) {
    if (!playback::looks_like_bcd8(b)) return -1;
    auto d2 = [](quint8 x) { return int((x >> 4) * 10 + (x & 0x0F)); };
    const QDate date(2000 + d2(quint8(b[0])), d2(quint8(b[1])), d2(quint8(b[2])));
    const QTime time(d2(quint8(b[3])), d2(quint8(b[4])), d2(quint8(b[5])));
    if (!date.isValid() || !time.isValid()) return -1;
    return QDateTime(date, time).addMSecs(d2(quint8(b[6])) * 100 + d2(quint8(b[7])))
        .toMSecsSinceEpoch();
}

static qint64 parse_time_header_ms(const QByteArray& line) {
    static const QRegularExpression re(
        QStringLiteral("(\\d{4}-\\d{2}-\\d{2})[ T](\\d{2}):(\\d{2}):(\\d{2})(?:\\.(\\d{1,3}))?"));
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(line));
    if (!m.hasMatch()) return -1;
    const QDate date = QDate::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd"));
    const QTime time(m.captured(2).toInt(), m.captured(3).toInt(),
                     m.captured(4).toInt(),
                     m.captured(5).isEmpty() ? 0
                                             : m.captured(5).leftJustified(3, '0').toInt());
    if (!date.isValid() || !time.isValid()) return -1;
    return QDateTime(date, time).toMSecsSinceEpoch();
}

static quint32 read_ntb(const QByteArray& unesc) {
    if (unesc.size() < 6) return 0;
    return (quint32)(quint8)unesc[2] | ((quint32)(quint8)unesc[3] << 8)
         | ((quint32)(quint8)unesc[4] << 16) | ((quint32)(quint8)unesc[5] << 24);
}

// 反转义 body(0x3D XX → 0xFF-XX)
static QByteArray unescape(const QByteArray& body) {
    QByteArray out;
    for (int i = 0; i < body.size(); ++i) {
        quint8 b = (quint8)body[i];
        if (b == 0x3D && i + 1 < body.size()) {
            ++i;
            out.append(char(0xFF - (quint8)body[i]));
        } else {
            out.append(char(b));
        }
    }
    return out;
}

// 解析 bin:文件头/段间 8B BCD 标注 + 0x3C 帧流 → PacketEntry 序列
static QVector<PacketEntry> parse_bin(const QByteArray& data) {
    QVector<PacketEntry> out;
    int pos = 0;
    qint64 base_ms = -1;
    bool first = false;
    quint32 last_ntb = 0;
    qint64 last_ft = 0;
    if (data.size() >= 8 && playback::looks_like_bcd8(data)) {
        base_ms = bcd_ms_of(data.left(8));
        pos = 8;
        first = true;
    }
    while (pos < data.size()) {
        if (data.size() - pos >= 8 && (quint8)data[pos] != 0x3C
            && playback::looks_like_bcd8(data.mid(pos, 8))) {
            base_ms = bcd_ms_of(data.mid(pos, 8));
            first = true;
            pos += 8;
            continue;
        }
        if ((quint8)data[pos] != 0x3C) { ++pos; continue; }
        int idx = data.indexOf(char(0x3E), pos + 1);
        if (idx < 0) break;
        QByteArray body = data.mid(pos + 1, idx - pos - 1);
        QByteArray wire;
        wire.append(char(0x3C)).append(body).append(char(0x3E));
        QByteArray unesc = unescape(body);
        const quint32 ntb = read_ntb(unesc);
        qint64 arrival;
        bool seg = false;
        if (first) {
            arrival = base_ms;
            seg = true;
            first = false;
        } else {
            const qint64 dn = (qint32)(ntb - last_ntb);
            if (dn > 0 && dn <= playback::kMaxNtbGapTicks)
                arrival = last_ft + playback::ntb_to_us(quint32(dn)) / 1000;
            else
                arrival = last_ft;   // 断点:确定性用上一帧时刻(测试专用)
        }
        last_ntb = ntb;
        last_ft = arrival;
        PacketEntry e;
        e.raw_wire = wire;
        e.raw_bytes = unesc;
        e.meta.seg_start = seg;
        e.meta.timestamp = ntb;
        e.epoch_ms = arrival;
        out.append(e);
        pos = idx + 1;
    }
    return out;
}

// 解析裸hex:TIME 头行 + 每行 0x3C 帧 → PacketEntry 序列
static QVector<PacketEntry> parse_rawhex(const QByteArray& data) {
    QVector<PacketEntry> out;
    qint64 base_ms = -1;
    bool first = false;
    quint32 last_ntb = 0;
    qint64 last_ft = 0;
    const QList<QByteArray> lines = data.split('\n');
    for (QByteArray line0 : lines) {
        QByteArray line = line0.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("TIME:")) {
            const qint64 ms = parse_time_header_ms(line);
            if (ms >= 0) { base_ms = ms; first = true; }
            continue;
        }
        // 解析 hex 字节(支持 "0x3c 0x9c ..." / "3c 9c ...")
        QByteArray raw;
        QByteArray s = line;
        const QList<QByteArray> toks = s.replace(',', ' ').split(' ');
        for (QByteArray t0 : toks) {
            QByteArray t = t0.trimmed();
            if (t.isEmpty()) continue;
            if ((t.startsWith("0x") || t.startsWith("0X")) && t.size() >= 3)
                t.remove(0, 2);
            if (t.size() < 2 || (t.size() % 2) != 0) { raw.clear(); break; }
            bool ok = true;
            for (int i = 0; i < t.size(); i += 2) {
                int v = t.mid(i, 2).toInt(&ok, 16);
                if (!ok) break;
                raw.append(char(v));
            }
            if (!ok) { raw.clear(); break; }
        }
        if (raw.size() < 9) continue;
        if ((quint8)raw[0] != 0x3C || (quint8)raw[raw.size() - 1] != 0x3E) continue;
        QByteArray body = raw.mid(1, raw.size() - 2);
        QByteArray unesc = unescape(body);
        const quint32 ntb = read_ntb(unesc);
        qint64 arrival;
        bool seg = false;
        if (first) {
            arrival = base_ms;
            seg = true;
            first = false;
        } else {
            const qint64 dn = (qint32)(ntb - last_ntb);
            if (dn > 0 && dn <= playback::kMaxNtbGapTicks)
                arrival = last_ft + playback::ntb_to_us(quint32(dn)) / 1000;
            else
                arrival = last_ft;   // 断点:确定性用上一帧时刻(测试专用)
        }
        last_ntb = ntb;
        last_ft = arrival;
        PacketEntry e;
        e.raw_wire = raw;
        e.raw_bytes = unesc;
        e.meta.seg_start = seg;
        e.meta.timestamp = ntb;
        e.epoch_ms = arrival;
        out.append(e);
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 3) { std::printf("usage: roundtrip <bin> <txt>\n"); return 2; }
    int fails = 0;

    // ---- 正向: bin → 裸hex → bin,比对 == 源 bin ----
    {
        QFile fbin(QString::fromLocal8Bit(argv[1]));
        if (!fbin.open(QIODevice::ReadOnly)) { std::printf("open bin fail\n"); return 2; }
        const QByteArray src_bin = fbin.readAll();
        fbin.close();
        const QVector<PacketEntry> e1 = parse_bin(src_bin);
        const QByteArray txt = playback::build_raw_hex_text(e1);
        const QVector<PacketEntry> e2 = parse_rawhex(txt);
        const QByteArray bin2 = playback::build_playback_bin(e2);
        const bool ok = (bin2 == src_bin);
        std::printf("正向 bin→裸hex→bin: 帧 %d,裸hex %d B,bin %d B vs 源 %d B → %s\n",
                    int(e1.size()), txt.size(), bin2.size(), src_bin.size(),
                    ok ? "一致" : "不一致");
        if (!ok) {
            ++fails;
            const int n = qMin(bin2.size(), src_bin.size());
            for (int i = 0; i < n; ++i)
                if (bin2[i] != src_bin[i]) {
                    std::printf("  首差异 @%d (bin2=%02x src=%02x)\n",
                                i, (quint8)bin2[i], (quint8)src_bin[i]); break;
                }
        }
    }

    // ---- 反向: 裸hex → bin → 裸hex,比对 == 源裸hex ----
    {
        QFile ftxt(QString::fromLocal8Bit(argv[2]));
        if (!ftxt.open(QIODevice::ReadOnly)) { std::printf("open txt fail\n"); return 2; }
        const QByteArray src_txt_raw = ftxt.readAll();
        ftxt.close();
        // 规范化换行:git autocrlf 可能把仓库里的 txt 检出成 CRLF,
        // 统一转 LF 再比对,避免 CRLF/LF 差异造成假失败
        QByteArray src_txt = src_txt_raw;
        src_txt.replace("\r\n", "\n");
        src_txt.replace('\r', '\n');
        const QVector<PacketEntry> e1 = parse_rawhex(src_txt);
        const QByteArray bin = playback::build_playback_bin(e1);
        const QVector<PacketEntry> e2 = parse_bin(bin);
        const QByteArray txt2 = playback::build_raw_hex_text(e2);
        const bool ok = (txt2 == src_txt);
        std::printf("反向 裸hex→bin→裸hex: 帧 %d,bin %d B,裸hex %d B vs 源 %d B → %s\n",
                    int(e1.size()), bin.size(), txt2.size(), src_txt.size(),
                    ok ? "一致" : "不一致");
        if (!ok) {
            ++fails;
            const int n = qMin(txt2.size(), src_txt.size());
            for (int i = 0; i < n; ++i)
                if (txt2[i] != src_txt[i]) {
                    std::printf("  首差异 @%d (txt2=%02x src=%02x)\n",
                                i, (quint8)txt2[i], (quint8)src_txt[i]); break;
                }
        }
    }

    std::printf(fails == 0 ? "PASS\n" : "FAIL(fail=%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
