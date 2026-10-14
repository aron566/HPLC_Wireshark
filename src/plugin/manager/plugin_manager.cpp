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

/// 解剖器在工厂/信号/下拉中的归一化键:注册键统一大写(配置侧存小写、
/// 分发器转大写匹配),清单 id 含小写字母时也命中同一键,不会静默回退。
QString dissector_key(const PluginRuntime* rt) {
    return rt->manifest.protocol_id.toUpper();
}
/// 是否真正可注册为解剖器:清单声明 + host 上报了实际解剖能力
/// (脚本后端本期无 dissect 通路,仅声明不算数)。
bool dissector_eligible(const PluginRuntime* rt) {
    return rt->manifest.dissector && rt->has_dissector;
}
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
    // 退役运行时本体在此统一释放:管理器是进程级单例,析构时已无
    // 请求线程存活,迟到指针不可能再被解引用
    for (auto* rt : m_retired) delete rt;
    m_retired.clear();
}

void PluginManager::stop_all() {
    if (m_plugins.isEmpty()) return;
    const QList<PluginRuntime*> runtimes = m_plugins.values();
    m_plugins.clear();
    // 第一遍:先置禁用+退役并注销协议,断绝新请求进入(各请求入口都先
    // 查 disabled/ready);在途请求(分发线程正持 ipc_mutex 等响应)不受
    // 影响;已取到 rt 指针、正排队等 ipc_mutex 的迟到请求,在拿锁后会
    // 复查 retired 并立即退出(见 parse_via_plugin/dissect_via_plugin)。
    for (auto* rt : runtimes) {
        rt->disabled = true;
        rt->ready = false;
        rt->retired.store(true);
        abort_response_waiters(rt);   // 在途等待先醒来放锁,第二遍才不等满超时
        if (dissector_eligible(rt)) {
            unregister_parser(dissector_key(rt));
            emit dissector_unregistered(dissector_key(rt));
        }
    }
    // 第二遍:逐个拿 ipc_mutex 等在途请求收尾(最长等其请求超时 5s),
    // 再关停进程并释放重对象(socket/server/进程)——与全系统"持锁
    // 贯穿请求"的纪律一致,避免采集进行中从市场卸插件/退出时与分发
    // 线程并发删除这些对象(use-after-free)。运行时本体(含 mutex)
    // 不在此 delete:可能仍有请求线程堵在其 mutex 上,本体转入
    // m_retired,留待管理器析构(进程退出、无请求线程存活时)释放。
    for (auto* rt : runtimes) {
        {
            QMutexLocker lock(&rt->ipc_mutex);
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
                rt->process = nullptr;
            }
            delete rt->server;
            rt->server = nullptr;
            rt->socket = nullptr;   // socket 随 server 一并销毁
        }
        m_retired.append(rt);
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
        const auto mt = static_cast<plugin_ipc::MsgType>(t);
        if (mt == plugin_ipc::MsgType::ParseResponse
            || mt == plugin_ipc::MsgType::DissectResponse
            || mt == plugin_ipc::MsgType::RenderResponse
            || mt == plugin_ipc::MsgType::EventAck) {
            // 响应帧:唯一读者在此把载荷按 seq 投递到响应邮箱,等待方
            // (wait_response)自取——绝不能落进 handle_message(它没有
            // 响应分支,响应会被静默丢弃,等待方超时误判插件崩溃)。
            QDataStream probe(frame);
            probe.setVersion(plugin_ipc::kStreamVersion);
            quint8 pt = 0;
            quint64 rseq = 0;
            probe >> pt >> rseq;   // 四类响应载荷均以 seq 起头
            {
                QMutexLocker rl(&rt->resp_mutex);
                rt->resp_body = frame.mid(1);   // 类型字节之后(以 seq 起头)
                rt->resp_seq = rseq;
                rt->resp_ready = true;
            }
            rt->resp_cond.wakeAll();
            continue;
        }
        handle_message(plugin_id, t, ds);
    }
}

bool PluginManager::wait_response(PluginRuntime* rt, quint64 seq,
                                  int timeout_ms, QByteArray* body) {
    QMutexLocker lock(&rt->resp_mutex);
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (true) {
        if (rt->resp_aborted) return false;
        if (rt->resp_ready && rt->resp_seq == seq) {
            *body = rt->resp_body;
            rt->resp_ready = false;
            return true;
        }
        const qint64 remain = deadline - QDateTime::currentMSecsSinceEpoch();
        if (remain <= 0) return false;
        rt->resp_cond.wait(&rt->resp_mutex,
                           static_cast<unsigned long>(qMin<qint64>(remain, 100)));
    }
}

