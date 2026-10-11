/// @file plugin_manager.cpp
/// @brief PluginManager 实现
#include "plugin_manager.h"
#include "plugin_graphics_view.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutexLocker>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QThread>

#include "plugin_ipc.h"
#include "plugin_serialization.h"
#include "plugin_parser_proxy.h"
#include "plugin_dissector_proxy.h"
#include "protocolfactory.h"

namespace {
constexpr int kHandshakeTimeoutMs = 10000;
constexpr int kParseTimeoutMs = 5000;
constexpr int kHeartbeatIntervalMs = 5000;
constexpr int kHeartbeatTimeoutMs = 15000;
} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
namespace {
/// Job Object(KILL_ON_JOB_CLOSE):主进程退出/被强杀后其句柄随进程关闭,
/// OS 连带终止组内全部 host 子进程——父死子亡的 OS 级兜底(第三重保障)。
HANDLE g_kill_job = nullptr;
void assign_to_kill_job(QProcess* proc) {
    if (!g_kill_job) {
        g_kill_job = CreateJobObjectW(nullptr, nullptr);
        if (g_kill_job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(g_kill_job,
                                    JobObjectExtendedLimitInformation,
                                    &info, sizeof(info));
        }
    }
    if (!g_kill_job) return;
    HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                           static_cast<DWORD>(proc->processId()));
    if (h) {
        AssignProcessToJobObject(g_kill_job, h);   // 失败(嵌套 Job 等)不阻断
        CloseHandle(h);
    }
}
}  // namespace
#endif  // Q_OS_WIN

PluginManager& PluginManager::instance() {
    static PluginManager inst;
    return inst;
}

PluginManager::PluginManager(QObject* parent) : QObject(parent) {
    m_hb_timer.setInterval(kHeartbeatIntervalMs);
    connect(&m_hb_timer, &QTimer::timeout, this, &PluginManager::checkHeartbeats);
    m_hb_timer.start();
}

PluginManager::~PluginManager() {
    stop_all();
}

void PluginManager::stop_all() {
    if (m_plugins.isEmpty()) return;
    const QList<PluginRuntime*> runtimes = m_plugins.values();
    m_plugins.clear();
    for (auto* rt : runtimes) {
        if (rt->manifest.dissector) {
            unregister_parser(rt->manifest.protocol_id);
            emit dissector_unregistered(rt->manifest.protocol_id);
        }
        if (rt->socket) {
            // 优雅退出
            QByteArray p;
            send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::Shutdown), p);
            rt->socket->flush();
            rt->socket->disconnectFromServer();
        }
        if (rt->process) {
            rt->process->terminate();
            if (!rt->process->waitForFinished(2000)) {
                rt->process->kill();
                rt->process->waitForFinished(1000);
            }
            delete rt->process;
        }
        delete rt->server;
        delete rt;
    }
}

void PluginManager::loadAll(const QString& plugins_dir) {
    const QDir d(plugins_dir);
    if (!d.exists()) return;
    for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const PluginManifest m = read_plugin_manifest(d.filePath(sub));
        if (!m.valid) continue;  // 无效清单跳过(可日志)
        if (m.runtime != QStringLiteral("native")
            && m.runtime != QStringLiteral("js")
            && m.runtime != QStringLiteral("lua")) continue;  // Phase2: native/js/lua
        if (m_plugins.contains(m.protocol_id)) continue;      // 重复协议 id
        start_plugin(m);
    }
}

QStringList PluginManager::pluginProtocolIds() const {
    return m_plugins.keys();
}

bool PluginManager::is_plugin_protocol(const QString& protocol_id) const {
    auto it = m_plugins.find(protocol_id);
    return it != m_plugins.end() && (*it)->ready && !(*it)->disabled;
}

static QString hostBinaryPath() {
    // bplc-plugin-host 与主程序同目录
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/bplc-plugin-host");
}

