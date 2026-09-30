/// @file plugin_backend.cpp
#include "plugin_backend.h"
#include "native_backend.h"
#include "js_backend.h"
#include "lua_backend.h"

IPluginBackend* create_backend(const QString& runtime) {
    if (runtime == QStringLiteral("native"))
        return new NativeBackend();
    if (runtime == QStringLiteral("js"))
        return new JsBackend();
    if (runtime == QStringLiteral("lua"))
        return new LuaBackend();
    return nullptr;
}
