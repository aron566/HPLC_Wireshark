/// @file lua_backend.cpp
#include "lua_backend.h"

#include <QDir>
#include <QFile>

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
    return true;
}

void LuaBackend::shutdown() {
    if (m_lua) { lua_close(m_lua); m_lua = nullptr; }
}

QString LuaBackend::protocol_id() const {
    return m_protocol_id;
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