bool PluginManager::start_plugin(const PluginManifest& m) {
    auto* rt = new PluginRuntime;
    rt->manifest = m;
    const QString sock_name = plugin_ipc::socketName(m.name);
    QLocalServer::removeServer(sock_name);

    rt->server = new QLocalServer(this);
    if (!rt->server->listen(sock_name)) {
        delete rt;
        return false;
    }
    const QString pid = m.protocol_id;
    connect(rt->server, &QLocalServer::newConnection,
            this, [this, pid]() { on_new_connection(pid); });

    rt->process = new QProcess(this);
    connect(rt->process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, pid](int code, QProcess::ExitStatus) {
                on_process_finished(pid, code);
            });
    connect(rt->process, &QProcess::errorOccurred,
            this, [this, pid]() { on_process_error(pid); });

    rt->process->start(hostBinaryPath(),
        {QStringLiteral("--socket"), sock_name,
         QStringLiteral("--plugin"), m.dir_path,
         QStringLiteral("--parent-pid"),
         QString::number(QCoreApplication::applicationPid())});
    if (!rt->process->waitForStarted(5000)) {
        delete rt;
        return false;
    }
#ifdef Q_OS_WIN
    assign_to_kill_job(rt->process);   // 主进程被杀时 OS 连带终止 host
#endif
    m_plugins.insert(pid, rt);
    return true;
}

void PluginManager::on_new_connection(const QString& plugin_id) {
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return;
    PluginRuntime* rt = *it;
    rt->socket = rt->server->nextPendingConnection();
    connect(rt->socket, &QLocalSocket::readyRead,
            this, [this, plugin_id]() { on_host_ready_read(plugin_id); });
    connect(rt->socket, &QLocalSocket::disconnected,
            this, [this, plugin_id]() {
                disable_plugin(plugin_id, QStringLiteral("host disconnected"));
            });
}

void PluginManager::on_host_ready_read(const QString& plugin_id) {
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end() || !(*it)->socket) return;
    PluginRuntime* rt = *it;
    rt->rx_buf.append(rt->socket->readAll());
    while (rt->rx_buf.size() >= 5) {
        QDataStream hdr(rt->rx_buf);
        hdr.setVersion(plugin_ipc::kStreamVersion);
        quint32 len = 0;
        hdr >> len;
        if (rt->rx_buf.size() < 4 + (int)len) break;
        const QByteArray frame = rt->rx_buf.mid(4, len);
        rt->rx_buf.remove(0, 4 + len);
        QDataStream ds(frame);
        ds.setVersion(plugin_ipc::kStreamVersion);
        quint8 t = 0;
        ds >> t;
        handle_message(plugin_id, t, ds);
    }
}

