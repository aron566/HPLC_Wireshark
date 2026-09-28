// backend_stub.cpp - 未编译后端的空实现,保证任何开关组合都能链接
#include "crash_backend.h"
#include "crash_handler.h"

#ifndef CRASH_HAVE_SENTRY
std::string backend_sentry_install(const CrashHandler::Options&) {
    return std::string();
}
void backend_sentry_shutdown() {}
#endif

#ifndef CRASH_HAVE_CRASHPAD
std::string backend_crashpad_install(const CrashHandler::Options&) {
    return std::string();
}
void backend_crashpad_shutdown() {}
#endif
