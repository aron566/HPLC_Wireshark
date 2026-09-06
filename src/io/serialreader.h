/// @file serialreader.h
/// @brief ReaderWorker + SerialReader:后台切帧的两种封装
/// @details ReaderConfig 是用户配置;ReaderMode 区分串口/文件/裸 hex。
///          ReaderWorker 在独立 QThread 跑,SerialReader 仅做线程包装。
#ifndef SERIALREADER_H
#define SERIALREADER_H

#include <QObject>
#include <QThread>
#include <QSerialPort>
#include <QFile>
#include <QTimer>
#include <QString>
#include <QMetaType>

#include "ringbuffer.h"
#include "bplcframe.h"

enum class ReaderMode {
    SerialPort,
    FilePlayback,
    RawHex
};

struct ReaderConfig {
    ReaderMode mode;
    QString    serial_name;
    qint32     baud_rate;
    QSerialPort::DataBits data_bits;
    QSerialPort::StopBits stop_bits;
    QSerialPort::Parity   parity;
    QString    file_path;
    bool       has_time_tag;

    ReaderConfig()
        : mode(ReaderMode::SerialPort),
          baud_rate(460800),
          data_bits(QSerialPort::Data8),
          stop_bits(QSerialPort::OneStop),
          parity(QSerialPort::NoParity),
          has_time_tag(false) {}
};
Q_DECLARE_METATYPE(ReaderConfig)

class ReaderWorker : public QObject {
    Q_OBJECT
public:
    explicit ReaderWorker(QObject* parent = nullptr);
    ~ReaderWorker() override;

public slots:
    void start_reading(const ReaderConfig& cfg);
    void stop_reading();

signals:
    void frame_ready(BplcFrame frame);
    void status_message(QString msg);
    void error_occurred(QString err);
    void finished();

private slots:
    void on_serial_ready_read();
    void on_file_poll_tick();

private:
    void try_extract_frame();
    void process_raw_hex_line(const QByteArray& line);

    QSerialPort* m_serial;
    QFile*       m_file;
    QTimer*      m_file_timer;
    QByteArray   m_in_buf;
    bool         m_get3c;
    bool         m_running;
    ReaderConfig m_cfg;
};

class SerialReader : public QObject {
    Q_OBJECT
public:
    explicit SerialReader(QObject* parent = nullptr);
    ~SerialReader() override;

    bool start(const ReaderConfig& cfg);
    void stop();

signals:
    void request_start(ReaderConfig cfg);
    void request_stop();
    void frame_ready(BplcFrame frame);
    void status_message(QString msg);
    void error_occurred(QString err);

private slots:
    void on_frame(BplcFrame f);
    void on_status(QString s);
    void on_error(QString e);

private:
    QThread*      m_thread;
    ReaderWorker* m_worker;
};

#endif // SERIALREADER_H
