/// @file lua_backend.cpp
#include "lua_backend.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>

#include "script_painter.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace {
// QByteArray → Lua 数组表(1-based),留在栈顶
void push_byte_table(lua_State* L, const QByteArray& b) {
    lua_newtable(L);
    for (int i = 0; i < b.size(); ++i) {
        lua_pushinteger(L, static_cast<unsigned char>(b[i]));
        lua_seti(L, -2, i + 1);
    }
}
// 栈顶错误 → QString 并清栈
QString pop_lua_error(lua_State* L) {
    QString e = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);
    return e;
}

// ---- Lua 绘图绑定: p 表的 C 函数,QPainter* 经 upvalue 传入 ----
namespace {
ScriptPainter* painter_upvalue(lua_State* L) {
    return static_cast<ScriptPainter*>(lua_touserdata(L, lua_upvalueindex(1)));
}
int l_set_pen(lua_State* L) {
    painter_upvalue(L)->set_pen(QString::fromUtf8(luaL_checkstring(L, 1)),
                                luaL_optnumber(L, 2, 1.0));
    return 0;
}
int l_set_brush(lua_State* L) {
    painter_upvalue(L)->set_brush(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}
int l_no_brush(lua_State* L) {
    painter_upvalue(L)->no_brush();
    return 0;
}
int l_no_pen(lua_State* L) {
    painter_upvalue(L)->no_pen();
    return 0;
}
int l_set_font(lua_State* L) {
    painter_upvalue(L)->set_font(QString::fromUtf8(luaL_checkstring(L, 1)),
                                 static_cast<int>(luaL_checkinteger(L, 2)),
                                 lua_toboolean(L, 3));
    return 0;
}
int l_clear(lua_State* L) {
    painter_upvalue(L)->clear(QString::fromUtf8(luaL_checkstring(L, 1)));
    return 0;
}
int l_draw_line(lua_State* L) {
    painter_upvalue(L)->draw_line(luaL_checknumber(L, 1), luaL_checknumber(L, 2),
                                  luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    return 0;
}
int l_draw_rect(lua_State* L) {
    painter_upvalue(L)->draw_rect(luaL_checknumber(L, 1), luaL_checknumber(L, 2),
                                  luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    return 0;
}
int l_fill_rect(lua_State* L) {
    painter_upvalue(L)->fill_rect(luaL_checknumber(L, 1), luaL_checknumber(L, 2),
                                  luaL_checknumber(L, 3), luaL_checknumber(L, 4),
                                  QString::fromUtf8(luaL_checkstring(L, 5)));
    return 0;
}
int l_draw_ellipse(lua_State* L) {
    painter_upvalue(L)->draw_ellipse(luaL_checknumber(L, 1), luaL_checknumber(L, 2),
                                     luaL_checknumber(L, 3), luaL_checknumber(L, 4));
    return 0;
}
int l_draw_text(lua_State* L) {
    painter_upvalue(L)->draw_text(luaL_checknumber(L, 1), luaL_checknumber(L, 2),
                                  QString::fromUtf8(luaL_checkstring(L, 3)));
    return 0;
}
int l_draw_point(lua_State* L) {
    painter_upvalue(L)->draw_point(luaL_checknumber(L, 1), luaL_checknumber(L, 2));
    return 0;
}
const luaL_Reg kPainterFuncs[] = {
    {"set_pen", l_set_pen}, {"set_brush", l_set_brush},
    {"no_brush", l_no_brush}, {"no_pen", l_no_pen},
    {"set_font", l_set_font}, {"clear", l_clear},
    {"draw_line", l_draw_line}, {"draw_rect", l_draw_rect},
    {"fill_rect", l_fill_rect}, {"draw_ellipse", l_draw_ellipse},
    {"draw_text", l_draw_text}, {"draw_point", l_draw_point},
    {nullptr, nullptr}
};
/// @brief 压入绘图 p 表(函数 upvalue 绑定 bridge)
void push_painter_table(lua_State* L, ScriptPainter* bridge) {
    lua_newtable(L);
    lua_pushlightuserdata(L, bridge);
    luaL_setfuncs(L, kPainterFuncs, 1);
}
} // namespace
} // namespace

LuaBackend::LuaBackend() = default;

LuaBackend::~LuaBackend() {
    shutdown();
}

bool LuaBackend::initialize(const PluginManifest& m, QString* err) {
    if (m.api_version != 1) {
        *err = QStringLiteral("api_version mismatch");
        return false;
    }
    m_lua = luaL_newstate();
    if (!m_lua) {
        *err = QStringLiteral("luaL_newstate failed");
        return false;
    }
    luaL_openlibs(m_lua);

    const QString lua_path = QDir(m.dir_path).filePath(m.entry);
    const QByteArray path_utf8 = lua_path.toUtf8();
    if (luaL_loadfile(m_lua, path_utf8.constData()) != LUA_OK) {
        *err = QStringLiteral("load %1: %2").arg(m.entry, pop_lua_error(m_lua));
        return false;
    }
    if (lua_pcall(m_lua, 0, 0, 0) != LUA_OK) {
        *err = QStringLiteral("run %1: %2").arg(m.entry, pop_lua_error(m_lua));
        return false;
    }

    // get_info()
    lua_getglobal(m_lua, "get_info");
    if (!lua_isfunction(m_lua, -1)) {
        *err = QStringLiteral("missing function get_info()");
        lua_pop(m_lua, 1);
        return false;
    }
    if (lua_pcall(m_lua, 0, 1, 0) != LUA_OK) {
        *err = QStringLiteral("get_info: %1").arg(pop_lua_error(m_lua));
        return false;
    }
    if (!lua_istable(m_lua, -1)) {
        *err = QStringLiteral("get_info() must return a table");
        lua_pop(m_lua, 1);
        return false;
    }
    m_protocol_id = table_str("protocolId");
    m_display_name = table_str("displayName");
    lua_pop(m_lua, 1);
    if (m_protocol_id.isEmpty()) {
        *err = QStringLiteral("get_info() missing protocolId");
        return false;
    }
    if (m_protocol_id != m.protocol_id) {
        *err = QStringLiteral("protocolId mismatch: manifest=%1 script=%2")
                   .arg(m.protocol_id, m_protocol_id);
        return false;
    }

    lua_getglobal(m_lua, "parse");
    if (!lua_isfunction(m_lua, -1)) {
        *err = QStringLiteral("missing function parse(frame)");
        lua_pop(m_lua, 1);
        return false;
    }
    lua_pop(m_lua, 1);  // 每次 parse 重新取(脚本可能热替换)

    // 图形函数(可选,manifest graphics=true 时必需)
    lua_getglobal(m_lua, "render");
    m_has_graphics = lua_isfunction(m_lua, -1);
    lua_pop(m_lua, 1);
    if (m.graphics && !m_has_graphics) {
        *err = QStringLiteral("manifest graphics=true but missing function render(p,w,h)");
        return false;
    }

    // 全局 request_redraw():脚本主动请求重绘
    LuaBackend* self = this;
    lua_pushlightuserdata(m_lua, self);
    lua_pushcclosure(m_lua, [](lua_State* L) -> int {
        auto* backend = static_cast<LuaBackend*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (backend->m_redraw_cb) backend->m_redraw_cb();
        return 0;
    }, 1);
    lua_setglobal(m_lua, "request_redraw");
    return true;
}

void LuaBackend::shutdown() {
    if (m_lua) { lua_close(m_lua); m_lua = nullptr; }
    m_has_graphics = false;
}

QString LuaBackend::protocol_id() const {
    return m_protocol_id;
}

bool LuaBackend::has_graphics() const {
    return m_has_graphics;
}

QSize LuaBackend::graphics_preferred_size() const {
    return QSize(400, 300);
}

QImage LuaBackend::render_graphics(int w, int h, QString* err) {
    if (!m_has_graphics) {
        *err = QStringLiteral("no graphics");
        return QImage();
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        *err = QStringLiteral("bad size");
        return QImage();
    }
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    ScriptPainter bridge(&painter);

    lua_getglobal(m_lua, "render");
    if (!lua_isfunction(m_lua, -1)) {
        *err = QStringLiteral("render() not found");
        lua_pop(m_lua, 1);
        return QImage();
    }
    push_painter_table(m_lua, &bridge);
    lua_pushinteger(m_lua, w);
    lua_pushinteger(m_lua, h);
    if (lua_pcall(m_lua, 3, 0, 0) != LUA_OK) {
        *err = QStringLiteral("render: %1").arg(pop_lua_error(m_lua));
        return QImage();
    }
    painter.end();
    return img;
}

bool LuaBackend::handle_graphics_event(const GraphicsEvent& e, QString* err) {
    if (!m_has_graphics) return false;
    lua_getglobal(m_lua, "on_event");
    if (!lua_isfunction(m_lua, -1)) {
        lua_pop(m_lua, 1);
        return false;
    }
    lua_pushinteger(m_lua, static_cast<int>(e.type));
    lua_pushinteger(m_lua, e.x);
    lua_pushinteger(m_lua, e.y);
    lua_pushinteger(m_lua, e.button);
    lua_pushinteger(m_lua, e.modifiers);
    lua_pushinteger(m_lua, e.delta_y);
    if (lua_pcall(m_lua, 6, 1, 0) != LUA_OK) {
        *err = QStringLiteral("on_event: %1").arg(pop_lua_error(m_lua));
        return false;
    }
    const bool redraw = lua_toboolean(m_lua, -1);
    lua_pop(m_lua, 1);
    return redraw;
}

QString LuaBackend::table_str(const char* key, const QString& d) {
    QString r = d;
    if (lua_getfield(m_lua, -1, key) == LUA_TSTRING)
        r = QString::fromUtf8(lua_tostring(m_lua, -1));
    lua_pop(m_lua, 1);
    return r;
}

quint64 LuaBackend::table_uint(const char* key, quint64 d) {
    quint64 r = d;
    if (lua_isinteger(m_lua, lua_getfield(m_lua, -1, key)))
        r = static_cast<quint64>(lua_tointeger(m_lua, -1));
    else if (lua_isnumber(m_lua, -1))
        r = static_cast<quint64>(lua_tonumber(m_lua, -1));
    lua_pop(m_lua, 1);
    return r;
}

bool LuaBackend::convert_fields(QVector<MsduFieldNode>* out, QString* err) {
    if (!lua_istable(m_lua, -1)) {
        *err = QStringLiteral("fields must be a table");
        return false;
    }
    const lua_Integer n = luaL_len(m_lua, -1);
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_geti(m_lua, -1, i);  // field 表
        if (!lua_istable(m_lua, -1)) {
            *err = QStringLiteral("field #%1 must be a table").arg(i);
            lua_pop(m_lua, 1);
            return false;
        }
        MsduFieldNode node;
        node.name = table_str("name");
        node.value = table_str("value");
        // relStart/relLen 可选
        lua_getfield(m_lua, -1, "relStart");
        node.rel_start = lua_isinteger(m_lua, -1)
            ? static_cast<int>(lua_tointeger(m_lua, -1)) : -1;
        lua_pop(m_lua, 1);
        lua_getfield(m_lua, -1, "relLen");
        node.rel_len = lua_isinteger(m_lua, -1)
            ? static_cast<int>(lua_tointeger(m_lua, -1)) : 0;
        lua_pop(m_lua, 1);
        // children 可选(递归)
        if (lua_getfield(m_lua, -1, "children") == LUA_TTABLE) {
            if (!convert_fields(&node.children, err)) {
                lua_pop(m_lua, 2);  // children + field
                return false;
            }
        }
        lua_pop(m_lua, 1);  // children 或 nil
        lua_pop(m_lua, 1);  // field 表
        out->append(node);
    }
    return true;
}

ParseResult LuaBackend::parse(const BplcFrame& frame, MsduState& msdu,
                              const ParseFilter& filter, QString* err) {
    Q_UNUSED(msdu);   // Phase2:Lua 插件不支持跨帧重组
    Q_UNUSED(filter);
    ParseResult r;
    r.meta = frame.meta;
    r.raw_wire = frame.raw_wire;
    r.arrival_us = frame.arrival_us;
    r.payload_for_log = frame.data;

    lua_getglobal(m_lua, "parse");
    if (!lua_isfunction(m_lua, -1)) {
        *err = QStringLiteral("parse() not found");
        lua_pop(m_lua, 1);
        r.accept = false; r.reject_reason = *err;
        return r;
    }
    // frame 表
    lua_newtable(m_lua);
    push_byte_table(m_lua, frame.data);
    lua_setfield(m_lua, -2, "data");
    push_byte_table(m_lua, frame.raw_wire);
    lua_setfield(m_lua, -2, "rawWire");
    lua_pushnumber(m_lua, static_cast<lua_Number>(frame.arrival_us));
    lua_setfield(m_lua, -2, "arrivalUs");

    if (lua_pcall(m_lua, 1, 1, 0) != LUA_OK) {
        *err = QStringLiteral("parse: %1").arg(pop_lua_error(m_lua));
        r.accept = false; r.reject_reason = *err;
        return r;
    }
    if (!lua_istable(m_lua, -1)) {
        *err = QStringLiteral("parse() must return a table");
        lua_pop(m_lua, 1);
        r.accept = false; r.reject_reason = *err;
        return r;
    }

    lua_getfield(m_lua, -1, "accept");
    r.accept = lua_toboolean(m_lua, -1);
    lua_pop(m_lua, 1);
    if (!r.accept) {
        r.reject_reason = table_str("rejectReason",
                                    QStringLiteral("rejected by plugin"));
        lua_pop(m_lua, 1);  // result 表
        return r;
    }

    // mpdu 可选
    if (lua_getfield(m_lua, -1, "mpdu") == LUA_TTABLE) {
        r.mpdu.frame_type = static_cast<quint8>(table_uint("frameType"));
        r.mpdu.src_tei = static_cast<quint16>(table_uint("srcTei"));
        r.mpdu.dst_tei = static_cast<quint16>(table_uint("dstTei"));
        r.mpdu.net_id = static_cast<quint32>(table_uint("netId"));
    } else if (!frame.data.isEmpty()) {
        r.mpdu.frame_type = 1;
    }
    lua_pop(m_lua, 1);  // mpdu 或 nil

    r.msdu.present = true;
    r.msdu.summary = table_str("summary");
    if (lua_getfield(m_lua, -1, "fields") == LUA_TTABLE) {
        if (!convert_fields(&r.msdu.tree, err)) {
            lua_pop(m_lua, 2);  // fields + result
            r.accept = false; r.reject_reason = *err;
            r.msdu.tree.clear();
            return r;
        }
    }
    lua_pop(m_lua, 1);  // fields 或 nil
    lua_pop(m_lua, 1);  // result 表
    return r;
}