void PluginManager::abort_response_waiters(PluginRuntime* rt) {
    {
        QMutexLocker rl(&rt->resp_mutex);
        rt->resp_aborted = true;
    }
    rt->resp_cond.wakeAll();
}

void PluginManager::handle_message(const QString& plugin_id, quint8 type,
                                  QDataStream& ds) {
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return;
    PluginRuntime* rt = *it;
    const auto t = static_cast<plugin_ipc::MsgType>(type);
    switch (t) {
    case plugin_ipc::MsgType::Hello: {
        int api_ver = 0; QString name, proto; bool cap_dissector = false;
        QVector<DissectColumn> cap_columns;
        QVector<DissectColorRule> cap_colors;
        ds >> api_ver >> name >> proto >> cap_dissector >> cap_columns
           >> cap_colors;
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
            rt->has_dissector = cap_dissector;
            rt->dissector_columns = cap_columns;
            rt->dissector_color_rules = cap_colors;
            rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
            // 仅真解剖器插件注册到解析器工厂(插件协议 id → 解剖代理)。
            // 面板类插件不注册:避免被选为解码器产生空心解析(见设计文档)。
            if (rt->manifest.dissector && !cap_dissector) {
                // 清单声明了解剖但后端没有解剖通路(如脚本后端本期未
                // 支持):不注册并给出原因,避免被选上后每帧往返换拒帧。
                emit plugin_status_changed(
                    plugin_id,
                    QStringLiteral("dissector declared but backend has no "
                                   "dissect channel; not registered"));
            } else if (dissector_eligible(rt)) {
                if (is_builtin_protocol(dissector_key(rt))) {
                    // id 与内置协议同名:拒绝注册(工厂层亦有防覆盖),
                    // 否则内置协议会被影射、注销时被连带删除。
                    disable_plugin(plugin_id, QStringLiteral(
                        "dissector protocol id collides with builtin protocol"));
                    break;
                }
                const QString pid = rt->manifest.protocol_id;
                const QString key = dissector_key(rt);
                register_parser(key, [pid]() { return make_dissector_parser(pid); });
                emit dissector_registered(key);
            }
            emit plugin_status_changed(plugin_id, QStringLiteral("ready"));
        } else {
            disable_plugin(plugin_id, QStringLiteral("handshake failed"));
        }
        break;
    }
    case plugin_ipc::MsgType::Pong: {
        quint64 seq = 0; ds >> seq;
        rt->last_pong_ms = QDateTime::currentMSecsSinceEpoch();
        if (seq == rt->awaiting_pong_seq) rt->awaiting_pong_seq = 0;
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
        route_plugin_message(from, to, topic, mp);
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
    // 拿锁后复查退役标志:stop_all 可能在我们排队等锁期间已关停该
    // 运行时(socket/进程已释放),此时立即退出,不得再触碰其 IPC 对象
    if (rt->retired.load()) return false;
    const quint64 seq = ++rt->seq;

    QByteArray payload;
    QDataStream o(&payload, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq << frame << msdu << filter;
    send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::ParseRequest), payload);

    // 阻塞等待响应邮箱投递(worker 线程调用,不阻塞 UI;socket 只由主
    // 线程的 on_host_ready_read 读取——等待期间到达的 Pong/总线消息也
    // 由它统一分发路由,不会被等待方吞掉或抢走)
    ParseResult r;
    MsduState new_msdu;
    bool got = false;
    QByteArray resp_body;
    if (wait_response(rt, seq, kParseTimeoutMs, &resp_body)) {
        QDataStream ds(resp_body);
        ds.setVersion(plugin_ipc::kStreamVersion);
        quint64 rseq = 0;
        bool ok = false;
        ds >> rseq >> ok;   // seq 已由投递方核对
        if (ok) { ds >> r >> new_msdu; }
        else { QString e; ds >> e; r.reject_reason = e; }
        r.accept = ok;
        got = true;
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
        if (dissector_eligible(rt) && rt->ready && !rt->disabled)
            out << dissector_key(rt);
    }
    return out;
}

bool PluginManager::is_dissector_protocol(const QString& protocol_id) const {
    const QString key = protocol_id.toUpper();   // 归一化键比对(大小写不敏感)
    for (const PluginRuntime* rt : m_plugins) {
        if (dissector_key(rt) == key && dissector_eligible(rt)
            && rt->ready && !rt->disabled)
            return true;
    }
    return false;
}

