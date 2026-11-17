/// @file reader_garbage_probe.cpp
/// @brief M5 回归:回放/串口切帧遇大段无 0x3C 数据时线性丢弃;BCD 段标注语义不变;abort 块内生效
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>
#include <cstdio>
#include "serialreader.h"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

/// 一段超长帧:0x3C + 5000×0x41 [+ 0x3E];每段在 :362(闭合)/:353(未闭合) 产生一条超长 DROP
static QByteArray overlong_block(bool closed = true) {
    QByteArray b(1, char(0x3C));
    b.append(QByteArray(5000, char(0x41)));
    if (closed) b.append(char(0x3E));
    return b;
}

/// 随机垃圾(无 0x3C);no_bcd=true 时每字节高半字节 ≥ 0xA,保证其中不会偶然出现合法 BCD 标注
static QByteArray garbage(int n, quint32 seed, bool no_bcd = false) {
    QRandomGenerator rng(seed);
    QByteArray g(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i) {
        char c = char(no_bcd ? 0xA0 + rng.bounded(0x60) : rng.bounded(256));
        if (c == char(0x3C)) c = 0;
        g[i] = c;
    }
    return g;
}

/// 2026-11-11 08:30:15.250 的 8B BCD 段标注
static QByteArray bcd_tag() {
    return QByteArray::fromHex("2611110830150250");
}

