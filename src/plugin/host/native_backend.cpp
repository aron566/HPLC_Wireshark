/// @file native_backend.cpp
#include "native_backend.h"
#include <QDir>

NativeBackend::~NativeBackend() {
    shutdown();
}

bool NativeBackend::initialize(const PluginManifest& m, QString* err) {
    if (m.api_version != 1) {
        *err = QStringLiteral("api_version mismatch");
        return false;
    }
    const QString lib_path = QDir(m.dir_path).filePath(m.entry);
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
    m_parser = m_plugin->create_parser();
    if (!m_parser) {
        *err = QStringLiteral("create_parser() returned null");
        return false;
    }
    m_protocol_id = m_plugin->protocol_id();
    return true;
}

void NativeBackend::shutdown() {
    delete m_parser;
    m_parser = nullptr;
    if (m_plugin) { m_plugin->shutdown(); m_plugin = nullptr; }
    m_loader.unload();
}

QString NativeBackend::protocol_id() const {
    return m_protocol_id;
}

ParseResult NativeBackend::parse(const BplcFrame& frame, MsduState& msdu,
                                 const ParseFilter& filter, QString* err) {
    Q_UNUSED(err);
    return m_parser->parse(frame, msdu, filter);
}
