/// @file serialreader.cpp
/// @brief ReaderWorker + SerialReader 实现
#include "serialreader.h"
#include <QSerialPortInfo>
#include <QDebug>

ReaderWorker::ReaderWorker(QObject* parent) : QObject(parent),
    m_serial(nullptr), m_file(nullptr), m_file_timer(nullptr),
    m_get3c(false), m_running(false) {}

ReaderWorker::~ReaderWorker() {
    stop_reading();
}

void ReaderWorker::start_reading(const ReaderConfig& cfg) {
    if (m_running) stop_reading();
    m_cfg = cfg;
    m_in_buf.clear();
    m_get3c = false;

    if (cfg.mode == ReaderMode::SerialPort) {
        m_serial = new QSerialPort(this);
        m_serial->setPortName(cfg.serial_name);
        m_serial->setBaudRate(cfg.baud_rate);
        m_serial->setDataBits(cfg.data_bits);
        m_serial->setParity(cfg.parity);
        m_serial->setStopBits(cfg.stop_bits);
        m_serial->setFlowControl(QSerialPort::NoFlowControl);
        if (!m_serial->open(QIODevice::ReadOnly)) {
            emit error_occurred(QString("打开串口失败: %1").arg(m_serial->errorString()));
            return;
        }
        connect(m_serial, &QSerialPort::readyRead, this, &ReaderWorker::on_serial_ready_read);
        emit status_message(QString("串口 %1 @ %2 已打开").arg(cfg.serial_name).arg(cfg.baud_rate));
    } else if (cfg.mode == ReaderMode::FilePlayback) {
        m_file = new QFile(cfg.file_path, this);
        if (!m_file->open(QIODevice::ReadOnly)) {
            emit error_occurred(QString("打开文件失败: %1").arg(m_file->errorString()));
            return;
        }
        m_file_timer = new QTimer(this);
        connect(m_file_timer, &QTimer::timeout, this, &ReaderWorker::on_file_poll_tick);
        m_file_timer->start(5);
        emit status_message(QString("文件回放: %1").arg(cfg.file_path));
    } else if (cfg.mode == ReaderMode::RawHex) {
        m_file = new QFile(cfg.file_path, this);
        if (!m_file->open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit error_occurred(QString("打开文件失败: %1").arg(m_file->errorString()));
            return;
        }
        emit status_message(QString("裸 hex 模式: %1").arg(cfg.file_path));
        while (!m_file->atEnd()) {
            QByteArray line = m_file->readLine().trimmed();
            if (!line.isEmpty()) process_raw_hex_line(line);
        }
        m_file->close();
        emit finished();
        return;
    }

    m_running = true;
}

void ReaderWorker::stop_reading() {
    m_running = false;
    if (m_file_timer) { m_file_timer->stop(); m_file_timer->deleteLater(); m_file_timer = nullptr; }
    if (m_serial)   { m_serial->close(); m_serial->deleteLater(); m_serial = nullptr; }
    if (m_file)     { m_file->close(); m_file->deleteLater(); m_file = nullptr; }
    emit finished();
}

void ReaderWorker::on_serial_ready_read() {
    if (!m_serial) return;
    QByteArray chunk = m_serial->readAll();
    m_in_buf.append(chunk);
    try_extract_frame();
}

void ReaderWorker::on_file_poll_tick() {
    if (!m_file || m_file->atEnd()) {
        m_file_timer->stop();
        emit status_message(QStringLiteral("文件回放结束"));
        emit finished();
        return;
    }
    QByteArray chunk = m_file->read(1024);
    m_in_buf.append(chunk);
    try_extract_frame();
}

void ReaderWorker::try_extract_frame() {
    while (!m_in_buf.isEmpty()) {
        if (!m_get3c) {
            int idx = m_in_buf.indexOf(char(0x3C));
            if (idx < 0) {
                m_in_buf.clear();
                return;
            }
            m_in_buf.remove(0, idx + 1);
            m_get3c = true;
            continue;
        }
        int idx = m_in_buf.indexOf(char(0x3E));
        if (idx < 0) return;
        QByteArray frame = m_in_buf.left(idx);
        m_in_buf.remove(0, idx + 1);
        m_get3c = false;

        // 反转义 0x3D
        QByteArray unesc;
        unesc.reserve(frame.size());
        for (int i = 0; i < frame.size(); ++i) {
            quint8 b = static_cast<quint8>(frame[i]);
            if (b == 0x3D && i + 1 < frame.size()) {
                ++i;
                unesc.append(static_cast<char>(0xFF - static_cast<quint8>(frame[i])));
            } else {
                unesc.append(static_cast<char>(b));
            }
        }

        BplcFrame bf;
        bf.arrival_ms = QDateTime::currentMSecsSinceEpoch();
        bf.meta.has_time_tag = m_cfg.has_time_tag;
        bf.data = unesc;
        emit frame_ready(bf);
    }
}

void ReaderWorker::process_raw_hex_line(const QByteArray& line) {
    QByteArray cleaned;
    cleaned.reserve(line.size());
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n') continue;
        cleaned.append(c);
    }
    if (cleaned.isEmpty() || (cleaned.size() % 2) != 0) return;

    QByteArray raw;
    raw.reserve(cleaned.size() / 2);
    for (int i = 0; i < cleaned.size(); i += 2) {
        bool ok1, ok2;
        quint8 hi = static_cast<quint8>(cleaned.mid(i, 1).toInt(&ok1, 16));
        quint8 lo = static_cast<quint8>(cleaned.mid(i + 1, 1).toInt(&ok2, 16));
        if (!ok1 || !ok2) return;
        raw.append(static_cast<char>((hi << 4) | lo));
    }

    BplcFrame bf;
    bf.meta.from_raw = true;
    bf.arrival_ms = QDateTime::currentMSecsSinceEpoch();
    bf.data = raw;
    emit frame_ready(bf);
}

SerialReader::SerialReader(QObject* parent) : QObject(parent), m_thread(nullptr), m_worker(nullptr) {
    m_thread = new QThread(this);
    m_worker = new ReaderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &SerialReader::request_start, m_worker, &ReaderWorker::start_reading);
    connect(this, &SerialReader::request_stop,  m_worker, &ReaderWorker::stop_reading);

    connect(m_worker, &ReaderWorker::frame_ready,    this, &SerialReader::on_frame);
    connect(m_worker, &ReaderWorker::status_message, this, &SerialReader::on_status);
    connect(m_worker, &ReaderWorker::error_occurred, this, &SerialReader::on_error);

    m_thread->start();
}

SerialReader::~SerialReader() {
    stop();
    m_thread->quit();
    m_thread->wait(2000);
}

bool SerialReader::start(const ReaderConfig& cfg) {
    emit request_start(cfg);
    return true;
}

void SerialReader::stop() {
    emit request_stop();
}

void SerialReader::on_frame(BplcFrame f) { emit frame_ready(f); }
void SerialReader::on_status(QString s) { emit status_message(s); }
void SerialReader::on_error(QString e)  { emit error_occurred(e); }
