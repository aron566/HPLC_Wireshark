/// @file framedispatcher.cpp
/// @brief 帧分发器实现
#include "framedispatcher.h"
#include "protocolfactory.h"
#include "appconfig.h"
#include "QsLog.h"

#include <QElapsedTimer>

DispatcherWorker::DispatcherWorker(std::unique_ptr<IProtocolParser> parser,
                                   QObject* parent)
    : QObject(parent), m_parser(std::move(parser)) {}

void DispatcherWorker::on_filter_changed(ParseFilter f) {
    m_filter = f;
}

void DispatcherWorker::on_frame(const BplcFrame& frame) {
    if (m_stopped.load()) return;   // 已停止:立即返回,快速清空积压帧
    QElapsedTimer t; t.start();
    auto r = m_parser->parse(frame, m_msdu, m_filter);
    emit parsed(r);
    // 错协议快解析(跳过 MSDU 重组/字段树,µs 级)会以数十万帧/秒洪泛
    // parsed 事件,饿死 GUI 的 flush timer 导致 pending 堆积卡顿。
    // 补足到最小帧间隔 ~20µs(≈50K 帧/秒),匹配 GUI 列表渲染速度。
    // 正常协议解析本就 ≥20µs,此分支不触发。
    if (t.nsecsElapsed() < 20000) {
        while (t.nsecsElapsed() < 20000)
            QThread::yieldCurrentThread();
    }
    // 累计统计:每 5 万帧打一条进度,定位回灌卡顿在哪个环节
    const quint64 n = m_frame_count.fetch_add(1) + 1;
    m_parse_us.fetch_add(quint64(t.nsecsElapsed() / 1000));
    if (n % 50000 == 0) {
        QLOG_INFO() << "parse progress:" << n << "frames, cumulative parse+throttle"
                    << (m_parse_us.load() / 1000) << "ms";
    }
}

FrameDispatcher::FrameDispatcher(QObject* parent)
    : QObject(parent), m_thread(nullptr), m_worker(nullptr) {
    // 按 config.ini 协议选择实例化解析器(内置国网/南网,或插件协议)
    // 协议键统一转大写为注册表 id: "gw_2022"→"GW_2022",插件协议直接存大写 id
    auto parser = make_parser_by_id(appcfg::protocol().toUpper());
    if (!parser)  // 未知协议回退国网
        parser = make_parser_by_id(QStringLiteral("GW_2022"));
    m_thread = new QThread(this);
    m_worker = new DispatcherWorker(std::move(parser));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this,     &FrameDispatcher::filter_changed,
            m_worker, &DispatcherWorker::on_filter_changed,
            Qt::QueuedConnection);
    connect(m_worker, &DispatcherWorker::parsed,
            this,     &FrameDispatcher::on_parsed,
            Qt::QueuedConnection);
    m_thread->start();
}

FrameDispatcher::~FrameDispatcher() {
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(2000)) {
            // 线程 2 秒未退出(异常):强制终止兜底,避免 QThread 析构时
            // 线程仍运行触发 qFatal("Destroyed while thread is still running")。
            m_thread->terminate();
            m_thread->wait();
        }
    }
}

void FrameDispatcher::set_filter(const ParseFilter& f) {
    emit filter_changed(f);
}

void FrameDispatcher::connect_source(QObject* source) {
    connect(source, SIGNAL(frame_ready(BplcFrame)),
            m_worker, SLOT(on_frame(BplcFrame)),
            Qt::QueuedConnection);
}

void FrameDispatcher::disconnect_source(QObject* source) {
    if (source && m_worker)
        disconnect(source, nullptr, m_worker, nullptr);
}

void FrameDispatcher::shutdown() {
    // 立即断开 worker→本对象的所有信号连接(含 parsed),并退出解析线程:
    // request_stop 置停止标志让 on_frame 直接返回(不清算积压帧),
    // 再 quit,线程快速退出、不再向 GUI 投递 parsed 事件。
    if (m_worker) {
        m_worker->request_stop();
        disconnect(m_worker, nullptr, this, nullptr);
    }
    if (m_thread) m_thread->quit();
}

void FrameDispatcher::on_parsed(const ParseResult& r) {
    m_total.fetch_add(1);
    if (!r.accept) m_dropped.fetch_add(1);

    FrameStatistics::Key key = FrameStatistics::KEY_OTHER;
    if (r.accept) {
        bool rf = r.meta.is_rf;
        switch (r.mpdu.frame_type) {
            case 0: key = rf ? FrameStatistics::KEY_BEACON_HRF : FrameStatistics::KEY_BEACON_HPLC; break;
            case 1: key = rf ? FrameStatistics::KEY_SOF_HRF    : FrameStatistics::KEY_SOF_HPLC;    break;
            case 2: key = rf ? FrameStatistics::KEY_ACK_HRF    : FrameStatistics::KEY_ACK_HPLC;    break;
            case 3: key = rf ? FrameStatistics::KEY_COORD_HRF  : FrameStatistics::KEY_COORD_HPLC;  break;
        }
        m_stats.increment(key);
        if (!r.msdu_body.isEmpty()) m_stats.increment(FrameStatistics::KEY_MSDU_COMPLETE);
    } else {
        m_stats.increment(FrameStatistics::KEY_DROPPED);
    }

    emit parsed(r);
    emit stats_updated();
}
