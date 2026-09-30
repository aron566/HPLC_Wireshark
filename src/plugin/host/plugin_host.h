/// @file plugin_host.h
/// @brief bplc-plugin-host:插件宿主进程(加载插件 .so,经 IPC 服务主程序)
#ifndef BPLC_PLUGIN_HOST_H
#define BPLC_PLUGIN_HOST_H

#include <QObject>
#include <QPluginLoader>
#include <QLocalSocket>
#include <QByteArray>

#include "iplugin.h"
#include "iprotocolparserplugin.h"
#include "plugin_manifest.h"
#include "plugin_ipc.h"

/// @brief 插件宿主:加载一个 native 插件,处理主程序的 IPC 请求
class PluginHost : public QObject {
    Q_OBJECT
public:
    explicit PluginHost(const QString& socket_name, const QString& plugin_dir,
                        QObject* parent = nullptr);
    ~PluginHost() override;

    /// @brief 启动:加载插件 + 连接主程序。false=启动失败(调用方退出非0)
    bool start(QString* err);
    /// @brief 运行事件循环直到退出
    int exec();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError e);

private:
    void sendMessage(plugin_ipc::MsgType t, const QByteArray& payload);
    void handleMessage(plugin_ipc::MsgType t, QDataStream& ds);
    void handleParseRequest(QDataStream& ds);

    QString            m_socket_name;
    PluginManifest     m_manifest;
    QPluginLoader      m_loader;
    IProtocolParserPlugin* m_plugin = nullptr;   ///< 不拥有(loader 拥有)
    IProtocolParser*   m_parser = nullptr;       ///< 拥有
    QLocalSocket       m_socket;
    QByteArray         m_rx_buf;
    bool               m_hello_done = false;
};

#endif // BPLC_PLUGIN_HOST_H
