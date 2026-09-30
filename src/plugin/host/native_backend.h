/// @file native_backend.h
/// @brief NativeBackend:QPluginLoader 加载 C++ 插件
#ifndef BPLC_NATIVE_BACKEND_H
#define BPLC_NATIVE_BACKEND_H

#include <QPluginLoader>

#include "plugin_backend.h"
#include "igraphicsplugin.h"
#include "iprotocolparserplugin.h"

/// @brief native (C++) 插件后端
class NativeBackend : public IPluginBackend {
public:
    NativeBackend() = default;
    ~NativeBackend() override;

    bool initialize(const PluginManifest& m, QString* err) override;
    void shutdown() override;
    QString protocol_id() const override;
    ParseResult parse(const BplcFrame& frame, MsduState& msdu,
                      const ParseFilter& filter, QString* err) override;

    // ---- 图形(Phase3) ----
    bool has_graphics() const override;
    QSize graphics_preferred_size() const override;
    QImage render_graphics(int w, int h, QString* err) override;
    bool handle_graphics_event(const GraphicsEvent& e, QString* err) override;

private:
    QPluginLoader m_loader;
    IProtocolParserPlugin* m_plugin = nullptr;  ///< 不拥有(可空,纯图形插件)
    IGraphicsPlugin* m_graphics = nullptr;      ///< 不拥有(可空)
    IProtocolParser* m_parser = nullptr;        ///< 拥有(可空)
    QString m_protocol_id;
};

#endif // BPLC_NATIVE_BACKEND_H