void PluginManager::handle_message(const QString& plugin_id, quint8 type,
                                  QDataStream& ds) {
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return;
    PluginRuntime* rt = *it;
    const auto t = static_cast<plugin_ipc::MsgType>(type);
    switch (t) {
    case plugin_ipc::MsgType::Hello: {
        int api_ver = 0; QString name, proto;
        ds >> api_ver >> name >> proto;
        QByteArray p;
        QDataStream o(&p, QIODevice::WriteOnly);
        o.setVersion(plugin_ipc::kStreamVersion);
        const bool ok = (api_ver == plugin_ipc::kApiVersion)
                     && (proto == rt->manifest.protocol_id);
        o << ok;
        o << (ok ? QString() : QStringLiteral("api_version/protocol mismatch"));
        send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::HelloAck), p);
        if (ok) {
            rt->ready = true;
            rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
            // 仅解剖器插件注册到解析器工厂(插件协议 id → 解剖代理)。
            // 面板类插件不注册:避免被选为解码器产生空心解析(见设计文档)。
            if (rt->manifest.dissector) {
                const QString pid = rt->manifest.protocol_id;
                register_parser(pid, [pid]() { return make_dissector_parser(pid); });
                emit dissector_registered(pid);
            }
            emit plugin_status_changed(plugin_id, QStringLiteral("ready"));
        } else {
            disable_plugin(plugin_id, QStringLiteral("handshake failed"));
        }
        break;
    }
    case plugin_ipc::MsgType::Pong: {
        quint64 seq = 0; ds >> seq; Q_UNUSED(seq);
        rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
        break;
    }
    case plugin_ipc::MsgType::Error: {
        quint64 seq = 0; QString msg;
        ds >> seq >> msg; Q_UNUSED(seq);
        // 解析错误不禁用插件,仅记录(可日志)
        break;
    }
    case plugin_ipc::MsgType::RequestRedraw: {
        // 插件主动请求重绘:通知所有注册的视图
        for (PluginGraphicsView* v : m_graphics_views.value(plugin_id))
            v->on_plugin_request_redraw();
        break;
    }
    case plugin_ipc::MsgType::PluginMessage: {
        // 插件间通讯:host 插件发出,主进程总线按目标名中转(见 MainWindow 接线)
        QString from, to, topic; QByteArray mp;
        ds >> from >> to >> topic >> mp;
        emit plugin_message_received(from, to, topic, mp);
        // 目标也是 host 插件时直接在本层转发(无需绕 UI 层)
        if (!to.isEmpty() && to != rt->manifest.name)
            send_plugin_message(to, from, topic, mp);
        break;
    }
    default:
        break;
    }
}

void PluginManager::send_message(PluginRuntime* rt, quint8 type,
                                const QByteArray& payload) {
    if (!rt->socket) return;
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setVersion(plugin_ipc::kStreamVersion);
    ds << quint32(1 + payload.size()) << type;
    frame.append(payload);
    rt->socket->write(frame);
    rt->socket->flush();
}

bool PluginManager::parse_via_plugin(const QString& protocol_id,
                                   const BplcFrame& frame, MsduState& msdu,
                                   const ParseFilter& filter,
                                   ParseResult* result) {
    auto it = m_plugins.find(protocol_id);
    if (it == m_plugins.end()) return false;
    PluginRuntime* rt = *it;
    if (!rt->ready || rt->disabled || !rt->socket) return false;

    QMutexLocker lock(&rt->ipc_mutex);
    const quint64 seq = ++rt->seq;

    QByteArray payload;
    QDataStream o(&payload, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq << frame << msdu << filter;
    send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::ParseRequest), payload);

    // 阻塞等待响应(worker 线程调用,不阻塞 UI)
    QByteArray resp_buf;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kParseTimeoutMs;
    bool got = false;
    ParseResult r;
    MsduState new_msdu;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!rt->socket->waitForReadyRead(100)) {
            if (rt->socket->state() != QLocalSocket::ConnectedState) break;
            continue;
        }
        resp_buf.append(rt->socket->readAll());
        // 尝试解析完整帧
        while (resp_buf.size() >= 5) {
            QDataStream hdr(resp_buf);
            hdr.setVersion(plugin_ipc::kStreamVersion);
            quint32 len = 0; hdr >> len;
            if (resp_buf.size() < 4 + (int)len) break;
            const QByteArray fr = resp_buf.mid(4, len);
            resp_buf.remove(0, 4 + len);
            QDataStream ds(fr);
            ds.setVersion(plugin_ipc::kStreamVersion);
            quint8 t = 0; ds >> t;
            if (static_cast<plugin_ipc::MsgType>(t)
                    == plugin_ipc::MsgType::ParseResponse) {
                quint64 rseq = 0; bool ok = false;
                ds >> rseq;
                if (rseq != seq) continue;  // 非本次请求的响应(理论上不应发生)
                ds >> ok;
                if (ok) { ds >> r >> new_msdu; }
                else { QString e; ds >> e; r.reject_reason = e; }
                r.accept = ok;
                got = true;
                break;
            }
            // Pong 等其他消息:更新心跳后继续等
            if (static_cast<plugin_ipc::MsgType>(t) == plugin_ipc::MsgType::Pong)
                rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
        }
        if (got) break;
    }
    if (!got) {
        r.accept = false;
        r.reject_reason = QStringLiteral("plugin parse timeout/crash");
        // 超时很可能意味着 host 挂了,禁用插件
        // 注意:不能在 worker 线程直接调 disable_plugin(涉及信号),标记即可
        rt->disabled = true;
        rt->disable_reason = r.reject_reason;
        QMetaObject::invokeMethod(this, [this, protocol_id]() {
            emit plugin_status_changed(protocol_id, QStringLiteral("timeout-disabled"));
        }, Qt::QueuedConnection);
        return false;
    }
    *result = r;
    msdu = new_msdu;
    return true;
}

