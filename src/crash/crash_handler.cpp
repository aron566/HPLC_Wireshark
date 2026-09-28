// crash_handler.cpp - 后端分发 facade
//
// 按编译宏 CRASH_HAVE_SENTRY / CRASH_HAVE_CRASHPAD (由 crash_*.pri 定义)
// 决定链接哪些后端实现。运行时按 Options::backend 选择其一,互斥安装
// (两个后端都会接管崩溃信号,同时装会冲突)。

#include "crash_handler.h"
#include "crash_backend.h"

#include <string>

std::string CrashHandler::install(const Options& opts) {
    static std::string active; // 只能安装一次
    if (!active.empty())
        return active;

    std::string want = opts.backend;
    if (want == "auto") {
#ifdef CRASH_HAVE_SENTRY
        want = "sentry";
#elif defined(CRASH_HAVE_CRASHPAD)
        want = "crashpad";
#else
        return active; // 两个后端都没编译
#endif
    }

    if (want == "sentry") {
        active = backend_sentry_install(opts);
        if (!active.empty())
            return active;
        // sentry 安装失败则回退到 crashpad
        want = "crashpad";
    }
    if (want == "crashpad") {
        active = backend_crashpad_install(opts);
    }
    return active;
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
