/// @file reader_envelope_probe.cpp
/// @brief A-6 回归:Reader 两处构造点(3C 回放 bin / 裸 hex)必填信封头字段
///        (epoch_ms/timestamp/phr_mcs/option/channel/is_rf),与协议解析器
///        decode_envelope 的复核值恒等(同字节同公式覆盖)。
/// @details 旧实现好帧路径只填 frame_time/local_rx_ms/frame_ts_is_ntb/
///          has_time_tag,host 侧插件看到的 meta.timestamp/channel/is_rf
///          全是 0/默认值。本探针用 3 帧合成 BEACON(FCH CRC24 有效)走真实
///          ReaderWorker(文件回放 + 裸 hex),再经同一 GW_2022_Parser 解析,
///          断言 reader 值 = 构造值(非默认值)且 = 解析器复核值。
///          NTB 步进 25000 tick(=1 ms):reader 时间链与解析器锚点双边
///          整除,无截断漂移;首帧 replay_time_tag 使锚点直接取 reader 时刻。
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>
#include <cstdio>
#include "serialreader.h"
#include "playbackwriter.h"
#include "gw_2022_parser.h"
#include "crc.h"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

/// 回放/裸 hex 共用的时间基准:2026-11-18 12:00:00.000(本地时间)
static qint64 base_ms() {
    return QDateTime(QDate(2026, 11, 18), QTime(12, 0, 0)).toMSecsSinceEpoch();
}

/// 3 帧测试向量(NTB 步进 25000 tick = 1 ms;媒介头各值互不相同且非 0)
struct TestFrameSpec {
    quint32 ntb;
    quint8  phr_mcs;
    quint8  option;
    quint8  channel;
    bool    is_rf;
};
static const TestFrameSpec kFrames[] = {
    {100000, 0x11, 0x22, 2, false},
    {125000, 0x33, 0x44, 5, true},
    {150000, 0x00, 0x01, 0, false},
};
static const int kFrameCount = 3;

/// 合成一帧 BEACON 的线上字节:0x3C + 转义 data + 0x3E,
/// data = [dlen 2B][NTB 4B][phr_mcs][option][channel][isRF][MPDU],
/// MPDU = 16B FCH(BEACON,CRC24 有效) + 32B 零载荷。
/// dlen 定义与 playbackwriter.h::frame_to_playback 一致:MPDU 长 + 4。
static QByteArray build_wire(const TestFrameSpec& spec) {
    QByteArray fch(16, char(0));
    fch[1] = char(0x56);   // net_id 0x123456 LE
    fch[2] = char(0x34);
    fch[3] = char(0x12);
    fch[8] = char(0x01);   // src_tei = 1(CCO)
    const quint32 crc = crc24_lsb(reinterpret_cast<const quint8*>(fch.constData()), 16);
    fch[13] = char(crc & 0xFF);
    fch[14] = char((crc >> 8) & 0xFF);
    fch[15] = char((crc >> 16) & 0xFF);
    const QByteArray mpdu = fch + QByteArray(32, char(0));

    QByteArray data;
    data.reserve(mpdu.size() + 10);
    const quint16 dlen = quint16(mpdu.size() + 4);
    data.append(char(dlen & 0xFF)).append(char(dlen >> 8));
    data.append(char(spec.ntb & 0xFF)).append(char((spec.ntb >> 8) & 0xFF))
        .append(char((spec.ntb >> 16) & 0xFF)).append(char((spec.ntb >> 24) & 0xFF));
    data.append(char(spec.phr_mcs)).append(char(spec.option))
        .append(char(spec.channel)).append(char(spec.is_rf ? 1 : 0));
    data.append(mpdu);

    QByteArray wire;
    wire.append(char(0x3C));
    wire.append(playback::escape_frame_data(data));
    wire.append(char(0x3E));
    return wire;
}

