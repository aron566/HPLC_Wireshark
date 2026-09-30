/// @file plugin_host.cpp
/// @brief bplc-plugin-host 实现
#include "plugin_host.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QTimer>

#include "plugin_serialization.h"

PluginHost::PluginHost(const QString& socket_name, const QString& plugin_dir,
                       QObject* parent)
    : QObject(parent), m_socket_name(socket_name) {
    m_manifest = read_plugin_manifest(plugin_dir);
}

PluginHost::~PluginHost() {
    delete m_parser;
    m_parser = nullptr;
    if (m_plugin) { m_plugin->shutdown(); m_plugin = nullptr; }
    m_loader.unload();
}

bool PluginHost::start(QString* err) {
    if (!m_manifest.valid) {
        *err = QStringLiteral("bad manifest: %1").arg(m_manifest.error);
        return false;
    }
    if (m_manifest.runtime != QStringLiteral("native")) {
        *err = QStringLiteral("unsupported runtime '%1' (Phase1 only native)")
                   .arg(m_manifest.runtime);
        return false;
    }
    if (m_manifest.api_version != plugin_ipc::kApiVersion) {
        *err = QStringLiteral("api_version mismatch: plugin=%1 host=%2")
                   .arg(m_manifest.api_version).arg(plugin_ipc::kApiVersion);
        return false;
    }

    const QString lib_path = QDir(m_manifest.dir_path).filePath(m_manifest.entry);
    m_loader.setFileName(lib_path);
    QObject* inst = m_loader.instance();
    if (!inst) {
        *err = QStringLiteral("load failed: %1").arg(m_loader.errorString());
        return false;
    }
    m_plugin = qobject_cast<IProtocolParserPlugin*>(inst);
    if (!m_plugin) {
        *err = QStringLiteral("not a IProtocolParserPlugin");
        return false;
    }
    if (!m_plugin->initialize()) {
        *err = QStringLiteral("plugin initialize() failed");
        return false;
    }
    m_parser = m_plugin->createParser();
    if (!m_parser) {
        *err = QStringLiteral("createParser() returned null");
        return false;
    }

    connect(&m_socket, &QLocalSocket::connected, this, &PluginHost::onConnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &PluginHost::onReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected,
            qApp, &QCoreApplication::quit);
    connect(&m_socket, &QLocalSocket::errorOccurred,
            this, &PluginHost::onSocketError);
    m_socket.connectToServer(m_socket_name);
    if (!m_socket.waitForConnected(10000)) {
        *err = QStringLiteral("connect to main app failed: %1")
                   .arg(m_socket.errorString());
        return false;
    }
    return true;
}

int PluginHost::exec() {
    return QCoreApplication::exec();
}

void PluginHost::onConnected() {
    // 握手
    QByteArray p;
    QDataStream ds(&p, QIODevice::WriteOnly);
    ds.setVersion(plugin_ipc::kStreamVersion);
    ds << plugin_ipc::kApiVersion << m_manifest.name << m_manifest.protocol_id;
    sendMessage(plugin_ipc::MsgType::Hello, p);
}

void PluginHost::onReadyRead() {
    m_rx_buf.append(m_socket.readAll());
    // 帧:[quint32 len][quint8 type][payload]
    while (m_rx_buf.size() >= 5) {
        QDataStream hdr(m_rx_buf);
        hdr.setVersion(plugin_ipc::kStreamVersion);
        quint32 len = 0;
        hdr >> len;
        if (m_rx_buf.size() < 4 + (int)len) break;  // 等更多数据
        const QByteArray frame = m_rx_buf.mid(4, len);
        m_rx_buf.remove(0, 4 + len);

        QDataStream ds(frame);
        ds.setVersion(plugin_ipc::kStreamVersion);
        quint8 t = 0;
        ds >> t;
        handleMessage(static_cast<plugin_ipc::MsgType>(t), ds);
    }
}

void PluginHost::onDisconnected() {
    QCoreApplication::quit();  // 主程序断开 → 退出
}

void PluginHost::onSocketError(QLocalSocket::LocalSocketError e) {
    if (e != QLocalSocket::PeerClosedError)
        QCoreApplication::exit(2);
}

void PluginHost::sendMessage(plugin_ipc::MsgType t, const QByteArray& payload) {
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setVersion(plugin_ipc::kStreamVersion);
    ds << quint32(1 + payload.size()) << static_cast<quint8>(t);
    frame.append(payload);
    m_socket.write(frame);
    m_socket.flush();
}

void PluginHost::handleMessage(plugin_ipc::MsgType t, QDataStream& ds) {
    switch (t) {
    case plugin_ipc::MsgType::HelloAck: {
        bool ok = false; QString reason;
        ds >> ok >> reason;
        m_hello_done = ok;
        if (!ok) QCoreApplication::exit(3);
        break;
    }
    case plugin_ipc::MsgType::ParseRequest:
        handleParseRequest(ds);
        break;
    case plugin_ipc::MsgType::Ping: {
        quint64 seq = 0; ds >> seq;
        QByteArray p; QDataStream o(&p, QIODevice::WriteOnly);
        o.setVersion(plugin_ipc::kStreamVersion); o << seq;
        sendMessage(plugin_ipc::MsgType::Pong, p);
        break;
    }
    case plugin_ipc::MsgType::Shutdown:
        QCoreApplication::quit();
        break;
    default:
        break;
    }
}

void PluginHost::handleParseRequest(QDataStream& ds) {
    quint64 seq = 0;
    BplcFrame frame; MsduState msdu; ParseFilter filter;
    ds >> seq >> frame >> msdu >> filter;

    QByteArray resp;
    QDataStream o(&resp, QIODevice::WriteOnly);
    o.setVersion(plugin_ipc::kStreamVersion);
    o << seq;
    if (!m_hello_done || !m_parser) {
        o << false;  // ok=false
        o << QStringLiteral("not ready");
    } else {
        const ParseResult r = m_parser->parse(frame, msdu, filter);
        o << true << r << msdu;
    }
    sendMessage(plugin_ipc::MsgType::ParseResponse, resp);
}
