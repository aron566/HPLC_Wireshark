/// @file local_plugin_engine.cpp
/// @brief 主界面内置插件引擎实现(工作线程喂帧 + GUI 线程展示)
#include "local_plugin_engine.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QThread>
#include <QTimer>

#include "i18n.h"
#include "plugin_backend.h"

namespace {

// 面板类型归一化:manifest 可选声明;缺省 graphics→topo,其余→stats
QString resolve_panel(const PluginManifest& m) {
    if (!m.panel.isEmpty()) return m.panel.trimmed().toLower();
    return m.graphics ? QStringLiteral("topo") : QStringLiteral("stats");
}

QString localized(const QString& zh, const QString& en) {
    if (trl::enabled() && !en.isEmpty()) return en;
    return zh.isEmpty() ? en : zh;
}

}  // namespace

struct PluginWorker::Runtime {
    PluginManifest manifest;
    IPluginBackend* backend = nullptr;
    MsduState msdu;
    qint64 accept = 0;
    qint64 reject = 0;
    QString panel;
    QString panel_function;
    QString display_name;
    QList<PluginDiagAlarm> pending_alarms;
};

PluginWorker::PluginWorker(QObject* parent) : QObject(parent) {
    qRegisterMetaType<PluginLoadedInfo>("PluginLoadedInfo");
    qRegisterMetaType<PluginDiagAlarm>("PluginDiagAlarm");
    qRegisterMetaType<QList<PluginDiagAlarm>>("QList<PluginDiagAlarm>");
    qRegisterMetaType<BplcFrame>("BplcFrame");
    qRegisterMetaType<TopoEvent>("TopoEvent");
}

PluginWorker::~PluginWorker() {
    for (auto* rt : m_plugins) {
        if (rt->backend) { rt->backend->shutdown(); delete rt->backend; }
        delete rt;
    }
    m_plugins.clear();
}

int PluginWorker::pending_frames() const {
    return m_pending.loadRelaxed();
}

void PluginWorker::start_load(const QString& dir) {
    m_dir = dir;
    const QDir d(dir);
    const QStringList sub = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                        QDir::Name);
    for (const QString& name : sub) {
        const QString pdir = d.absoluteFilePath(name);
        // meta.json enabled=false 的插件跳过(静默,不报错)
        {
            QFile mf(QDir(pdir).filePath(QStringLiteral("meta.json")));
            bool enabled = true;
            if (mf.open(QIODevice::ReadOnly)) {
                const QJsonObject mo =
                    QJsonDocument::fromJson(mf.readAll()).object();
                enabled = mo.value(QStringLiteral("enabled")).toBool(true);
            }
            if (!enabled) continue;
        }
        PluginLoadedInfo info;
        info.plugin_id = name;
        const PluginManifest m = read_plugin_manifest(pdir);
        if (!m.error.isEmpty() || !m.valid) {
            info.ok = false;
            info.error = m.error.isEmpty() ? QStringLiteral("invalid manifest")
                                           : m.error;
            emit plugin_loaded(info);
            continue;
        }
        info.plugin_id = m.name;
        info.protocol_id = m.protocol_id;
        info.display_name = localized(m.display_name, m.display_name_en);
        info.description = localized(m.description, m.description_en);
        info.panel = resolve_panel(m);
        info.panel_function = m.panel_function;
        info.has_graphics = m.graphics;

        QString err;
        IPluginBackend* b = create_backend(m.runtime);
        if (!b) {
            info.ok = false;
            info.error = QStringLiteral("unsupported runtime: %1").arg(m.runtime);
            emit plugin_loaded(info);
            continue;
        }
        if (!b->initialize(m, &err)) {
            info.ok = false;
            info.error = err;
            delete b;
            emit plugin_loaded(info);
            continue;
        }
        if (m.graphics && !b->has_graphics()) {
            // 声明了图形但后端无图形能力:降级为 stats 面板,保留解析
            info.panel = QStringLiteral("stats");
            info.has_graphics = false;
        } else {
            info.has_graphics = b->has_graphics();
        }
        auto* rt = new Runtime();
        rt->manifest = m;
        rt->backend = b;
        rt->panel = info.panel;
        rt->panel_function = info.panel_function;
        rt->display_name = info.display_name;
        // 图形插件主动请求重绘 → 通知面板刷新
        b->set_redraw_callback([this, pid = m.name]() {
            emit render_ready(pid, QImage());  // 空图=仅提示刷新
        });
        // 插件界面控制:host.jumpToFrame(idx) → 主界面跳帧
        b->set_host_jump_callback([this](qint64 frame_index) {
            emit host_jump_to_frame(frame_index);
        });
        m_plugins.insert(m.name, rt);
        info.ok = true;
        emit plugin_loaded(info);
    }
    // 周期性 flush:告警批量 + 统计推送(500ms)
    if (!m_flush_timer) {
        m_flush_timer = new QTimer(this);
        connect(m_flush_timer, &QTimer::timeout, this, &PluginWorker::flush_periodic);
        m_flush_timer->start(500);
    }
    emit load_finished();
}

