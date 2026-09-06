/// @file ringbuffer.h
/// @brief 单生产者单消费者的环形缓冲 + 帧队列
/// @details 两个类:
///   - ByteRingBuffer:QSemaphore 计数 + QMutex 保护索引,溢出丢弃
///   - FrameQueue:跨线程 BplcFrame 队列,满则丢(计数器自增)
#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QSemaphore>
#include <QVector>
#include <QAtomicInt>

#include "bplcframe.h"

class ByteRingBuffer {
public:
    explicit ByteRingBuffer(int capacity = 256 * 1024)
        : m_capacity(capacity), m_buffer(capacity, 0),
          m_free(capacity), m_used(0),
          m_read_idx(0), m_write_idx(0) {}

    int push(const char* data, int len) {
        int written = 0;
        while (written < len) {
            if (!m_free.tryAcquire(1, 0)) break;
            {
                QMutexLocker lock(&m_mutex);
                m_buffer[m_write_idx] = static_cast<quint8>(data[written]);
                m_write_idx = (m_write_idx + 1) % m_capacity;
                ++written;
            }
            m_used.release(1);
        }
        return written;
    }

    int push(const QByteArray& data) {
        return push(data.constData(), data.size());
    }

    int pop(int max_len, QByteArray& out, int timeout_ms = 50) {
        out.clear();
        if (!m_used.tryAcquire(1, timeout_ms)) return 0;
        QMutexLocker lock(&m_mutex);
        int got = 1;
        out.append(static_cast<char>(m_buffer[m_read_idx]));
        m_read_idx = (m_read_idx + 1) % m_capacity;
        while (got < max_len && m_used.tryAcquire(1, 0)) {
            out.append(static_cast<char>(m_buffer[m_read_idx]));
            m_read_idx = (m_read_idx + 1) % m_capacity;
            ++got;
        }
        lock.unlock();
        m_free.release(got);
        return got;
    }

    int used_bytes() const { return m_used.available(); }
    int free_bytes() const { return m_free.available(); }
    int capacity() const { return m_capacity; }

    void clear() {
        QMutexLocker lock(&m_mutex);
        int used = m_used.available();
        m_used.acquire(used);
        m_free.release(used);
        m_read_idx = m_write_idx = 0;
    }

private:
    int                m_capacity;
    QVector<quint8>    m_buffer;
    QSemaphore         m_free;
    QSemaphore         m_used;
    QMutex             m_mutex;
    int                m_read_idx;
    int                m_write_idx;
};

class FrameQueue {
public:
    explicit FrameQueue(int capacity = 4096) : m_capacity(capacity), m_free(capacity) {}

    bool enqueue(const BplcFrame& frame) {
        if (!m_free.tryAcquire(1, 0)) {
            m_dropped.fetchAndAddOrdered(1);
            return false;
        }
        QMutexLocker lock(&m_mutex);
        m_frames.push_back(frame);
        m_used.release(1);
        return true;
    }

    bool dequeue(BplcFrame& out, int timeout_ms = 100) {
        if (!m_used.tryAcquire(1, timeout_ms)) return false;
        QMutexLocker lock(&m_mutex);
        if (m_frames.isEmpty()) return false;
        out = m_frames.front();
        m_frames.pop_front();
        m_free.release(1);
        return true;
    }

    int  size()    const { return m_used.available(); }
    int  dropped() const { return m_dropped.loadAcquire(); }
    void clear_dropped() { m_dropped.storeRelease(0); }

private:
    int                m_capacity;
    QSemaphore         m_free;
    QSemaphore         m_used;
    QMutex             m_mutex;
    QList<BplcFrame>   m_frames;
    QAtomicInt         m_dropped{0};
};

#endif // RINGBUFFER_H