QStringList PluginManager::dissector_protocol_ids() const {
    QStringList out;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        const PluginRuntime* rt = it.value();
        if (rt->manifest.dissector && rt->ready && !rt->disabled)
            out << it.key();
    }
    return out;
}

bool PluginManager::is_dissector_protocol(const QString& protocol_id) const {
    auto it = m_plugins.constFind(protocol_id);
    if (it == m_plugins.constEnd()) return false;
    const PluginRuntime* rt = it.value();
    return rt->manifest.dissector && rt->ready && !rt->disabled;
}

QString PluginManager::dissector_display_name(const QString& protocol_id) const {
    auto it = m_plugins.constFind(protocol_id);
    if (it == m_plugins.constEnd()) return protocol_id;
    const PluginManifest& m = it.value()->manifest;
    return m.display_name.isEmpty() ? protocol_id : m.display_name;
}

bool PluginManager::dissect_via_plugin(const QString& protocol_id,
                                       const QByteArray& payload,
                                       const FrameContext& ctx,
                                       DissectResult* out) {
    auto it = m_plugins.find(protocol_id);
    if (it == m_plugins.end()) return false;
    PluginRuntime* rt = *it;
    if (!rt->ready || rt->disabled || !rt->socket) return false;

    QMutexLocker lock(&rt->ipc_mutex);
    const quint64 seq = ++rt->seq;

    QByteArray body;
    QDataStream o(&body, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq << payload << ctx;
    send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::DissectRequest), body);

    // 阻塞等待响应(分发 worker 线程调用,不阻塞 UI;与 parse_via_plugin 同构)
    QByteArray resp_buf;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kParseTimeoutMs;
    bool got = false;
    bool ok = false;
    DissectResult r;
    QString err_text;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!rt->socket->waitForReadyRead(100)) {
            if (rt->socket->state() != QLocalSocket::ConnectedState) break;
            continue;
        }
        resp_buf.append(rt->socket->readAll());
        while (resp_buf.size() >= 5) {
            QDataStream hdr(resp_buf);
            hdr.setVersion(plugin_ipc::kStreamVersion);
            quint32 len = 0; hdr >> len;
            if (resp_buf.size() < 4 + (int)len) break;
            const QByteArray fr = resp_buf.mid(4, len);
            resp_buf.remove(0, 4 + len);
            QDataStream ds(fr);
            ds.setVersion(plugin_ipc::kStreamVersion);
            quint8 t = 0; ds >> t;
            if (static_cast<plugin_ipc::MsgType>(t)
                    == plugin_ipc::MsgType::DissectResponse) {
                quint64 rseq = 0;
                ds >> rseq;
                if (rseq != seq) continue;
                ds >> ok;
                if (ok) ds >> r;
                else ds >> err_text;
                got = true;
                break;
            }
            if (static_cast<plugin_ipc::MsgType>(t) == plugin_ipc::MsgType::Pong)
                rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
            if (static_cast<plugin_ipc::MsgType>(t) == plugin_ipc::MsgType::PluginMessage) {
                QString from, to, topic; QByteArray mp;
                ds >> from >> to >> topic >> mp;
                emit plugin_message_received(from, to, topic, mp);
            }
        }
        if (got) break;
    }
    if (!got || !ok) {
        if (!got) {
            // 超时/断连:很可能 host 已挂,禁用插件并注销协议(回主线程执行)
            const bool was_dissector = rt->manifest.dissector;
            rt->disabled = true;
            rt->disable_reason = QStringLiteral("plugin dissect timeout/crash");
            QMetaObject::invokeMethod(this, [this, protocol_id, was_dissector]() {
                if (was_dissector) {
                    unregister_parser(protocol_id);
                    emit dissector_unregistered(protocol_id);
                }
                emit plugin_status_changed(protocol_id,
                                           QStringLiteral("timeout-disabled"));
            }, Qt::QueuedConnection);
        }
        return false;
    }
    *out = r;
    return true;
}

