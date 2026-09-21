/// @file framedispatcher.h
/// @brief 帧分发与统计容器
#ifndef FRAMEDISPATCHER_H
#define FRAMEDISPATCHER_H

#include "iprotocolparser.h"
#include "ringbuffer.h"
#include "statistics.h"
#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>

class DispatcherWorker : public QObject {
    Q_OBJECT
public:
    explicit DispatcherWorker(std::unique_ptr<IProtocolParser> parser,
                              QObject* parent = nullptr);
    /// @brief 请求停止:置位后 on_frame 立即返回不再解析(供 shutdown 快速清空积压帧)
    void request_stop() { m_stopped.store(true); }

public slots:
    void on_frame(const BplcFrame& frame);
    void on_filter_changed(ParseFilter f);

signals:
    void parsed(const ParseResult& r);
    void stats_updated(qint64 total, qint64 dropped);

private:
    std::unique_ptr<IProtocolParser> m_parser;
    MsduState   m_msdu;
    ParseFilter m_filter;
    std::atomic<bool> m_stopped{false};
};

class FrameDispatcher : public QObject {
    Q_OBJECT
public:
    explicit FrameDispatcher(QObject* parent = nullptr);
    ~FrameDispatcher() override;

    void set_filter(const ParseFilter& f);
    FrameStatistics* statistics() { return &m_stats; }

    void connect_source(QObject* source);
    void disconnect_source(QObject* source);
    /// @brief 立即停止解析:断开 worker 信号并退出解析线程;
    ///        供重建 dispatcher 时快速让旧线程停止处理积压帧。
    void shutdown();

signals:
    void filter_changed(ParseFilter f);
    void parsed(const ParseResult& r);
    void stats_updated();

private slots:
    void on_parsed(const ParseResult& r);

private:
    QThread*            m_thread;
    DispatcherWorker*   m_worker;
    FrameStatistics     m_stats;
    std::atomic<quint64> m_total{0};
    std::atomic<quint64> m_dropped{0};
};

#endif // FRAMEDISPATCHER_H
