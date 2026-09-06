/// @file framedispatcher.cpp
/// @brief 帧分发器实现
#include "framedispatcher.h"

DispatcherWorker::DispatcherWorker(QObject* parent) : QObject(parent) {}

void DispatcherWorker::on_filter_changed(BplcParser::Filter f) {
    m_filter = f;
}

void DispatcherWorker::on_frame(const BplcFrame& frame) {
    auto r = m_parser.parse(frame, m_msdu, m_filter);
    emit parsed(r);
}

FrameDispatcher::FrameDispatcher(QObject* parent) : QObject(parent), m_thread(nullptr), m_worker(nullptr) {
    m_thread = new QThread(this);
    m_worker = new DispatcherWorker();
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
    m_thread->quit();
    m_thread->wait(2000);
}

void FrameDispatcher::set_filter(const BplcParser::Filter& f) {
    emit filter_changed(f);
}

void FrameDispatcher::connect_source(QObject* source) {
    connect(source, SIGNAL(frame_ready(BplcFrame)),
            m_worker, SLOT(on_frame(BplcFrame)),
            Qt::QueuedConnection);
}

void FrameDispatcher::on_parsed(const BplcParser::Result& r) {
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