/// 走真实 ReaderWorker 读回放文件,收帧后逐帧过解析器,断言 5 组元数据恒等
/// (reader 值 = 构造值 = 解析器复核值;epoch_ms = 基准 + i ms)。
static void check_run(const QVector<BplcFrame>& frames, const char* tag) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s: reader 切出 3 帧", tag);
    check(frames.size() == kFrameCount, buf);
    if (frames.size() != kFrameCount) return;

    GW_2022_Parser parser;   // 同一实例连续解析,锚点链与生产一致
    MsduState msdu;
    const ParseFilter filter;
    for (int i = 0; i < kFrameCount; ++i) {
        const BplcFrame& f = frames[i];
        const TestFrameSpec& spec = kFrames[i];
        const qint64 want_epoch = base_ms() + i;   // NTB 步进 1 ms/帧
        std::snprintf(buf, sizeof(buf), "%s: frame %d 被 reader 接受", tag, i);
        check(f.accepted && f.error_reason.isEmpty(), buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader timestamp=%u",
                      tag, i, spec.ntb);
        check(f.meta.timestamp == spec.ntb, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader channel=%u",
                      tag, i, spec.channel);
        check(f.meta.channel == spec.channel, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader is_rf=%d",
                      tag, i, int(spec.is_rf));
        check(f.meta.is_rf == spec.is_rf, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader phr_mcs/option=%02x/%02x",
                      tag, i, spec.phr_mcs, spec.option);
        check(f.meta.phr_mcs == spec.phr_mcs && f.meta.option == spec.option, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader epoch_ms=基准+%dms",
                      tag, i, i);
        check(f.meta.epoch_ms == want_epoch, buf);

        const ParseResult r = parser.parse(f, msdu, filter);
        std::snprintf(buf, sizeof(buf), "%s: frame %d 解析器接受", tag, i);
        check(r.accept, buf);
        if (!r.accept) continue;
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader NTB == parser NTB", tag, i);
        check(f.meta.timestamp == r.meta.timestamp, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader channel == parser channel",
                      tag, i);
        check(f.meta.channel == r.meta.channel, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader is_rf == parser is_rf", tag, i);
        check(f.meta.is_rf == r.meta.is_rf, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader phr_mcs/option == parser",
                      tag, i);
        check(f.meta.phr_mcs == r.meta.phr_mcs && f.meta.option == r.meta.option, buf);
        std::snprintf(buf, sizeof(buf), "%s: frame %d reader epoch_ms == parser epoch_ms",
                      tag, i);
        check(f.meta.epoch_ms == r.meta.epoch_ms, buf);
    }
}

static void run_3c() {
    QTemporaryDir dir;
    QFile f(dir.filePath("in.bin"));
    if (!f.open(QIODevice::WriteOnly)) { check(false, "3c: 临时 bin 写不开"); return; }
    f.write(playback::bcd_time_tag(base_ms()));   // 文件头 8B BCD 标注
    for (int i = 0; i < kFrameCount; ++i) f.write(build_wire(kFrames[i]));
    f.close();

    ReaderWorker w;
    QVector<BplcFrame> frames;
    QObject::connect(&w, &ReaderWorker::frame_ready,
                     [&](const BplcFrame& fr, quint64) { frames.append(fr); });
    ReaderConfig cfg;
    cfg.mode = ReaderMode::FilePlayback;
    cfg.file_path = f.fileName();
    w.start_reading(cfg);
    check_run(frames, "3c");
}

static void run_raw_hex() {
    QTemporaryDir dir;
    QFile f(dir.filePath("in.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        check(false, "rawhex: 临时 txt 写不开");
        return;
    }
    f.write("TIME: 2026-11-18 12:00:00.000\n");
    for (int i = 0; i < kFrameCount; ++i) f.write(playback::hex_line_of(build_wire(kFrames[i])));
    f.close();

    ReaderWorker w;
    QVector<BplcFrame> frames;
    QObject::connect(&w, &ReaderWorker::frame_ready,
                     [&](const BplcFrame& fr, quint64) { frames.append(fr); });
    ReaderConfig cfg;
    cfg.mode = ReaderMode::RawHex;
    cfg.file_path = f.fileName();
    w.start_reading(cfg);
    check_run(frames, "rawhex");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    run_3c();
    run_raw_hex();
    if (g_fail) {
        std::printf("reader_envelope_probe: FAILURES (%d failures)\n", g_fail);
        return 1;
    }
    std::printf("RESULT: ALL PASS (0 failures)\n");
    return 0;
}
