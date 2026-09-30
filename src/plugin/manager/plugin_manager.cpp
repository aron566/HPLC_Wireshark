/// @file plugin_manager.cpp
/// @brief PluginManager 实现
#include "plugin_manager.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutexLocker>
#include <QProcess>
#include <QThread>

#include "plugin_ipc.h"
#include "plugin_serialization.h"
#include "plugin_parser_proxy.h"
#include "protocolfactory.h"

namespace {
constexpr int kHandshakeTimeoutMs = 10000;
constexpr int kParseTimeoutMs = 5000;
constexpr int kHeartbeatIntervalMs = 5000;
constexpr int kHeartbeatTimeoutMs = 15000;
} // namespace

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
    for (auto* rt : m_plugins) {
        if (rt->socket) {
            // 优雅退出
            QByteArray p;
            send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::Shutdown), p);
            rt->socket->flush();
        }
        if (rt->process) {
            rt->process->terminate();
            rt->process->waitForFinished(2000);
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
            && m.runtime != QStringLiteral("js")) continue;  // Phase2: native/js
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
         QStringLiteral("--plugin"), m.dir_path});
    if (!rt->process->waitForStarted(5000)) {
        delete rt;
        return false;
    }
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
            // 注册到解析器工厂(插件协议 id → 代理)
            const QString pid = rt->manifest.protocol_id;
            register_parser(pid, [pid]() { return make_plugin_parser(pid); });
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
    unregister_parser(rt->manifest.protocol_id);
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
