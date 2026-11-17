/// @file frame_dispatcher_lifetime_probe.cpp
/// @brief FrameDispatcher 反复 构造→shutdown()→处理延迟删除→析构(切换协议/重连时的重建路径)。
///        旧实现 shutdown 提前 quit 使 worker 经 finished→deleteLater 先被删,析构再
///        request_stop() 即释放后使用(ASan heap-use-after-free)。修复后应 0 报错。
///        另含 N1:解析线程卡住时析构必须有界返回(两次 5000 ms 等待后放弃),而非永久挂起。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "appconfig.h"
#include "framedispatcher.h"
#include "iprotocolparser.h"
#include "protocolfactory.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

// 本探针不喂帧,on_frame 不会走到 prepare_entry;给个桩免得链接整个 UI(entrybuilder→topo)
PreparedEntryPtr prepare_entry(ParseResult&&) { return PreparedEntryPtr(); }

/// @brief N1 桩解析器:parse() 永不返回,模拟解析线程真卡住(如插件死循环/宿主无响应)
class StuckParser : public IProtocolParser {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2026; }
    ParseResult parse(const BplcFrame&, MsduState&, const ParseFilter&) override {
        entered.store(true);
        for (;;) std::this_thread::sleep_for(std::chrono::hours(1));  // 永不返回
        return ParseResult();  // 不可达
    }
    static std::atomic<bool> entered;
};
std::atomic<bool> StuckParser::entered{false};

/// @brief 喂帧信号源(connect_source 要求信号 frame_ready(BplcFrame, quint64))
class FrameSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void frame_ready(BplcFrame, quint64);
    void finished();
};

// N1:析构必须在有界时间内返回(两次 5000 ms 等待后放弃)。旧实现第二次是无界
// wait(),本用例会永久挂起(外层 timeout 杀掉即 FAIL)。卡住的线程/worker 在
// 析构里脱钩泄漏(仅退出路径,随进程退出回收),CI 跑本探针时已配
// ASAN_OPTIONS=detect_leaks=0,见 probe_suite.sh。
static void test_stuck_thread_destructor() {
    StuckParser::entered.store(false);
    register_parser(QStringLiteral("STUCK"), [] {
        return std::unique_ptr<IProtocolParser>(new StuckParser());
    });
    const QString old_protocol = appcfg::protocol();
    appcfg::set_protocol(QStringLiteral("stuck"));

    // 队列连接要元类型(主程序启动时已注册,探针里自己注册)
    qRegisterMetaType<BplcFrame>("BplcFrame");
    auto* d = new FrameDispatcher();
    FrameSource src;
    d->connect_source(&src);
    BplcFrame frame;
    emit src.frame_ready(frame, 0);
    QElapsedTimer wait_entered;
    wait_entered.start();
    while (!StuckParser::entered.load() && wait_entered.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    check("N1: worker thread stuck in parse", StuckParser::entered.load());

    QElapsedTimer dt;
    dt.start();
    delete d;  // 卡住的线程:新实现约 10 s 返回(两次 5000 ms),旧实现永不返回
    const qint64 ms = dt.elapsed();
    std::printf("  N1: destructor returned after %lld ms\n", static_cast<long long>(ms));
    check("N1: destructor returns in bounded time (< 60 s)", ms < 60000);
    check("N1: destructor waited for both bounded waits (>= 9 s)", ms >= 9000);

    appcfg::set_protocol(old_protocol);
    unregister_parser(QStringLiteral("STUCK"));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    constexpr int kRounds = 50;
    for (int i = 0; i < kRounds; ++i) {
        auto* d = new FrameDispatcher();
        d->shutdown();
        // 让 QThread::finished 及其可能挂的 deleteLater 全部投递并执行
        for (int k = 0; k < 5; ++k) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QThread::msleep(1);
        }
        delete d;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    // 不调 shutdown 直接析构的路径也走一遍
    for (int i = 0; i < 10; ++i) delete new FrameDispatcher();
    test_stuck_thread_destructor();  // N1:解析线程卡住时析构有界返回
    if (g_fails) {
        std::printf("frame_dispatcher_lifetime_probe: FAILURES (%d failures)\n", g_fails);
        return 1;
    }
    std::printf("frame_dispatcher_lifetime_probe: OK (%d rounds, 0 failures)\n", kRounds);
    return 0;
}

#include "frame_dispatcher_lifetime_probe.moc"
