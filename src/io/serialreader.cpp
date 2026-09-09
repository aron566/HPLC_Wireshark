/// @file serialreader.cpp
/// @brief ReaderWorker + SerialReader 实现
#include "serialreader.h"
#include "i18n.h"
#include "playbackwriter.h"   // playback::looks_like_bcd_time(回放时间标签识别)
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <chrono>

ReaderWorker::ReaderWorker(QObject* parent) : QObject(parent),
    m_serial(nullptr), m_file(nullptr), m_file_timer(nullptr),
    m_get3c(false), m_frame_rx_us(0), m_playback_base_ms(-1), m_first_frame(false),
    m_last_ntb(0), m_last_ft(0), m_running(false) {}

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
            emit error_occurred(trl::L("打开串口失败: %1").arg(m_serial->errorString()));
            return;
        }
        connect(m_serial, &QSerialPort::readyRead, this, &ReaderWorker::on_serial_ready_read);
        emit status_message(trl::L("串口 %1 @ %2 已打开").arg(cfg.serial_name).arg(cfg.baud_rate));
    } else if (cfg.mode == ReaderMode::FilePlayback) {
        m_file = new QFile(cfg.file_path, this);
        if (!m_file->open(QIODevice::ReadOnly)) {
            emit error_occurred(trl::L("打开文件失败: %1").arg(m_file->errorString()));
            return;
        }
        // 新 bin:文件头 8B BCD 时间标注(首帧本地时刻,独立于帧);读取并跳过
        m_playback_base_ms = -1;
        m_first_frame = true;
        QByteArray head = m_file->peek(8);
        if (head.size() == 8 && playback::looks_like_bcd_time(head)) {
            m_playback_base_ms = bcd_ms_of(head);
            m_file->seek(8);
        }
        m_file_timer = new QTimer(this);
        connect(m_file_timer, &QTimer::timeout, this, &ReaderWorker::on_file_poll_tick);
        m_file_timer->start(5);
        emit status_message(trl::L("文件回放: %1").arg(cfg.file_path));
    } else if (cfg.mode == ReaderMode::RawHex) {
        m_file = new QFile(cfg.file_path, this);
        if (!m_file->open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit error_occurred(trl::L("打开文件失败: %1").arg(m_file->errorString()));
            return;
        }
        emit status_message(trl::L("裸 hex 模式: %1").arg(cfg.file_path));
        m_raw_base_ms = -1;   // 未给出时间头 → 回退本地时间
        while (!m_file->atEnd()) {
            QByteArray line = m_file->readLine().trimmed();
            if (line.isEmpty()) continue;
            // 时间头行(首帧时间文本,如 TIME: 2026-09-07 18:43:00.123)
            const qint64 hdr_ms = parse_time_header(line);
            if (hdr_ms >= 0) { m_raw_base_ms = hdr_ms; continue; }
            process_raw_hex_line(line);
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

namespace {
// 单调高精度时钟(µs):帧起始分节符 0x3C 到达打点
qint64 steady_us() {
    return qint64(std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
}  // namespace

void ReaderWorker::on_serial_ready_read() {
    if (!m_serial) return;
    QByteArray chunk = m_serial->readAll();
    if (chunk.isEmpty()) return;
    // “读到第一个字符是 0x3C”即帧起始到达:缓冲无残留(上一帧已收完)且
    // 新数据首字节为 0x3C 时,此刻就是该帧起点;后续同批内的帧起点由
    // try_extract_frame 逐 0x3C 发现打点
    if (!m_get3c && m_in_buf.isEmpty() && quint8(chunk[0]) == 0x3C)
        m_frame_rx_us = steady_us();
    m_in_buf.append(chunk);
    try_extract_frame();
}

void ReaderWorker::on_file_poll_tick() {
    if (!m_file || m_file->atEnd()) {
        m_file_timer->stop();
        emit status_message(trl::L("文件回放结束"));
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
            // 帧起始分节符 0x3C 的本地接收时刻(单调 µs):
            // 批内后续帧/缓冲中发现的起点在此打点(实时串口);文件回放不填
            if (m_cfg.mode == ReaderMode::SerialPort)
                m_frame_rx_us = steady_us();
            continue;
        }
        int idx = m_in_buf.indexOf(char(0x3E));
        if (idx < 0) return;
        QByteArray frame = m_in_buf.left(idx);
        m_in_buf.remove(0, idx + 1);
        m_get3c = false;

        // 原始串口帧(0x3C...0x3E,含哨兵与 0x3D 转义原样):所有 0x3C 帧均保留
        // (实时串口与回放 bin 同为 0x3C 数据 → 原始报文列一致显示);
        // 裸 hex 文本无哨兵,在 process_raw_hex_line 单独处理
        QByteArray wire;
        wire.reserve(frame.size() + 2);
        wire.append(char(0x3C)).append(frame).append(char(0x3E));

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
        // 新 bin:时间轴以帧内 NTB(tick)差推进——
        //   首帧  frame_time = 文件头 8B 标注时刻
        //   后续帧 frame_time = 上一帧 frame_time + (本帧 NTB − 上一帧 NTB)
        if (m_playback_base_ms >= 0) {
            const quint32 ntb = ((quint32)(quint8)unesc[2])
                              | ((quint32)(quint8)unesc[3] << 8)
                              | ((quint32)(quint8)unesc[4] << 16)
                              | ((quint32)(quint8)unesc[5] << 24);
            if (m_first_frame) {
                bf.arrival_ms = m_playback_base_ms;
                m_last_ft = m_playback_base_ms;
                m_last_ntb = ntb;
                m_first_frame = false;
            } else {
                const qint64 dn = (qint32)(ntb - m_last_ntb);   // 回绕安全
                bf.arrival_ms = m_last_ft + dn;
                m_last_ft = bf.arrival_ms;
                m_last_ntb = ntb;
            }
        } else {
            bf.arrival_ms = QDateTime::currentMSecsSinceEpoch();
        }
        bf.arrival_us = m_frame_rx_us;   // 0x3C 起始高精度接收时刻(实时)
        bf.raw_wire   = wire;            // 原始串口帧原样(调试复制)
        // 帧 ts 域语义:串口实时=设备填的 NTB tick(40µs 分辨);
        // 文件回放(0x3C bin)=导出端 epoch ms 低 32 位(毫秒级)
        bf.meta.frame_ts_is_ntb = (m_cfg.mode == ReaderMode::SerialPort);
        // 新格式:8B 时间标注仅在文件头(已 seek 跳过),帧体无逐帧 BCD;
        // 不做旧版每帧 BCD 兼容检测
        bf.meta.has_time_tag = false;
        bf.data = unesc;
        emit frame_ready(bf);
    }
}

void ReaderWorker::process_raw_hex_line(const QByteArray& line) {
    // 每行一帧裸数据(无 0x3C/0x3E/0x3D 封装):
    //   [ts 4B LE][phr_mcs 1B][option 1B][channel 1B][isRF 1B][MPDU...]
    // 支持 "0x01 0xd5 …" 或 "01 d5…" 写法;帧间由行分隔定界。
    QByteArray raw;
    QByteArray s = line;
    const QList<QByteArray> toks = s.replace(',', ' ').split(' ');
    for (const QByteArray& t0 : toks) {
        QByteArray t = t0.trimmed();
        if (t.isEmpty()) continue;
        if ((t.startsWith("0x") || t.startsWith("0X")) && t.size() >= 3)
            t.remove(0, 2);
        if (t.size() < 2 || (t.size() % 2) != 0) return;   // 非纯 hex → 跳过整行
        for (int i = 0; i < t.size(); i += 2) {
            bool ok = false;
            int v = t.mid(i, 2).toInt(&ok, 16);
            if (!ok) return;
            raw.append(char(v));
        }
    }
    // 至少 ts4+media4+1B MPDU,否则无法构成可解析帧
    if (raw.size() < 9) return;

    // 仅支持完整 0x3C 原始帧行(导出 v1.0.13+ 格式);其它行格式忽略
    if ((quint8)raw[0] != 0x3C || (quint8)raw[raw.size() - 1] != 0x3E) return;

    {
        QByteArray body = raw.mid(1, raw.size() - 2);
        QByteArray data;
        data.reserve(body.size());
        for (int i = 0; i < body.size(); ++i) {
            quint8 b = (quint8)body[i];
            if (b == 0x3D && i + 1 < body.size()) {
                ++i;
                data.append((char)(0xFF - (quint8)body[i]));
            } else {
                data.append((char)b);
            }
        }
        // ts4 = epoch ms 低 32 → 还原捕获时刻
        const quint32 ts_le2 = (quint32)(quint8)data[2]
                             | (quint32)(quint8)data[3] << 8
                             | (quint32)(quint8)data[4] << 16
                             | (quint32)(quint8)data[5] << 24;
        qint64 now2 = QDateTime::currentMSecsSinceEpoch();
        qint64 hi2 = (now2 >> 32) << 32;
        qint64 t2 = hi2 | qint64(ts_le2);
        if (t2 - now2 >  (1LL << 31)) t2 -= (1LL << 32);
        if (now2 - t2 > (1LL << 31))  t2 += (1LL << 32);

        BplcFrame bf;
        bf.meta.from_raw = false;
        bf.meta.has_time_tag = false;
        bf.meta.frame_ts_is_ntb = false;
        bf.arrival_ms = t2;
        bf.raw_wire   = raw;   // 0x3C...0x3E 原样
        bf.data       = data;  // 反转义后的 [dlen][ts][media][MPDU]
        emit frame_ready(bf);
        return;
    }
}

qint64 ReaderWorker::parse_time_header(const QByteArray& line) {
    // 兼容 "TIME: 2026-09-07 18:43:00.123" / "TIME: 2026-09-07 18:43:00" /
    // 直接 "2026-09-07 18:43:00.123"(样本 TIME: 行后还可能跟其它说明文字)
    static const QRegularExpression re(
        QStringLiteral("(\\d{4}-\\d{2}-\\d{2})[ T](\\d{2}):(\\d{2}):(\\d{2})"
                       "(?:\\.(\\d{1,3}))?"));
    const QRegularExpressionMatch m = re.match(QLatin1String(line));
    if (!m.hasMatch()) return -1;
    const QDate date = QDate::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd"));
    const QTime time(m.captured(2).toInt(), m.captured(3).toInt(),
                     m.captured(4).toInt(), m.captured(5).isEmpty() ? 0
                                        : m.captured(5).leftJustified(3, '0').toInt());
    if (!date.isValid() || !time.isValid()) return -1;
    return QDateTime(date, time).toMSecsSinceEpoch();
}

qint64 ReaderWorker::bcd_ms_of(const QByteArray& b) {
    // 8B BCD:[年-2000][月][日][时][分][秒][毫秒低2][毫秒低2]→ 本地 epoch ms
    auto d2 = [](quint8 x) { return int((x >> 4) * 10 + (x & 0x0F)); };
    if (b.size() < 8) return -1;
    const QDate date(2000 + d2(quint8(b[0])), d2(quint8(b[1])), d2(quint8(b[2])));
    const QTime time(d2(quint8(b[3])), d2(quint8(b[4])), d2(quint8(b[5])));
    if (!date.isValid() || !time.isValid()) return -1;
    const int ms = d2(quint8(b[6])) * 100 + d2(quint8(b[7]));
    return QDateTime(date, time).addMSecs(ms).toMSecsSinceEpoch();
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
namespace {
// 中→英注册(文件级:数据源状态/错误消息)
struct I18nRegSerialReader {
    I18nRegSerialReader() {
        trl::register_en("打开串口失败: %1", "Failed to open serial port: %1");
        trl::register_en("串口 %1 @ %2 已打开", "Serial port %1 @ %2 opened");
        trl::register_en("打开文件失败: %1", "Failed to open file: %1");
        trl::register_en("文件回放: %1", "File replay: %1");
        trl::register_en("裸 hex 模式: %1", "Raw hex mode: %1");
        trl::register_en("文件回放结束", "File replay ended");
    }
};
const I18nRegSerialReader g_i18n_reg_serialreader;
}  // namespace
