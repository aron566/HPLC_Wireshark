/// @file plugin_manager.h
/// @brief PluginManager:主程序侧插件管理(扫描/启动/监控/IPC)
/// @details 职责:
///   - 扫描 plugins/ 目录,读取 plugin.json
///   - 为每个插件创建 QLocalServer,启动 bplc-plugin-host 子进程
///   - 握手校验(api_version),注册协议到工厂
///   - 同步 IPC 解析(worker 线程调用,阻塞等待)
///   - 心跳 + 崩溃检测(进程退出/心跳超时 → 禁用插件并通知)
#ifndef BPLC_PLUGIN_MANAGER_H
#define BPLC_PLUGIN_MANAGER_H

#include <QObject>
#include <QMap>
#include <QMutex>
#include <QTimer>

#include "bplcframe.h"
#include "iprotocolparser.h"
#include "plugin_manifest.h"

class QLocalServer;
class QLocalSocket;
class QProcess;

/// @brief 单个插件的运行时状态
struct PluginRuntime {
    PluginManifest manifest;
    QProcess*     process = nullptr;
    QLocalServer* server = nullptr;
    QLocalSocket* socket = nullptr;   ///< 握手成功后的连接
    QByteArray    rx_buf;
    QMutex        ipc_mutex;          ///< 串行化该插件的 IPC 请求
    quint64       seq = 0;
    bool          ready = false;      ///< 握手完成
    bool          disabled = false;   ///< 崩溃/错误后禁用
    QString       disable_reason;
    qint64        last_pong_ms = 0;
};

/// @brief 插件管理器(单例,主线程)
class PluginManager : public QObject {
    Q_OBJECT
public:
    static PluginManager& instance();

    /// @brief 扫描并启动 plugins_dir 下所有插件
    void loadAll(const QString& plugins_dir);
    /// @brief 已注册的插件协议 id 列表(供 UI/配置)
    QStringList pluginProtocolIds() const;
    /// @brief 协议 id 是否来自插件
    bool is_plugin_protocol(const QString& protocol_id) const;

    /// @brief 经插件解析一帧(阻塞,worker 线程调用)
    /// @return ok=false 时 result.accept=false 且 reject_reason 置错误信息
    bool parse_via_plugin(const QString& protocol_id,
                        const BplcFrame& frame, MsduState& msdu,
                        const ParseFilter& filter, ParseResult* result);

signals:
    /// @brief 插件状态变化(崩溃/禁用/就绪),UI 可据此提示
    void plugin_status_changed(const QString& plugin_id, const QString& status);

private:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    bool start_plugin(const PluginManifest& m);
    void on_new_connection(const QString& plugin_id);
    void on_host_ready_read(const QString& plugin_id);
    void on_process_finished(const QString& plugin_id, int code);
    void on_process_error(const QString& plugin_id);
    void disable_plugin(const QString& plugin_id, const QString& reason);
    void handle_message(const QString& plugin_id, quint8 type, QDataStream& ds);
    void send_message(PluginRuntime* rt, quint8 type, const QByteArray& payload);
    void checkHeartbeats();

    QMap<QString, PluginRuntime*> m_plugins;  ///< key: protocol_id
    QTimer m_hb_timer;
};

#endif // BPLC_PLUGIN_MANAGER_H
