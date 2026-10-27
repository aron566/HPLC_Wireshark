// crash_handler.cpp - 后端分发 facade
//
// 按编译宏 CRASH_HAVE_SENTRY / CRASH_HAVE_CRASHPAD (由 crash_*.pri 定义)
// 决定链接哪些后端实现。运行时按 Options::backend 选择其一,互斥安装
// (两个后端都会接管崩溃信号,同时装会冲突)。

#include "crash_handler.h"
#include "crash_backend.h"

#include <string>

namespace {
std::string g_active_backend;   // 已激活后端("sentry"/"crashpad"),空=未激活
}

std::string CrashHandler::install(const Options& opts) {
    if (!g_active_backend.empty())
        return g_active_backend;

    std::string want = opts.backend;
    if (want == "auto") {
#ifdef CRASH_HAVE_SENTRY
        want = "sentry";
#elif defined(CRASH_HAVE_CRASHPAD)
        want = "crashpad";
#else
        return g_active_backend; // 两个后端都没编译
#endif
    }

    if (want == "sentry") {
        g_active_backend = backend_sentry_install(opts);
        if (!g_active_backend.empty())
            return g_active_backend;
        // sentry 安装失败则回退到 crashpad
        want = "crashpad";
    }
    if (want == "crashpad") {
        g_active_backend = backend_crashpad_install(opts);
        if (!g_active_backend.empty())
            backend_crashpad_install_abort_hook();  // SIGABRT 兜底(Windows:abort 钩子落 dump;Linux:crashpad 自带捕获,钩子为空操作)
    }
    return g_active_backend;
}

void CrashHandler::dump_without_crash() {
    if (g_active_backend == "crashpad")
        backend_crashpad_dump_without_crash();
}

void CrashHandler::shutdown() {
#ifdef CRASH_HAVE_SENTRY
    backend_sentry_shutdown();
#endif
#ifdef CRASH_HAVE_CRASHPAD
    backend_crashpad_shutdown();
#endif
}

bool CrashHandler::has_backend(const char* name) {
    const std::string n = name ? name : "";
#ifdef CRASH_HAVE_SENTRY
    if (n == "sentry")
        return true;
#endif
#ifdef CRASH_HAVE_CRASHPAD
    if (n == "crashpad")
        return true;
#endif
    return false;
}