void PluginManager::reset_dissectors() {
    for (auto* rt : m_plugins) {
        if (!rt->manifest.dissector || !rt->ready || rt->disabled || !rt->socket)
            continue;
        QMutexLocker lock(&rt->ipc_mutex);
        send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::ResetDissector),
                     QByteArray());
    }
}

bool PluginManager::send_plugin_message(const QString& to_name,
                                        const QString& from_name,
                                        const QString& topic,
                                        const QByteArray& payload) {
    for (auto* rt : m_plugins) {
        if (rt->manifest.name != to_name) continue;
        if (!rt->ready || rt->disabled || !rt->socket) return false;
        QMutexLocker lock(&rt->ipc_mutex);
        QByteArray body;
        QDataStream o(&body, QIODevice::WriteOnly);
        o.setVersion(plugin_ipc::kStreamVersion);
        o << from_name << to_name << topic << payload;
        send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::PluginMessage),
                     body);
        return true;
    }
    return false;
}

void PluginManager::on_process_finished(const QString& plugin_id, int code) {
    Q_UNUSED(code);
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return;
    PluginRuntime* rt = *it;
    if (!rt->disabled) {
        // 非预期退出(未主动禁用):视为崩溃
        disable_plugin(plugin_id, QStringLiteral("host process exited unexpectedly"));
    }
}

void PluginManager::on_process_error(const QString& plugin_id) {
    disable_plugin(plugin_id, QStringLiteral("host process error"));
}

void PluginManager::disable_plugin(const QString& plugin_id,
                                  const QString& reason) {
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end() || (*it)->disabled) return;
    PluginRuntime* rt = *it;
    rt->disabled = true;
    rt->disable_reason = reason;
    rt->ready = false;
    if (rt->manifest.dissector) {
        unregister_parser(rt->manifest.protocol_id);
        emit dissector_unregistered(rt->manifest.protocol_id);
    }
    emit plugin_status_changed(plugin_id, QStringLiteral("disabled: ") + reason);
}

void PluginManager::checkHeartbeats() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        PluginRuntime* rt = it.value();
        if (!rt->ready || rt->disabled || !rt->socket) continue;
        // 发 Ping
        {
            QMutexLocker lock(&rt->ipc_mutex);
            QByteArray p;
            QDataStream o(&p, QIODevice::WriteOnly);
            o.setVersion(plugin_ipc::kStreamVersion);
            o << ++rt->seq;
            send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::Ping), p);
        }
        if (now - rt->last_pong_ms > kHeartbeatTimeoutMs) {
            disable_plugin(it.key(), QStringLiteral("heartbeat timeout"));
        }
    }
}

// ---- 图形(Phase3) ----

