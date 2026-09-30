/// @file lua_backend.h
/// @brief LuaBackend:内嵌 Lua 5.4 运行 Lua 插件
/// @details Lua 插件 API v1(与 JS API 对齐,表用 {}):
///   function get_info() -> { protocolId = "X", displayName = "..." }
///   function parse(frame) -> {
///     accept = true, rejectReason = "...", summary = "...",
///     mpdu = { frameType = 1, srcTei = 2, dstTei = 3 },
///     fields = { { name="..", value="..", relStart=0, relLen=1,
///                   children = { ... } }, ... }
///   }
///   frame = { data = {byte,...}, rawWire = {byte,...}, arrivalUs = number }
/// @note 安全边界是进程隔离;Lua 标准库全开(含 io/os),插件崩溃不影响主程序。
#ifndef BPLC_LUA_BACKEND_H
#define BPLC_LUA_BACKEND_H

#include "plugin_backend.h"

struct lua_State;

/// @brief Lua 插件后端
class LuaBackend : public IPluginBackend {
public:
    LuaBackend();
    ~LuaBackend() override;

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
    /// @brief Lua fields 表(数组) → MsduFieldNode 树(递归)。表在栈顶
    bool convert_fields(QVector<MsduFieldNode>* out, QString* err);
    /// @brief 取表字段的字符串/整数(字段不存在给缺省)
    QString table_str(const char* key, const QString& d = QString());
    quint64 table_uint(const char* key, quint64 d = 0);

    lua_State* m_lua = nullptr;
    bool m_has_graphics = false;
    QString m_protocol_id;
    QString m_display_name;
};

#endif // BPLC_LUA_BACKEND_H
