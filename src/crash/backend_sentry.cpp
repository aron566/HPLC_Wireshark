// backend_sentry.cpp - sentry-native 后端
//
// 崩溃时由 crashpad_handler(进程外)生成 minidump,写入 database_path,
// 有 DSN 则上传到 Sentry 服务,无 DSN 则仅本地落盘。
// 需要 CRASH_HAVE_SENTRY 编译宏(由 crash_sentry.pri 定义)。
#ifdef CRASH_HAVE_SENTRY

#include "crash_backend.h"
#include "crash_handler.h"
#include "crash_util.h"

#include <sentry.h>

#include <string>

namespace {

std::string g_db_path;

std::string default_db_path(const CrashHandler::Options& opts) {
    if (!opts.database_path.empty())
        return opts.database_path;
    return crash_util::join(crash_util::exe_dir(), "crashpad_db");
}

// crashpad_handler 与主程序放同一目录(随安装包发布)
std::string handler_path() {
#ifdef _WIN32
    return crash_util::join(crash_util::exe_dir(), "crashpad_handler.exe");
#else
    return crash_util::join(crash_util::exe_dir(), "crashpad_handler");
#endif
}

} // namespace

std::string backend_sentry_install(const CrashHandler::Options& opts) {
    g_db_path = default_db_path(opts);
    if (!crash_util::ensure_dir(g_db_path))
        return std::string();

    sentry_options_t* options = sentry_options_new();
    if (!options)
        return std::string();

    // DSN 为空 => 不上传,仅本地落盘(仍生成 minidump 可供手动分析)
    sentry_options_set_dsn(options, opts.dsn.c_str());
    sentry_options_set_database_path(options, g_db_path.c_str());
    sentry_options_set_handler_path(options, handler_path().c_str());
    if (!opts.release.empty())
        sentry_options_set_release(options, opts.release.c_str());
    sentry_options_set_debug(options, 0);

    if (sentry_init(options) != 0) {
        sentry_options_free(options);
        return std::string();
    }
    // sentry_init 成功后接管 options 所有权,无需 free
    return "sentry";
}

void backend_sentry_shutdown() {
    sentry_close();
}

#endif // CRASH_HAVE_SENTRY
