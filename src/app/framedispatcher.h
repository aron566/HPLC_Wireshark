/// @file framedispatcher.h
/// @brief 帧分发与统计容器
#ifndef FRAMEDISPATCHER_H
#define FRAMEDISPATCHER_H

#include "bplcparser.h"
#include "ringbuffer.h"
#include "statistics.h"
#include <QObject>
#include <QThread>
#include <atomic>

class DispatcherWorker : public QObject {
    Q_OBJECT
public:
    explicit DispatcherWorker(QObject* parent = nullptr);

public slots:
    void on_frame(const BplcFrame& frame);
    void on_filter_changed(BplcParser::Filter f);

signals:
    void parsed(const BplcParser::Result& r);
    void stats_updated(qint64 total, qint64 dropped);

private:
    BplcParser        m_parser;
    MsduState         m_msdu;
    BplcParser::Filter m_filter;
};

class FrameDispatcher : public QObject {
    Q_OBJECT
public:
    explicit FrameDispatcher(QObject* parent = nullptr);
    ~FrameDispatcher() override;

    void set_filter(const BplcParser::Filter& f);
    FrameStatistics* statistics() { return &m_stats; }

    void connect_source(QObject* source);

signals:
    void filter_changed(BplcParser::Filter f);
    void parsed(const BplcParser::Result& r);
    void stats_updated();

private slots:
    void on_parsed(const BplcParser::Result& r);

private:
    QThread*            m_thread;
    DispatcherWorker*   m_worker;
    FrameStatistics     m_stats;
    std::atomic<quint64> m_total{0};
    std::atomic<quint64> m_dropped{0};
};

#endif // FRAMEDISPATCHER_H
