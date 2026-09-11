/// @file serialreader.cpp
/// @brief ReaderWorker + SerialReader 实现
#include "serialreader.h"
#include "i18n.h"
#include "playbackwriter.h"
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>
#include <chrono>

ReaderWorker::ReaderWorker(QObject* parent) : QObject(parent),
    m_serial(nullptr), m_file(nullptr),
    m_get3c(false), m_frame_rx_us(0), m_playback_base_ms(-1), m_first_frame(false),
    m_last_ntb(0), m_last_ft(0), m_running(false), m_raw_base_ms(-1),
    m_hex_seg_first(false), m_last_hex_ts(0), m_last_hex_ft(0),
    m_last_local_ms(0), m_file_size(0), m_last_progress(-1) {}

ReaderWorker::~ReaderWorker() {
    stop_reading();
}

void ReaderWorker::start_reading(const ReaderConfig& cfg) {
    if (m_running) stop_reading();
    m_cfg = cfg;
    m_in_buf.clear();
    m_get3c = false;
    m_last_local_ms = 0;   // 每次启动重置断段判断基准

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
        m_file_size = m_file->size();
        m_last_progress = -1;
        // 新 bin:文件头 8B BCD 时间标注(首帧本地时刻,独立于帧);读取并跳过
        m_playback_base_ms = -1;
        m_first_frame = true;
        QByteArray head = m_file->peek(8);
        if (head.size() == 8 && bcd_ms_of(head) >= 0) {   // 强校验(BCD 且日期合法)
            m_playback_base_ms = bcd_ms_of(head);
            m_file->seek(8);
        }
        emit status_message(trl::L("文件回放: %1").arg(cfg.file_path));
        // 一次性读完整文件(不用 timer,快速回放);每块 1MB,切帧即时 emit
        while (!m_file->atEnd()) {
            QByteArray chunk = m_file->read(1024 * 1024);
            if (chunk.isEmpty()) break;
            m_in_buf.append(chunk);
            try_extract_frame();
            // 回放进度百分比(按已读字节/文件总大小,仅变化时上报)
            if (m_file_size > 0) {
                const int percent = int(m_file->pos() * 100 / m_file_size);
                if (percent != m_last_progress) {
                    m_last_progress = percent;
                    emit progress_percent(percent);
                }
            }
        }
        m_file->close();
        emit progress_percent(100);
        emit status_message(trl::L("文件回放结束"));
        emit finished();
        return;
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
            if (hdr_ms >= 0) { m_raw_base_ms = hdr_ms; m_hex_seg_first = true; continue; }
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

bool ReaderWorker::try_consume_bcd_tag() {
    // 仅文件回放有段间 8B 标注;合法 BCD 不含 0x3C/0x3D/0x3E,不会误吃帧哨兵
    if (m_cfg.mode != ReaderMode::FilePlayback || m_in_buf.size() < 8)
        return false;
    const qint64 ms = bcd_ms_of(m_in_buf.left(8));
    if (ms < 0) return false;
    m_playback_base_ms = ms;
    m_first_frame = true;
    m_last_ntb = 0;
    m_in_buf.remove(0, 8);
    return true;
}

void ReaderWorker::try_extract_frame() {
    while (!m_in_buf.isEmpty()) {
        if (!m_get3c) {
            // 段间 8B 标注可能独占本段缓冲(0x3C 在下一次 1024B read)
            if (try_consume_bcd_tag())
                continue;

            int idx = m_in_buf.indexOf(char(0x3C));
            if (idx < 0) {
                // 无 0x3C:剔除 1 字节,回到循环顶重新匹配 8B BCD(滑窗对齐帧头)
                m_in_buf.remove(0, 1);
                continue;
            }
            if (idx > 0) {
                // 0x3C 前紧贴完整 8B 标注(标注前允许有噪声)
                if (m_cfg.mode == ReaderMode::FilePlayback && idx >= 8
                    && bcd_ms_of(m_in_buf.mid(idx - 8, 8)) >= 0) {
                    if (idx > 8)
                        m_in_buf.remove(0, idx - 8);
                    if (!try_consume_bcd_tag() && m_in_buf.size() >= 8)
                        m_in_buf.remove(0, 8);
                    continue;
                }
                // 1~7B 前缀不可能是完整标注:合法 BCD 不含 0x3C,
                // 半截标注且尚无 0x3C 已在 idx<0 分支等待。
                // 串口/回放都立刻丢掉,否则已缓冲的完整帧被卡住。
                m_in_buf.remove(0, idx);
                continue;
            }
            // idx == 0:3C 在缓冲开头。验证是否为假帧头——
            // 帧体中的 0x3C/0x3E 均已 0x3D 转义,故合法帧 3C 后应直达 3E;
            // 若 3C 后先遇到另一个 3C(而非 3E),说明第一个 3C 是孤立的假帧头,
            // 丢弃它,以第二个 3C 为帧头重新对齐。
            const int next_3c = m_in_buf.indexOf(char(0x3C), idx + 1);
            const int next_3e = m_in_buf.indexOf(char(0x3E), idx + 1);
            if (next_3c >= 0 && next_3e >= 0 && next_3c < next_3e) {
                m_in_buf.remove(0, 1);   // 第一个 3C 失效,丢弃
                continue;
            }
            m_in_buf.remove(0, 1);
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
        // 新 bin:时间轴以帧内 NTB(u32,40 µs)差推进——
        //   首帧  frame_time = 文件头/段 8B 标注时刻
        //   后续帧 frame_time = 上一帧 frame_time + (本帧 NTB − 上一帧 NTB)×40 µs
        //   帧间 NTB 差超出合理范围(长时间无报文/跨 u32 回绕)→ 用本地时刻,不
        //   沿用上一帧 NTB(本帧为断点,重置链)
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
                bf.meta.seg_start = true;   // 跨段断点:Delta 不计算
            } else {
                const qint64 dn = (qint32)(ntb - m_last_ntb);   // 回绕安全
                if (dn > 0 && dn <= playback::kMaxNtbGapTicks) {
                    bf.arrival_ms = m_last_ft + playback::ntb_to_us(quint32(dn)) / 1000;
                } else {
                    bf.arrival_ms = QDateTime::currentMSecsSinceEpoch();  // 断点:本地时刻
                }
                m_last_ft = bf.arrival_ms;
                m_last_ntb = ntb;
            }
        } else {
            // 实时串口:无 8B 标注,arrival=本地时刻;断段判断用本地接收时间差
            // (超过 NTB u32 回绕周期 ≈171.8s 视为断段,标 seg_start)
            const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
            bf.arrival_ms = now_ms;
            if (m_cfg.mode == ReaderMode::SerialPort) {
                if (m_last_local_ms != 0
                    && now_ms - m_last_local_ms > playback::kMaxLocalGapMs)
                    bf.meta.seg_start = true;   // 长时间无报文 → 断段
                m_last_local_ms = now_ms;
            }
        }
        bf.arrival_us = m_frame_rx_us;   // 0x3C 起始高精度接收时刻(实时)
        bf.raw_wire   = wire;            // 原始串口帧原样(调试复制)
        // 帧 ts 域语义(2026-09 统一):ts 一律为 NTB tick(25kHz,40ns),
        // 无论实时串口(设备填)还是文件回放(bin/裸 hex 导出均写 NTB)
        bf.meta.frame_ts_is_ntb = true;
        // 8B 标注在文件头/段间,帧体无逐帧 BCD;不做旧版每帧 BCD 兼容检测
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
    // 8B BCD 时间标注行(非 0x3C 开头,裸 hex 多段):解析记录(断点/基准)
    if (raw.size() == 8 && (quint8)raw[0] != 0x3C
        && playback::looks_like_bcd8(raw)) {
        m_raw_base_ms = bcd_ms_of(raw);
        return;
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
        // 时间:ts 统一 NTB tick(25kHz,40ns)。段首帧用 TIME 头(m_raw_base_ms)
        // 作基准;段内按 NTB 差×40ns 推进(与 bin 回放同构);断段(差超阈值/回绕)
        // 回退本地时刻。无 TIME 头则回退本地。
        const quint32 ts_le2 = (quint32)(quint8)data[2]
                             | (quint32)(quint8)data[3] << 8
                             | (quint32)(quint8)data[4] << 16
                             | (quint32)(quint8)data[5] << 24;
        qint64 t2;
        bool seg_start = false;
        if (m_raw_base_ms >= 0) {
            if (m_hex_seg_first) {
                t2 = m_raw_base_ms;
                m_hex_seg_first = false;
                seg_start = true;
            } else {
                const qint64 dn = (qint32)(ts_le2 - m_last_hex_ts);   // NTB 差(tick)
                if (dn > 0 && dn <= playback::kMaxNtbGapTicks)
                    t2 = m_last_hex_ft + playback::ntb_to_us(quint32(dn)) / 1000;
                else
                    t2 = QDateTime::currentMSecsSinceEpoch();   // 断段:回退本地
            }
            m_last_hex_ts = ts_le2;
            m_last_hex_ft = t2;
        } else {
            // 无 TIME 头:ts4=NTB 无法还原绝对时刻,回退本地
            t2 = QDateTime::currentMSecsSinceEpoch();
        }

        BplcFrame bf;
        bf.meta.from_raw = false;
        bf.meta.has_time_tag = false;
        bf.meta.frame_ts_is_ntb = true;
        bf.meta.seg_start = seg_start;
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
    // 8B BCD:[年-2000][月][日][时][分][秒][毫秒百位][毫秒低2]→ 本地 epoch ms
    if (!playback::looks_like_bcd8(b)) return -1;
    auto d2 = [](quint8 x) { return int((x >> 4) * 10 + (x & 0x0F)); };
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
    connect(m_worker, &ReaderWorker::progress_percent, this, &SerialReader::on_progress);

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
void SerialReader::on_progress(int p)   { emit progress_percent(p); }
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
