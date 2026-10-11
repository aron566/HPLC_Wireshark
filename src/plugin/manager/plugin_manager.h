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

#include <atomic>

#include "bplcframe.h"
#include "dissect.h"
#include "igraphicsplugin.h"
#include "iprotocolparser.h"
#include "plugin_manifest.h"

class QLocalServer;
class QLocalSocket;
class QProcess;
class PluginGraphicsView;

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
    bool          has_dissector = false; ///< host 握手上报的实际解剖能力
    bool          disabled = false;   ///< 崩溃/错误后禁用
    std::atomic<bool> retired{false}; ///< stop_all 已退役:对象本体延迟到
                                      ///< 管理器析构才释放(见 stop_all)
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

    // ---- 解剖器（新架构） ----
    /// @brief 已就绪的解剖器协议 id 列表(仅 manifest.dissector 且握手成功)
    QStringList dissector_protocol_ids() const;
    /// @brief 协议 id 是否为已就绪的解剖器插件
    bool is_dissector_protocol(const QString& protocol_id) const;
    /// @brief 解剖器协议的显示名(清单 display_name,供协议下拉框)
    QString dissector_display_name(const QString& protocol_id) const;
    /// @brief 经解剖器插件解剖一帧净荷(阻塞,worker 线程调用)
    bool dissect_via_plugin(const QString& protocol_id,
                            const QByteArray& payload, const FrameContext& ctx,
                            DissectResult* out);
    /// @brief 通知全部解剖器清跨帧状态(新采集/回放)
    void reset_dissectors();

    // ---- 插件间通讯（主进程总线） ----
    /// @brief 向指定 host 插件投递一条消息(按插件名寻址)。false=目标不存在
    bool send_plugin_message(const QString& to_name, const QString& from_name,
                             const QString& topic, const QByteArray& payload);

    // ---- 生命周期 ----
    /// @brief 停止全部插件 host 进程(优雅 Shutdown → terminate → kill 兜底)
    /// @details 市场安装/卸载删插件目录前、主程序退出时调用;调用后可再
    ///          loadAll 重新拉起。析构函数亦调用之。
    void stop_all();

    // ---- 图形(Phase3) ----
    /// @brief 插件是否有图形能力
    bool has_graphics(const QString& protocol_id) const;
    /// @brief 请求渲染(阻塞,主线程调用)。返回空图=失败
    QImage request_render(const QString& protocol_id, int w, int h);
    /// @brief 发送图形事件(阻塞,主线程调用)。返回 needs_redraw
    bool send_graphics_event(const QString& protocol_id,
                             const GraphicsEvent& e);
    /// @brief 注册图形视图(收 RequestRedraw 时通知重绘)
    void register_graphics_view(const QString& protocol_id,
                                PluginGraphicsView* view);
    void unregister_graphics_view(const QString& protocol_id,
                                  PluginGraphicsView* view);

signals:
    /// @brief 插件状态变化(崩溃/禁用/就绪),UI 可据此提示
    void plugin_status_changed(const QString& plugin_id, const QString& status);
    /// @brief 解剖器协议注册/注销(供主界面在配置协议待注册时自动切换分发器)
    void dissector_registered(const QString& protocol_id);
    void dissector_unregistered(const QString& protocol_id);
    /// @brief 收到某插件发往其他插件的消息(主进程总线中转,见 DISSECTOR_ARCH)
    void plugin_message_received(const QString& from, const QString& to,
                                 const QString& topic,
                                 const QByteArray& payload);

private:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    bool start_plugin(const PluginManifest& m);
    void on_new_connection(const QString& plugin_id);
    void on_host_ready_read(const QString& plugin_id);
    void on_process_finished(const QString& plugin_id, int code);
    void on_process_error(const QString& plugin_id);
    void disable_plugin(const QString& plugin_id, const QString& reason);
    /// @brief 总线路由(信号广播 + host→host 直投,直投排队到主线程防锁耦合);
    ///        handle_message 与请求等待循环共用,保证路由语义只有一份
    void route_plugin_message(const QString& from, const QString& to,
                              const QString& topic, const QByteArray& payload);
    void handle_message(const QString& plugin_id, quint8 type, QDataStream& ds);
    void send_message(PluginRuntime* rt, quint8 type, const QByteArray& payload);
    void checkHeartbeats();

    QMap<QString, PluginRuntime*> m_plugins;  ///< key: protocol_id
    QList<PluginRuntime*> m_retired;  ///< stop_all 退役的运行时本体:重对象
                                      ///< (进程/socket)已在 stop_all 内释放,
                                      ///< 本体留置此处待管理器析构统一 delete,
                                      ///< 保证迟到的请求线程手中的指针永不悬空
    QMap<QString, QList<PluginGraphicsView*>> m_graphics_views;  ///< key: protocol_id
    QTimer m_hb_timer;
};

#endif // BPLC_PLUGIN_MANAGER_H
