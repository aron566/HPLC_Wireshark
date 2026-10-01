/// @file local_plugin_engine.h
/// @brief 主界面内置插件引擎(进程内 JS/Lua 后端,独立工作线程喂帧)
/// @details 与 PluginManager(多进程 IPC 隔离)不同,本引擎为让主界面直接
///          调用插件并展示插件功能界面而设:
///          - 工作线程内为每个插件创建 IPluginBackend(JS/Lua),逐帧 parse
///          - GUI 线程只做展示:按需 request_render / request_text,
///            结果经 queued 信号回传(QImage/QString 隐式共享,跨线程安全)
///          - QJSEngine 非线程安全:后端所有调用限定在工作线程内
#ifndef BPLC_LOCAL_PLUGIN_ENGINE_H
#define BPLC_LOCAL_PLUGIN_ENGINE_H

#include <QAtomicInt>
#include <QImage>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include "bplcframe.h"
#include "plugin_manifest.h"

class IPluginBackend;
class QThread;
class QTimer;

/// @brief 诊断告警行(插件 parse 结果 Diagnosis 子节点)
struct PluginDiagAlarm {
    qint64  frame_no = 0;
    QString name;
    QString value;
};
Q_DECLARE_METATYPE(PluginDiagAlarm)

/// @brief 插件加载结果(跨线程信号载荷)
struct PluginLoadedInfo {
    QString plugin_id;      ///< manifest.name
    QString display_name;   ///< 本地化显示名
    QString description;    ///< 本地化描述
    QString panel;          ///< 面板类型: topo|replay|diag|report|stats
    QString panel_function; ///< 文本类面板调用的脚本函数名(可空)
    QString protocol_id;
    bool    has_graphics = false;
    bool    ok = false;
    QString error;
};
Q_DECLARE_METATYPE(PluginLoadedInfo)

/// @brief 插件工作线程对象(拥有全部 IPluginBackend)
class PluginWorker : public QObject {
    Q_OBJECT
public:
    explicit PluginWorker(QObject* parent = nullptr);
    ~PluginWorker() override;

    /// @brief 工作线程积压帧数(GUI 线程可调用,原子)
    int pending_frames() const;

public slots:
    void start_load(const QString& dir);
    void on_frame(const BplcFrame& f);
    void request_render(const QString& pid, int w, int h);
    void request_text(const QString& pid, const QString& func);
    void reset_all();

signals:
    void plugin_loaded(const PluginLoadedInfo& info);
    void load_finished();
    void plugin_stats(const QString& pid, qint64 accept, qint64 reject);
    void diag_alarms(const QString& pid, const QList<PluginDiagAlarm>& alarms);
    void render_ready(const QString& pid, const QImage& img);
    void text_ready(const QString& pid, const QString& func, const QString& text);

private slots:
    void flush_periodic();

private:
    struct Runtime;
    QMap<QString, Runtime*> m_plugins;  ///< key: manifest.name
    QTimer* m_flush_timer = nullptr;
    QAtomicInt m_pending{0};
    qint64 m_frame_seq = 0;
    QString m_dir;
};

/// @brief 主界面侧插件引擎(GUI 线程门面,转发到工作线程)
class LocalPluginEngine : public QObject {
    Q_OBJECT
public:
    explicit LocalPluginEngine(QObject* parent = nullptr);
    ~LocalPluginEngine() override;

    /// @brief 扫描并加载 plugins_dir 下全部插件(异步,经 plugin_loaded 逐个回传)
    void load(const QString& dir);
    /// @brief 工作线程是否空闲(无积压帧)
    bool is_idle() const { return m_worker && m_worker->pending_frames() == 0; }

public slots:
    /// @brief 喂一帧(供 SerialReader::frame_ready 直连,QueuedConnection)
    void feed_frame(const BplcFrame& f);
    void request_render(const QString& pid, int w, int h);
    void request_text(const QString& pid, const QString& func);
    void reset_all();

signals:
    void plugin_loaded(const PluginLoadedInfo& info);
    void load_finished();
    void plugin_stats(const QString& pid, qint64 accept, qint64 reject);
    void diag_alarms(const QString& pid, const QList<PluginDiagAlarm>& alarms);
    void render_ready(const QString& pid, const QImage& img);
    void text_ready(const QString& pid, const QString& func, const QString& text);

private:
    QThread*      m_thread = nullptr;
    PluginWorker* m_worker = nullptr;
};

#endif // BPLC_LOCAL_PLUGIN_ENGINE_H
