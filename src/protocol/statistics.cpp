/// @file statistics.cpp
/// @brief FrameStatistics 实现:按帧类型自增的 QHash<int, qint64>
#include "statistics.h"

FrameStatistics::FrameStatistics(QObject* parent) : QObject(parent) {
    reset();
}

void FrameStatistics::increment(Key k, qint64 n) {
    QMutexLocker lock(&m_mutex);
    m_counts[static_cast<int>(k)] += n;
}

void FrameStatistics::reset() {
    QMutexLocker lock(&m_mutex);
    m_counts.clear();
    for (int i = 0; i < KEY_COUNT; ++i) {
        m_counts[i] = 0;
    }
}

QHash<int, qint64> FrameStatistics::snapshot() const {
    QMutexLocker lock(&m_mutex);
    return m_counts;
}

qint64 FrameStatistics::total() const {
    QMutexLocker lock(&m_mutex);
    qint64 sum = 0;
    for (auto v : m_counts) sum += v;
    return sum;
}