namespace {
constexpr int kGraphicsTimeoutMs = 5000;
/// @brief 同步等待指定 seq 的响应(主线程调用,带事件循环防假死)
template <typename Handler>
bool wait_graphics_response(PluginRuntime* rt, quint64 seq,
                            plugin_ipc::MsgType want, Handler&& on_frame) {
    QByteArray resp_buf;
    const qint64 deadline =
        QDateTime::currentMSecsSinceEpoch() + kGraphicsTimeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        // 主线程:用事件循环等待,避免界面假死
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        QObject::connect(rt->socket, &QLocalSocket::readyRead,
                         &loop, &QEventLoop::quit);
        loop.exec();
        if (rt->socket->state() != QLocalSocket::ConnectedState) break;
        resp_buf.append(rt->socket->readAll());
        while (resp_buf.size() >= 5) {
            QDataStream hdr(resp_buf);
            hdr.setVersion(plugin_ipc::kStreamVersion);
            quint32 len = 0; hdr >> len;
            if (resp_buf.size() < 4 + static_cast<int>(len)) break;
            const QByteArray fr = resp_buf.mid(4, len);
            resp_buf.remove(0, 4 + len);
            QDataStream ds(fr);
            ds.setVersion(plugin_ipc::kStreamVersion);
            quint8 t = 0; ds >> t;
            const auto mt = static_cast<plugin_ipc::MsgType>(t);
            if (mt == want) {
                quint64 rseq = 0; ds >> rseq;
                if (rseq != seq) continue;
                on_frame(ds);
                return true;
            }
            if (mt == plugin_ipc::MsgType::Pong)
                rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
            // RequestRedraw 可能在等待中到达:直接处理
            if (mt == plugin_ipc::MsgType::RequestRedraw) {
                // 由外层 handle_message 统一处理,这里只标记
                // (简化:忽略,视图的定时器会补)
            }
        }
    }
    return false;
}
} // namespace

bool PluginManager::has_graphics(const QString& protocol_id) const {
    auto it = m_plugins.find(protocol_id);
    if (it == m_plugins.end()) return false;
    return (*it)->manifest.graphics;
}

QImage PluginManager::request_render(const QString& protocol_id,
                                     int w, int h) {
    auto it = m_plugins.find(protocol_id);
    if (it == m_plugins.end()) return QImage();
    PluginRuntime* rt = *it;
    if (!rt->ready || rt->disabled || !rt->socket) return QImage();
    if (!rt->manifest.graphics) return QImage();

    QMutexLocker lock(&rt->ipc_mutex);
    const quint64 seq = ++rt->seq;
    QByteArray payload;
    QDataStream o(&payload, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq << w << h;
    send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::RenderRequest),
                 payload);

    QImage img;
    const bool ok = wait_graphics_response(
        rt, seq, plugin_ipc::MsgType::RenderResponse,
        [&img](QDataStream& ds) {
            bool rok = false;
            ds >> rok >> img;
            if (!rok) img = QImage();
        });
    return ok ? img : QImage();
}

bool PluginManager::send_graphics_event(const QString& protocol_id,
                                        const GraphicsEvent& e) {
    auto it = m_plugins.find(protocol_id);
    if (it == m_plugins.end()) return false;
    PluginRuntime* rt = *it;
    if (!rt->ready || rt->disabled || !rt->socket) return false;
    if (!rt->manifest.graphics) return false;

    QMutexLocker lock(&rt->ipc_mutex);
    const quint64 seq = ++rt->seq;
    QByteArray payload;
    QDataStream o(&payload, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq << e;
    send_message(rt,
                 static_cast<quint8>(plugin_ipc::MsgType::GraphicsEventMsg),
                 payload);

    bool redraw = false;
    wait_graphics_response(
        rt, seq, plugin_ipc::MsgType::EventAck,
        [&redraw](QDataStream& ds) { ds >> redraw; });
    return redraw;
}

void PluginManager::register_graphics_view(const QString& protocol_id,
                                           PluginGraphicsView* view) {
    m_graphics_views[protocol_id].append(view);
    connect(view, &QObject::destroyed, this,
            [this, protocol_id, view]() { unregister_graphics_view(protocol_id, view); });
}

void PluginManager::unregister_graphics_view(const QString& protocol_id,
                                             PluginGraphicsView* view) {
    m_graphics_views[protocol_id].removeAll(view);
}