QString PluginManager::dissector_display_name(const QString& protocol_id) const {
    const QString key = protocol_id.toUpper();
    for (const PluginRuntime* rt : m_plugins) {
        if (dissector_key(rt) == key) {
            const PluginManifest& m = rt->manifest;
            return m.display_name.isEmpty() ? protocol_id : m.display_name;
        }
    }
    return protocol_id;
}

QVector<DissectColumn> PluginManager::dissector_columns(
        const QString& protocol_id) const {
    const QString key = protocol_id.toUpper();
    for (const PluginRuntime* rt : m_plugins) {
        if (dissector_key(rt) == key && dissector_eligible(rt)
            && rt->ready && !rt->disabled)
            return rt->dissector_columns;
    }
    return {};
}

QVector<DissectColorRule> PluginManager::dissector_color_rules(
        const QString& protocol_id) const {
    const QString key = protocol_id.toUpper();
    for (const PluginRuntime* rt : m_plugins) {
        if (dissector_key(rt) == key && dissector_eligible(rt)
            && rt->ready && !rt->disabled)
            return rt->dissector_color_rules;
    }
    return {};
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
    if (rt->retired.load()) return false;   // stop_all 已退役(见 parse 同理)
    const quint64 seq = ++rt->seq;

    QByteArray body;
    QDataStream o(&body, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq << payload << ctx;
    send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::DissectRequest), body);

    // 阻塞等待响应邮箱投递(分发 worker 线程调用,不阻塞 UI;与
    // parse_via_plugin 同构,socket 只由主线程的 on_host_ready_read 读)
    bool got = false;
    bool ok = false;
    DissectResult r;
    QString err_text;
    QByteArray resp_body;
    if (wait_response(rt, seq, kParseTimeoutMs, &resp_body)) {
        QDataStream ds(resp_body);
        ds.setVersion(plugin_ipc::kStreamVersion);
        quint64 rseq = 0;
        ds >> rseq >> ok;   // seq 已由投递方核对
        if (ok) ds >> r;
        else ds >> err_text;
        got = true;
    }
    if (!got || !ok) {
        if (!got) {
            // 超时/断连:很可能 host 已挂,禁用插件并注销协议(回主线程执行)
            const bool was_dissector = dissector_eligible(rt);
            rt->disabled = true;
            rt->disable_reason = QStringLiteral("plugin dissect timeout/crash");
            QMetaObject::invokeMethod(this, [this, protocol_id, was_dissector]() {
                if (was_dissector) {
                    unregister_parser(protocol_id.toUpper());
                    emit dissector_unregistered(protocol_id.toUpper());
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
        if (!dissector_eligible(rt) || !rt->ready || rt->disabled || !rt->socket)
            continue;
        QMutexLocker lock(&rt->ipc_mutex);
        send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::ResetDissector),
                     QByteArray());
    }
}

void PluginManager::route_plugin_message(const QString& from,
                                         const QString& to,
                                         const QString& topic,
                                         const QByteArray& payload) {
    emit plugin_message_received(from, to, topic, payload);
    // host→host 直投:目标是另一个 host 插件时在本层转发(无需绕 UI 层)。
    // 排队到主线程执行:本函数可能正被请求等待循环调用(调用方持源
    // runtime 的 ipc_mutex),同步转发会去拿目标 runtime 的 mutex,
    // 目标恰为源自身时即自锁;排队投递把两种情形都解耦。
    if (!to.isEmpty() && to != from) {
        QMetaObject::invokeMethod(
            this, [this, from, to, topic, payload]() {
                send_plugin_message(to, from, topic, payload);
            },
            Qt::QueuedConnection);
    }
}

bool PluginManager::send_plugin_message(const QString& to_name,
                                        const QString& from_name,
                                        const QString& topic,
                                        const QByteArray& payload) {
    for (auto* rt : m_plugins) {
        if (rt->manifest.name != to_name) continue;
        if (!rt->ready || rt->disabled || !rt->socket) return false;
        // tryLock:总线发送多在主线程,阻塞等锁会停转主线程的响应投递
        // (见 checkHeartbeats 注释);目标忙(有请求在途)即报未投递
        if (!rt->ipc_mutex.tryLock()) return false;
        QByteArray body;
        QDataStream o(&body, QIODevice::WriteOnly);
        o.setVersion(plugin_ipc::kStreamVersion);
        o << from_name << to_name << topic << payload;
        send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::PluginMessage),
                     body);
        rt->ipc_mutex.unlock();
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
    abort_response_waiters(rt);   // 在途请求立即失败退出,不等满超时
    rt->disabled = true;
    rt->disable_reason = reason;
    rt->ready = false;
    if (dissector_eligible(rt)) {
        unregister_parser(dissector_key(rt));
        emit dissector_unregistered(dissector_key(rt));
    }
    emit plugin_status_changed(plugin_id, QStringLiteral("disabled: ") + reason);
}