class ReaderWorkerTestAccess {
public:
    struct Run { int frames; qint64 first_epoch; qint64 ms; int retained; };
    static Run file(const QByteArray& input) {
        QTemporaryDir dir;
        QFile f(dir.filePath("in.bin"));
        f.open(QIODevice::WriteOnly); f.write(input); f.close();
        ReaderWorker w;
        ReaderConfig cfg;
        cfg.mode = ReaderMode::FilePlayback;
        cfg.file_path = f.fileName();
        Run r{0, -1, 0, 0};
        QObject::connect(&w, &ReaderWorker::frame_ready, [&](const BplcFrame& fr) {
            if (r.frames++ == 0) r.first_epoch = fr.meta.frame_time.toMSecsSinceEpoch();
        });
        QElapsedTimer t; t.start();
        w.start_reading(cfg);
        r.ms = t.elapsed();
        return r;
    }
    static Run serial(const QByteArray& input) {
        ReaderWorker w;
        w.m_cfg.mode = ReaderMode::SerialPort;
        Run r{0, -1, 0, 0};
        QObject::connect(&w, &ReaderWorker::frame_ready, [&](const BplcFrame&) { ++r.frames; });
        QElapsedTimer t; t.start();
        w.m_in_buf = input;
        w.try_extract_frame();
        r.ms = t.elapsed();
        r.retained = w.m_in_buf.size();
        return r;
    }
    /// 同一 worker 分批喂超长块,批间可隔 gap_ms;返回发出的 DROP 数、逐条原因与计数
    struct Burst {
        int drops = 0;
        QStringList reasons;
        QByteArray last_wire;
        qint64 frame_count = 0;
    };
    static Burst overlong_bursts(const QVector<int>& counts, int gap_ms = 0,
                                 bool closed = true) {
        ReaderWorker w;
        w.m_cfg.mode = ReaderMode::SerialPort;
        Burst b;
        QObject::connect(&w, &ReaderWorker::frame_ready, [&](const BplcFrame& fr) {
            if (!fr.accepted) {
                ++b.drops;
                b.reasons << fr.error_reason;
                b.last_wire = fr.raw_wire;
            }
        });
        for (int i = 0; i < counts.size(); ++i) {
            for (int k = 0; k < counts[i]; ++k) {
                w.m_in_buf = overlong_block(closed);
                w.try_extract_frame();
            }
            if (gap_ms > 0 && i + 1 < counts.size()) QThread::msleep(gap_ms);
        }
        b.frame_count = w.m_frame_count;
        // 析构走 stop_reading() 会把剩余抑制计数补发成汇总行;本用例只统计喂数据期间的行
        QObject::disconnect(&w, &ReaderWorker::frame_ready, nullptr, nullptr);
        return b;
    }
    /// N 个被抑制的超长帧后 stop_reading():返回 stop 期间发出的行
    /// (先发 1 条未抑制 DROP,再 N 条被抑制;缓冲在 stop 前已清空)。
    /// simulate_gui_stop=true 时按 SerialReader::stop 的顺序先 abort() 并挂上背压额度,
    /// 验证中止后的汇总行仍能发出。
    struct StopFlush {
        int rows_before = 0;
        int rows_on_stop = 0;
        QStringList stop_reasons;
        QByteArray stop_wire;
        int in_buf_at_stop = -1;
        qint64 frame_count_before = 0;
        qint64 frame_count_after = 0;
        int suppressed_after = -1;
        qint64 last_us_after = -1;
        int rows_on_second_stop = 0;
    };
    static StopFlush suppressed_then_stop(int n, bool simulate_gui_stop = false) {
        ReaderWorker w;
        w.m_cfg.mode = ReaderMode::SerialPort;
        std::shared_ptr<FrameCredit> credit;
        if (simulate_gui_stop) {
            credit = std::make_shared<FrameCredit>();
            credit->set_enabled(true);
            w.set_flow_credit(credit);
            w.m_run_session = w.begin_session();
        }
        StopFlush r;
        bool stopping = false;
        int second = 0;
        QObject::connect(&w, &ReaderWorker::frame_ready, [&](const BplcFrame& fr) {
            if (fr.accepted) return;
            if (stopping == false) { ++r.rows_before; return; }
            if (second) { ++r.rows_on_second_stop; return; }
            ++r.rows_on_stop;
            r.stop_reasons << fr.error_reason;
            r.stop_wire = fr.raw_wire;
        });
        for (int k = 0; k < n + 1; ++k) {
            w.m_in_buf = overlong_block(true);
            w.try_extract_frame();
        }
        r.in_buf_at_stop = int(w.m_in_buf.size());
        r.frame_count_before = w.m_frame_count;
        if (simulate_gui_stop) w.abort();
        stopping = true;
        w.stop_reading();
        r.frame_count_after = w.m_frame_count;
        r.suppressed_after = w.m_overlong_drop_suppressed;
        r.last_us_after = w.m_last_overlong_drop_us;
        second = 1;
        w.stop_reading();   // 再停一次:计数已清零,不得再发
        QObject::disconnect(&w, &ReaderWorker::frame_ready, nullptr, nullptr);
        return r;
    }
    /// 回放文件 = N+1 个闭合超长块后 EOF:返回全部 DROP 原因
    static QStringList overlong_playback(int n) {
        QByteArray input;
        for (int k = 0; k < n + 1; ++k) input += overlong_block(true);
        QTemporaryDir dir;
        QFile f(dir.filePath("overlong.bin"));
        f.open(QIODevice::WriteOnly); f.write(input); f.close();
        ReaderWorker w;
        ReaderConfig cfg;
        cfg.mode = ReaderMode::FilePlayback;
        cfg.file_path = f.fileName();
        QStringList reasons;
        QObject::connect(&w, &ReaderWorker::frame_ready, [&](const BplcFrame& fr) {
            if (!fr.accepted) reasons << fr.error_reason;
        });
        w.start_reading(cfg);
        QObject::disconnect(&w, &ReaderWorker::frame_ready, nullptr, nullptr);
        return reasons;
    }
    static qint64 abort_latency(const QByteArray& input, int delay_ms, int* frames_out) {
        QTemporaryDir dir;
        QFile f(dir.filePath("in.bin"));
        f.open(QIODevice::WriteOnly); f.write(input); f.close();
        ReaderConfig cfg;
        cfg.mode = ReaderMode::FilePlayback;
        cfg.file_path = f.fileName();
        std::atomic<int> frames{0};
        std::atomic<ReaderWorker*> worker{nullptr};
        QThread* th = QThread::create([&]() {
            ReaderWorker w;   // 在回放线程里创建,子对象同线程
            QObject::connect(&w, &ReaderWorker::frame_ready, &w,
                             [&](const BplcFrame&) { ++frames; });
            worker = &w;
            w.start_reading(cfg);
            worker = nullptr;
        });
        th->start();
        while (!worker.load() && !th->isFinished()) QThread::usleep(100);
        QThread::msleep(delay_ms);
        QElapsedTimer t; t.start();
        if (ReaderWorker* w = worker.load()) w->abort();   // 与 SerialReader::stop 同:跨线程直接置标志
        th->wait();
        const qint64 ms = t.elapsed();
        delete th;
        *frames_out = frames.load();
        return ms;
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFile sample(QStringLiteral(BPLC_REPO_DIR "/unit_test/ci/roundtrip/roundtrip_test.bin"));
    if (!sample.open(QIODevice::ReadOnly)) { std::puts("FAIL open roundtrip_test.bin"); return 1; }
    const QByteArray frames = sample.readAll();
    using A = ReaderWorkerTestAccess;

    const A::Run base = A::file(frames);
    std::printf("  baseline: %d frames in %lld ms\n", base.frames, (long long)base.ms);
    check(base.frames > 100, "baseline sample replays");

    // ① 4 MB 无 0x3C 垃圾 + 样本:旧实现每 1 MB 块约 1e12 次比较(数小时),新实现线性
    {
        const A::Run r = A::file(garbage(4 * 1024 * 1024, 1) + frames);
        std::printf("  garbage+sample: %d frames in %lld ms\n", r.frames, (long long)r.ms);
        check(r.ms < 5000, "4 MB garbage replay finishes in < 5 s");
        check(r.frames == base.frames, "all sample frames still extracted after garbage");
    }

    // ② 整块(1 MB 读块)无 0x3C 的垃圾中夹 BCD 段标注:标注仍生效(首帧时间取标注),与逐字节滑窗一致
    {
        const QByteArray body = frames.mid(8);   // 去掉样本自带的 8B 文件头标注
        const A::Run plain = A::file(bcd_tag() + body);
        const A::Run noisy = A::file(garbage(300000, 2) + bcd_tag() + garbage(1200000, 3, true) + body);
        const qint64 tag_ms = QDateTime(QDate(2026, 11, 11), QTime(8, 30, 15, 250)).toMSecsSinceEpoch();
        check(plain.first_epoch == tag_ms, "plain tag sets first frame time");
        std::printf("  first epoch plain=%lld noisy=%lld\n", (long long)plain.first_epoch,
                    (long long)noisy.first_epoch);
        check(plain.first_epoch > 0 && noisy.first_epoch == plain.first_epoch,
              "BCD tag inside garbage still sets the segment time");
        check(noisy.frames == plain.frames, "frame count unchanged with tagged garbage");
    }

    // ③ 串口模式:2 MB 垃圾一次清空
    {
        const A::Run r = A::serial(garbage(2 * 1024 * 1024, 4));
        check(r.ms < 2000 && r.retained == 0 && r.frames == 0, "serial garbage dropped at once");
    }

    // ⑦ L10:超长帧 DROP 限速 1 秒 1 条,抑制数汇总进下一条,计数口径不变
    {
        // 一批 5 个超长块:只发 1 条 DROP,其余 4 条只计数不刷屏
        const A::Burst b = A::overlong_bursts({5});
        std::printf("  overlong burst: %d drops, frame_count=%lld, reason=%s\n",
                    b.drops, (long long)b.frame_count,
                    qPrintable(b.reasons.value(0)));
        check(b.drops == 1, "overlong burst emits at most 1 DROP per second");
        check(b.frame_count == 5, "suppressed overlong frames still counted");
        check(b.reasons.size() == 1 &&
                  (b.reasons[0].contains(QStringLiteral("帧超过最大长度")) ||
                   b.reasons[0].contains(QStringLiteral("exceeds maximum size"))),
              "first DROP keeps original reason");
        check(b.last_wire.size() == 4098 && b.last_wire.startsWith(char(0x3C)) &&
                  b.last_wire.endsWith(char(0x3E)) &&
                  b.last_wire.mid(1, 4096) == QByteArray(4096, char(0x41)),
              "DROP wire is 0x3C + first 4 KiB + 0x3E");
        // 5 块 → 隔 1.1 s → 1 块 → 再隔 1.1 s → 1 块:共 3 条,第 2 条带“另有 4 帧”,第 3 条无后缀
        const A::Burst b2 = A::overlong_bursts({5, 1, 1}, 1100);
        std::printf("  overlong spaced: %d drops, frame_count=%lld\n",
                    b2.drops, (long long)b2.frame_count);
        check(b2.drops == 3, "spaced bursts each emit one DROP");
        check(b2.frame_count == 7, "spaced bursts keep full count");
        check(b2.reasons.size() == 3 &&
                  (b2.reasons[1].contains(QStringLiteral("另有 4 帧")) ||
                   b2.reasons[1].contains(QStringLiteral("4 additional"))),
              "second DROP merges suppressed count");
        check(b2.reasons.size() == 3 &&
                  !b2.reasons[2].contains(QStringLiteral("另有")) &&
                  !b2.reasons[2].contains(QStringLiteral("suppressed")),
              "suppressed count resets after being merged");
        // 未闭合超长帧(:353):连喂 2 段只发 1 条,计数仍为 2
        const A::Burst b3 = A::overlong_bursts({2}, 0, false);
        check(b3.drops == 1 && b3.frame_count == 2,
              "unterminated overlong frames rate-limited too");
        check(b3.reasons.size() == 1 &&
                  (b3.reasons[0].contains(QStringLiteral("未闭合帧超过最大长度")) ||
                   b3.reasons[0].contains(QStringLiteral("unterminated frame"))),
              "unterminated DROP keeps its own reason");
    }

    // ⑧ 被抑制的超长 DROP 在 stop/EOF 时汇总成 1 条:与此刻 m_in_buf 无关(旧实现缓冲 <4 KiB 时直接返回)
    {
        const int N = 7;
        const A::StopFlush s = A::suppressed_then_stop(N);
        std::printf("  stop flush: before=%d on_stop=%d in_buf=%d reason=%s\n",
                    s.rows_before, s.rows_on_stop, s.in_buf_at_stop,
                    qPrintable(s.stop_reasons.value(0)));
        check(s.rows_before == 1, "only the first overlong DROP is emitted before stop");
        check(s.in_buf_at_stop == 0, "input buffer is empty at stop (summary must not depend on it)");
        check(s.rows_on_stop == 1, "N suppressed drops + stop -> exactly one summary row");
        const QString want_zh = QStringLiteral("另有 %1 帧").arg(N);
        const QString want_en = QStringLiteral("%1 additional").arg(N);
        check(s.stop_reasons.size() == 1 &&
                  (s.stop_reasons[0].contains(want_zh) || s.stop_reasons[0].contains(want_en)),
              "summary row carries suppressed count N");
        check(s.stop_wire.size() == 4098 && s.stop_wire.startsWith(char(0x3C)) &&
                  s.stop_wire.endsWith(char(0x3E)) &&
                  s.stop_wire.mid(1, 4096) == QByteArray(4096, char(0x41)),
              "summary row wire is the retained 0x3C + 4 KiB + 0x3E");
        check(s.frame_count_before == N + 1 && s.frame_count_after == N + 1,
              "summary row does not double-count suppressed frames");
        check(s.suppressed_after == 0 && s.last_us_after == 0,
              "stop resets suppressed counter and throttle time");
        check(s.rows_on_second_stop == 0, "second stop emits nothing");

        const A::StopFlush z = A::suppressed_then_stop(0);
        check(z.rows_before == 1 && z.rows_on_stop == 0, "no summary row when nothing was suppressed");

        const A::StopFlush g = A::suppressed_then_stop(N, true);
        check(g.rows_on_stop == 1 && g.stop_reasons.size() == 1 &&
                  (g.stop_reasons[0].contains(want_zh) || g.stop_reasons[0].contains(want_en)),
              "summary row survives abort()+stop_reading with flow credit (SerialReader::stop order)");

        const QStringList p = A::overlong_playback(N);
        std::printf("  playback EOF flush: %d DROP rows, last=%s\n", int(p.size()),
                    qPrintable(p.value(p.size() - 1)));
        check(p.size() == 2 && (p.last().contains(want_zh) || p.last().contains(want_en)),
              "playback EOF emits one summary row with count N");
    }

    // ④ abort 在块内生效(20 份样本 ≈ 4.7 MB 帧数据)
    {
        QByteArray big;
        for (int i = 0; i < 20; ++i) big += frames;
        int got = 0;
        const qint64 ms = A::abort_latency(big, 5, &got);
        std::printf("  abort latency %lld ms, %d frames before stop\n", (long long)ms, got);
        check(ms < 1000, "abort stops replay within 1 s");
    }

    // ⑤ L5:start() 紧跟 stop(),worker 稍后处理 start 也不得回放(旧实现整文件回放完)
    {
        QByteArray big;
        for (int i = 0; i < 20; ++i) big += frames;
        QTemporaryDir dir;
        QFile f(dir.filePath("big.bin"));
        f.open(QIODevice::WriteOnly); f.write(big); f.close();
        ReaderConfig cfg;
        cfg.mode = ReaderMode::FilePlayback;
        cfg.file_path = f.fileName();
        SerialReader r;
        int got = 0;
        QObject::connect(&r, &SerialReader::frame_ready, [&](const BplcFrame&) { ++got; });
        r.start(cfg);
        r.stop();
        QElapsedTimer t; t.start();
        while (t.elapsed() < 800) { app.processEvents(); QThread::msleep(5); }
        check(got == 0, "stop right after start cancels the queued start");
        // 再次 start:新会话完整回放
        got = 0;
        r.start(cfg);
        t.restart();
        while (got < base.frames * 20 && t.elapsed() < 30000) { app.processEvents(); QThread::msleep(1); }
        std::printf("  second session frames=%d\n", got);
        check(got == base.frames * 20, "a later start replays the whole file");

        // ⑥ L4:回放被背压阻塞(无消费者归还额度)时析构,必须很快返回且不 qFatal
        auto* r2 = new SerialReader;
        r2->flow_credit()->set_enabled(true);
        r2->start(cfg);
        QThread::msleep(100);   // 4096 帧后阻塞在额度上
        QElapsedTimer td; td.start();
        delete r2;
        std::printf("  destroy while blocked: %lld ms\n", (long long)td.elapsed());
        check(td.elapsed() < 1000, "SerialReader destructor returns promptly while replay is blocked");
    }

    std::printf("reader_garbage_probe: %s (%d failures)\n", g_fail ? "FAIL" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