void PluginWorker::on_frame(const BplcFrame& f) {
    m_pending.ref();
    ++m_frame_seq;
    // 插件 API 事实契约:frame.data 为原始线帧(0x3C...0x3E 含哨兵),
    // 与独立回放工具一致;主程序 frame_ready 给的是去哨兵净荷,此处映射。
    BplcFrame pf = f;
    if (!pf.raw_wire.isEmpty()) pf.data = pf.raw_wire;
    for (auto* rt : m_plugins) {
        QString perr;
        ParseFilter filter;
        const ParseResult r = rt->backend->parse(pf, rt->msdu, filter, &perr);
        if (perr.isEmpty() && r.accept) {
            ++rt->accept;
            // 诊断面板:收集 Diagnosis 子节点
            if (rt->panel == QLatin1String("diag")) {
                for (const auto& top : r.msdu.tree) {
                    if (top.name != QLatin1String("Diagnosis")) continue;
                    for (const auto& c : top.children) {
                        if (rt->pending_alarms.size() >= 500) break;
                        PluginDiagAlarm a;
                        a.frame_no = m_frame_seq;
                        a.name = c.name;
                        a.value = c.value;
                        rt->pending_alarms.append(a);
                    }
                }
                if (rt->pending_alarms.size() >= 200) {
                    emit diag_alarms(rt->manifest.name, rt->pending_alarms);
                    rt->pending_alarms.clear();
                }
            }
        } else {
            ++rt->reject;
        }
    }
    m_pending.deref();
}

void PluginWorker::flush_periodic() {
    for (auto* rt : m_plugins) {
        if (!rt->pending_alarms.isEmpty()) {
            emit diag_alarms(rt->manifest.name, rt->pending_alarms);
            rt->pending_alarms.clear();
        }
        emit plugin_stats(rt->manifest.name, rt->accept, rt->reject);
    }
}

void PluginWorker::request_render(const QString& pid, int w, int h) {
    auto* rt = m_plugins.value(pid, nullptr);
    if (!rt || !rt->backend || !rt->backend->has_graphics()) return;
    QString err;
    const QImage img = rt->backend->render_graphics(
        qBound(64, w, 1600), qBound(48, h, 1200), &err);
    emit render_ready(pid, img);
}

/// @brief 图形事件透传:后端返回 true=需要重绘,发空图触发面板刷新
void PluginWorker::on_graphics_event(const QString& pid, const GraphicsEvent& e) {
    auto* rt = m_plugins.value(pid, nullptr);
    if (!rt || !rt->backend || !rt->backend->has_graphics()) return;
    QString err;
    if (rt->backend->handle_graphics_event(e, &err))
        emit render_ready(pid, QImage());  // 空图=刷新提示,面板按需重绘
}

void PluginWorker::on_frame_selected(qint64 frame_index, bool force_history) {
    for (auto* rt : m_plugins) {
        if (!rt->backend) continue;
        rt->backend->notify_frame_selected(frame_index, force_history);
    }
}

void PluginWorker::request_text(const QString& pid, const QString& func) {
    auto* rt = m_plugins.value(pid, nullptr);
    if (!rt || !rt->backend) return;
    QString err;
    const QString text = rt->backend->call_text_function(
        func.toUtf8().constData(), &err);
    emit text_ready(pid, func, err.isEmpty() ? text
                                             : QStringLiteral("ERROR: %1").arg(err));
}

void PluginWorker::reset_all() {
    for (auto* rt : m_plugins) {
        if (!rt->backend) continue;
        QString err;
        rt->backend->shutdown();
        if (!rt->backend->initialize(rt->manifest, &err)) {
            // reset 失败:保留旧状态,仅清零计数
        }
        rt->msdu = MsduState();
        rt->accept = 0;
        rt->reject = 0;
        rt->pending_alarms.clear();
        emit plugin_stats(rt->manifest.name, 0, 0);
    }
    m_frame_seq = 0;
}