void PluginManager::checkHeartbeats() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        PluginRuntime* rt = it.value();
        if (!rt->ready || rt->disabled || !rt->socket) continue;
        // 发 Ping:必须 tryLock——本函数在主线程执行,而响应投递也靠主
        // 线程的 on_host_ready_read;若在此阻塞等 ipc_mutex(在途请求的
        // 工作线程正持锁等响应),主线程停转 → 响应永远投递不了 → 请求
        // 等满超时误判崩溃并禁用插件(2026-10-13 实测:长回放下首个与
        // 在途请求相撞的心跳 tick 必触发)。有请求在途本身就是存活证据,
        // 本 tick 跳过 Ping 与超时判定。
        if (!rt->ipc_mutex.tryLock()) continue;
        bool heartbeat_expired = false;
        if (rt->awaiting_pong_seq != 0) {
            // 已有一枚 Ping 在途:不再叠发,只看它是否已超时未答
            heartbeat_expired =
                (now - rt->last_ping_ms > kHeartbeatTimeoutMs);
        } else {
            rt->awaiting_pong_seq = ++rt->seq;
            rt->last_ping_ms = now;
            QByteArray p;
            QDataStream o(&p, QIODevice::WriteOnly);
            o.setVersion(plugin_ipc::kStreamVersion);
            o << rt->awaiting_pong_seq;
            send_message(rt, static_cast<quint8>(plugin_ipc::MsgType::Ping), p);
        }
        rt->ipc_mutex.unlock();
        if (heartbeat_expired) {
            disable_plugin(it.key(), QStringLiteral("heartbeat timeout"));
        }
    }
}

// ---- 图形(Phase3) ----

namespace {
constexpr int kGraphicsTimeoutMs = 5000;
/// @brief 同步等待指定 seq 的响应(主线程调用,带事件循环防假死)
/// @details 与 parse/dissect 等待同一邮箱机制:socket 只由
///          on_host_ready_read 读取并按 seq 投递,本函数只泵事件 + 查
///          邮箱,绝不直读 socket(双读竞态,见 PluginRuntime 注释)。
///          等待期间到达的 Pong/RequestRedraw/总线消息由读者统一分发。
template <typename Handler>
bool wait_graphics_response(PluginRuntime* rt, quint64 seq,
                            plugin_ipc::MsgType /*want*/, Handler&& on_frame) {
    const qint64 deadline =
        QDateTime::currentMSecsSinceEpoch() + kGraphicsTimeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        {
            QMutexLocker rl(&rt->resp_mutex);
            if (rt->resp_aborted) return false;
            if (rt->resp_ready && rt->resp_seq == seq) {
                const QByteArray body = rt->resp_body;
                rt->resp_ready = false;
                QDataStream ds(body);
                ds.setVersion(plugin_ipc::kStreamVersion);
                quint64 rseq = 0;
                ds >> rseq;   // handler 的流位置与旧实现一致(seq 之后)
                on_frame(ds);
                return true;
            }
        }
        if (rt->socket->state() != QLocalSocket::ConnectedState) return false;
        // 主线程:短事件循环泵一下,让 readyRead 有机会投递响应
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
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

    // tryLock:主线程阻塞等锁会停转响应投递(见 checkHeartbeats 注释);
    // 忙时跳过本帧渲染(视图定时器会再来)
    if (!rt->ipc_mutex.tryLock()) return QImage();
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
    rt->ipc_mutex.unlock();
    return ok ? img : QImage();
}

bool PluginManager::send_graphics_event(const QString& protocol_id,
                                        const GraphicsEvent& e) {
    auto it = m_plugins.find(protocol_id);
    if (it == m_plugins.end()) return false;
    PluginRuntime* rt = *it;
    if (!rt->ready || rt->disabled || !rt->socket) return false;
    if (!rt->manifest.graphics) return false;

    // tryLock:主线程阻塞等锁会停转响应投递(见 checkHeartbeats 注释)
    if (!rt->ipc_mutex.tryLock()) return false;
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
    rt->ipc_mutex.unlock();
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
