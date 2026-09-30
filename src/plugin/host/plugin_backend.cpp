/// @file plugin_backend.cpp
#include "plugin_backend.h"
#include "native_backend.h"
#include "js_backend.h"

IPluginBackend* create_backend(const QString& runtime) {
    if (runtime == QStringLiteral("native"))
        return new NativeBackend();
    if (runtime == QStringLiteral("js"))
        return new JsBackend();
    // lua: Phase2 后续
    return nullptr;
}