/// @brief 卸载全部插件:删除后端并清空列表(重载目录前调用)
void PluginWorker::unload_all() {
    for (auto* rt : m_plugins) {
        if (rt->backend) {
            QString err;
            rt->backend->shutdown();
            delete rt->backend;
        }
        delete rt;
    }
    m_plugins.clear();
    m_frame_seq = 0;
    m_pending.storeRelaxed(0);
}

// =====================================================================
// LocalPluginEngine
// =====================================================================
LocalPluginEngine::LocalPluginEngine(QObject* parent) : QObject(parent) {
    qRegisterMetaType<PluginLoadedInfo>("PluginLoadedInfo");
    qRegisterMetaType<PluginDiagAlarm>("PluginDiagAlarm");
    qRegisterMetaType<QList<PluginDiagAlarm>>("QList<PluginDiagAlarm>");
    qRegisterMetaType<BplcFrame>("BplcFrame");
    qRegisterMetaType<TopoEvent>("TopoEvent");

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("PluginWorker"));
    m_worker = new PluginWorker();
    m_worker->moveToThread(m_thread);

    // worker → engine(本对象,GUI 线程):全部 QueuedConnection
    connect(m_worker, &PluginWorker::plugin_loaded,
            this, &LocalPluginEngine::plugin_loaded, Qt::QueuedConnection);
    connect(m_worker, &PluginWorker::load_finished,
            this, &LocalPluginEngine::load_finished, Qt::QueuedConnection);
    connect(m_worker, &PluginWorker::plugin_stats,
            this, &LocalPluginEngine::plugin_stats, Qt::QueuedConnection);
    connect(m_worker, &PluginWorker::diag_alarms,
            this, &LocalPluginEngine::diag_alarms, Qt::QueuedConnection);
    connect(m_worker, &PluginWorker::render_ready,
            this, &LocalPluginEngine::render_ready, Qt::QueuedConnection);
    connect(m_worker, &PluginWorker::text_ready,
            this, &LocalPluginEngine::text_ready, Qt::QueuedConnection);
    // 插件请求主界面跳帧:工作线程 → GUI 线程
    connect(m_worker, &PluginWorker::host_jump_to_frame,
            this, &LocalPluginEngine::host_jump_to_frame,
            Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

LocalPluginEngine::~LocalPluginEngine() {
    m_thread->quit();
    m_thread->wait(3000);
}

void LocalPluginEngine::load(const QString& dir) {
    QMetaObject::invokeMethod(m_worker, "start_load", Qt::QueuedConnection,
                              Q_ARG(QString, dir));
}

void LocalPluginEngine::feed_frame(const BplcFrame& f) {
    QMetaObject::invokeMethod(m_worker, "on_frame", Qt::QueuedConnection,
                              Q_ARG(BplcFrame, f));
}

void LocalPluginEngine::request_render(const QString& pid, int w, int h) {
    QMetaObject::invokeMethod(m_worker, "request_render", Qt::QueuedConnection,
                              Q_ARG(QString, pid), Q_ARG(int, w), Q_ARG(int, h));
}

void LocalPluginEngine::request_text(const QString& pid, const QString& func) {
    QMetaObject::invokeMethod(m_worker, "request_text", Qt::QueuedConnection,
                              Q_ARG(QString, pid), Q_ARG(QString, func));
}

void LocalPluginEngine::send_graphics_event(const QString& pid,
                                            const GraphicsEvent& e) {
    QMetaObject::invokeMethod(m_worker, "on_graphics_event",
                              Qt::QueuedConnection,
                              Q_ARG(QString, pid), Q_ARG(GraphicsEvent, e));
}

void LocalPluginEngine::notify_frame_selected(qint64 frame_index,
                                              bool force_history) {
    QMetaObject::invokeMethod(m_worker, "on_frame_selected",
                              Qt::QueuedConnection,
                              Q_ARG(qint64, frame_index), Q_ARG(bool, force_history));
}

void LocalPluginEngine::reset_all() {
    QMetaObject::invokeMethod(m_worker, "reset_all", Qt::QueuedConnection);
}

void LocalPluginEngine::unload_all() {
    QMetaObject::invokeMethod(m_worker, "unload_all", Qt::QueuedConnection);
}
