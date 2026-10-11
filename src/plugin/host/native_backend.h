/// @file native_backend.h
/// @brief NativeBackend:QPluginLoader 加载 C++ 插件
#ifndef BPLC_NATIVE_BACKEND_H
#define BPLC_NATIVE_BACKEND_H

#include <QPluginLoader>

#include "plugin_backend.h"
#include "idissectorplugin.h"
#include "igraphicsplugin.h"
#include "ipluginbus.h"
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

    // ---- 解剖器 ----
    bool has_dissector() const override;
    bool dissect(const QByteArray& payload, const FrameContext& ctx,
                 DissectResult* out, QString* err) override;
    void dissect_reset() override;

    // ---- 插件间通讯 ----
    void set_bus_send_callback(BusSendCallback cb) override;
    void on_plugin_message(const QString& from, const QString& topic,
                           const QByteArray& payload) override;

    // ---- 图形(Phase3) ----
    bool has_graphics() const override;
    QSize graphics_preferred_size() const override;
    QImage render_graphics(int w, int h, QString* err) override;
    bool handle_graphics_event(const GraphicsEvent& e, QString* err) override;
    void set_ui_dark(bool dark) override;  ///< 主题跟随 → 转发给 native 图形插件

private:
    QPluginLoader m_loader;
    QObject* m_instance = nullptr;              ///< 插件根实例(拥有:shutdown 时先 delete 再 unload)
    IProtocolParserPlugin* m_plugin = nullptr;  ///< 不拥有(可空,纯图形插件)
    IGraphicsPlugin* m_graphics = nullptr;      ///< 不拥有(可空)
    IDissectorPlugin* m_dissector = nullptr;    ///< 不拥有(可空,解剖器插件)
    IPluginBusAware* m_bus_aware = nullptr;     ///< 不拥有(可空,插件间通讯)
    IProtocolParser* m_parser = nullptr;        ///< 拥有(可空)
    QString m_protocol_id;
};

#endif // BPLC_NATIVE_BACKEND_H
