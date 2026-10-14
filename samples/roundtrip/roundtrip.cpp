/// @file roundtrip.cpp
/// @brief 实际 ReaderWorker + 批量/流式导出器的 bin↔hex 无损互转验证。
#include <QCoreApplication>
#include <QBuffer>
#include <QDebug>
#include <QTemporaryDir>
#include "serialreader.h"
#include "playbackwriter.h"
#include "packetentry_serialize.h"

namespace {
int failures = 0;

void check(bool ok, const QString& name) {
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name;
    if (!ok) ++failures;
}

QVector<PacketEntry> read(const QByteArray& input, ReaderMode mode) {
    QTemporaryDir dir;
    QFile file(dir.filePath("input"));
    if (!file.open(QIODevice::WriteOnly) || file.write(input) != input.size()) {
        check(false, "create replay input");
        return {};
    }
    file.close();
    ReaderConfig cfg;
    cfg.mode = mode;
    cfg.file_path = file.fileName();
    QVector<PacketEntry> entries;
    ReaderWorker worker;
    QObject::connect(&worker, &ReaderWorker::error_occurred,
                     [](const QString& error) { check(false, error); });
    QObject::connect(&worker, &ReaderWorker::frame_ready, [&](const BplcFrame& frame) {
        PacketEntry e;
        e.meta = frame.meta;
        e.raw_wire = frame.raw_wire;
        e.raw_bytes = frame.data.mid(10);
        e.epoch_ms = frame.meta.frame_time.toMSecsSinceEpoch();
        entries.append(e);
    });
    worker.start_reading(cfg);
    return entries;
}

QByteArray wire(quint32 ntb, int padding = 0) {
    QByteArray data = QByteArray::fromHex("14000000000000000200");
    const quint16 dlen = quint16(20 + padding);
    data[0] = char(dlen & 0xFF);
    data[1] = char(dlen >> 8);
    for (int i = 0; i < 4; ++i) data[2 + i] = char(ntb >> (8 * i));
    data += QByteArray::fromHex("02d5a1cd10013000012b0000009f0e21");
    data += QByteArray(padding, char(0xAA));
    return QByteArray(1, char(0x3C)) + playback::escape_frame_data(data) + char(0x3E);
}

QByteArray time_line(qint64 ms) {
    return "TIME: " + QDateTime::fromMSecsSinceEpoch(ms)
                             .toString("yyyy-MM-dd HH:mm:ss.zzz").toUtf8() + '\n';
}

QByteArray streamed(const QVector<PacketEntry>& entries, bool hex) {
    QByteArray bytes;
    QBuffer out(&bytes);
    out.open(QIODevice::WriteOnly);
    if (hex) {
        playback::RawHexWriter writer(&out);
        for (const auto& e : entries) writer.add(e);
    } else {
        playback::PlaybackBinWriter writer(&out);
        for (const auto& e : entries) writer.add(e);
    }
    return bytes;
}

void bin_roundtrip(const QByteArray& input, const QVector<PacketEntry>& entries) {
    check(playback::build_playback_bin(entries) == input, "bin playback -> bin equals source");
    check(streamed(entries, false) == input, "streaming bin export equals source");
    const QByteArray hex = playback::build_raw_hex_text(entries);
    check(streamed(entries, true) == hex, "streaming and batch hex exports agree");
    const auto reread = read(hex, ReaderMode::RawHex);
    check(reread.size() == entries.size(), "hex replay preserves frame count");
    check(playback::build_playback_bin(reread) == input, "bin -> hex -> bin equals source bytes");
    check(streamed(reread, false) == input, "bin -> hex -> streaming bin equals source bytes");
}

void regressions() {
    const qint64 base = QDateTime(QDate(2026, 9, 7), QTime(18, 43, 0, 123)).toMSecsSinceEpoch();
    const qint64 later = base + 600000;
    const QByteArray first = wire(1000);
    const QByteArray second = wire(26000);
    const QByteArray tag = playback::bcd_time_tag(base);
    const QByteArray tag2 = playback::bcd_time_tag(later);

    for (int split = 0; split <= 8; ++split) {
        qInfo() << "BCD bytes before read boundary:" << split;
        QByteArray input = tag;
        const int target = 8 + 1024 * 1024 - split;
        quint32 ntb = 1000;
        QByteArray last;
        while (input.size() + wire(ntb).size() <= target) {
            last = wire(ntb);
            input += last;
            ntb += 25000;
        }
        // 延长最后一帧,精确对齐标注,文件不混入会被读取器丢弃的噪声。
        const int padding = target - input.size();
        input.chop(last.size());
        last = wire(ntb - 25000, padding);
        input += last + tag2 + wire(ntb);
        const auto entries = read(input, ReaderMode::FilePlayback);
        check(!entries.isEmpty() && entries.last().meta.seg_start
                  && entries.last().epoch_ms == later, "boundary BCD resets segment time");
        bin_roundtrip(input, entries);
    }

    for (bool bcd : {false, true}) {
        const QByteArray input = (bcd ? playback::hex_line_of(tag) : time_line(base))
                                 + playback::hex_line_of(first)
                                 + (bcd ? playback::hex_line_of(tag2) : time_line(later))
                                 + playback::hex_line_of(second)
                                 + playback::hex_line_of(wire(51000));
        const auto entries = read(input, ReaderMode::RawHex);
        check(entries.size() == 3 && entries[0].epoch_ms == base
                  && entries[1].epoch_ms == later && entries[1].meta.seg_start
                  && entries[2].epoch_ms == later + 1 && !entries[2].meta.seg_start,
              bcd ? "standalone BCD hex resets next frame" : "TIME header resets next frame");
        const QByteArray bin = tag + first + tag2 + second + wire(51000);
        check(streamed(entries, false) == bin, "hex -> bin retains annotation time and position");
        bin_roundtrip(bin, read(bin, ReaderMode::FilePlayback));
        QVector<PacketEntry> stored;
        check(pser::decode_block(pser::encode_block(entries), stored)
                  && stored.size() == 3 && stored[1].meta.seg_start
                  && stored[1].epoch_ms == later && streamed(stored, false) == bin,
              "segment and export survive disk-block round-trip");
    }

    for (ReaderMode mode : {ReaderMode::FilePlayback, ReaderMode::RawHex}) {
        const bool hex = mode == ReaderMode::RawHex;
        const QByteArray gap = wire(1000 + 61u * 25000000u);
        const QByteArray input = hex ? time_line(base) + playback::hex_line_of(first)
                                                        + playback::hex_line_of(gap)
                                    : tag + first + gap;
        const qint64 before = QDateTime::currentMSecsSinceEpoch();
        const auto entries = read(input, mode);
        const qint64 after = QDateTime::currentMSecsSinceEpoch();
        check(entries.size() == 2 && !entries[1].meta.seg_start
                  && entries[1].epoch_ms >= before && entries[1].epoch_ms <= after,
              "missing intermediate annotation uses local time without seg_start");
        check(streamed(entries, hex) == input, "fallback does not invent export annotation");
        const QByteArray bin = tag + first + gap;
        bin_roundtrip(bin, entries);
        if (entries.size() != 2) continue;
        auto extra = entries;
        extra[1].meta.seg_start = true;
        check(streamed(extra, hex) != input, "adding seg_start at fallback changes exported file");
    }

    const QByteArray invalid_tag = QByteArray::fromHex("2602311843000000");
    const auto invalid = read(time_line(base) + playback::hex_line_of(first)
                                 + playback::hex_line_of(invalid_tag)
                                 + playback::hex_line_of(second), ReaderMode::RawHex);
    check(invalid.size() == 2 && invalid.last().epoch_ms == base + 1
              && !invalid.last().meta.seg_start, "invalid BCD date does not reset segment");
}

void fixtures(const QString& bin_path, const QString& hex_path) {
    QFile bin(bin_path), hex(hex_path);
    if (!bin.open(QIODevice::ReadOnly) || !hex.open(QIODevice::ReadOnly)) {
        check(false, "open fixture files");
        return;
    }
    const QByteArray src_bin = bin.readAll();
    QByteArray src_hex = hex.readAll();
    bin_roundtrip(src_bin, read(src_bin, ReaderMode::FilePlayback));
    const auto entries = read(src_hex, ReaderMode::RawHex);
    const auto reread = read(streamed(entries, false), ReaderMode::FilePlayback);
    src_hex.replace("\r\n", "\n");
    check(streamed(reread, true) == src_hex, "canonical fixture hex -> bin -> hex");
}
}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    regressions();
    if (argc == 3) fixtures(app.arguments()[1], app.arguments()[2]);
    else if (argc != 1) { qWarning() << "usage: roundtrip [bin txt]"; return 2; }
    qInfo() << "RESULT:" << (failures == 0 ? "PASS" : "FAIL") << "failures:" << failures;
    return failures == 0 ? 0 : 1;
}
