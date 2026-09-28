// backend_crashpad.cpp - crashpad 原生后端(无 Sentry 服务依赖)
//
// 崩溃时由 crashpad_handler(进程外)生成 minidump 到 database_path/reports,
// 不上传。用户手动取回 .dmp,用 minidump 工具分析。
// 需要 CRASH_HAVE_CRASHPAD 编译宏(由 crash_crashpad.pri 定义)。
#ifdef CRASH_HAVE_CRASHPAD

#include "crash_backend.h"
#include "crash_handler.h"
#include "crash_util.h"

#include "crashpad/client/crash_report_database.h"
#include "crashpad/client/crashpad_client.h"
#include "crashpad/client/settings.h"

#include <map>
#include <string>
#include <vector>

namespace {

bool g_installed = false;

std::string default_db_path(const CrashHandler::Options& opts) {
    if (!opts.database_path.empty())
        return opts.database_path;
    return crash_util::join(crash_util::exe_dir(), "crashpad_db");
}

std::string handler_path() {
#ifdef _WIN32
    return crash_util::join(crash_util::exe_dir(), "crashpad_handler.exe");
#else
    return crash_util::join(crash_util::exe_dir(), "crashpad_handler");
#endif
}

} // namespace

std::string backend_crashpad_install(const CrashHandler::Options& opts) {
    if (g_installed)
        return "crashpad";

    const std::string db = default_db_path(opts);
    if (!crash_util::ensure_dir(db))
        return std::string();

    // 初始化崩溃数据库目录结构
    base::FilePath db_path(base::FilePath::StringType(db.begin(), db.end()));
    std::unique_ptr<crashpad::CrashReportDatabase> database =
        crashpad::CrashReportDatabase::Initialize(db_path);
    if (!database)
        return std::string();

    const std::string hp = handler_path();
    base::FilePath handler_path_fp(
        base::FilePath::StringType(hp.begin(), hp.end()));

    std::map<std::string, std::string> annotations;
    annotations["product"] = opts.product_name;
    if (!opts.release.empty())
        annotations["version"] = opts.release;

    std::vector<std::string> arguments;
#if defined(__linux__) || defined(__APPLE__)
    // Linux/macOS 上 handler 用 ptrace 附着;容器/受限环境可能需要放宽 yama 设置,
    // 失败时 StartHandler 返回 false,调用方回退。
#endif

    crashpad::CrashpadClient client;
    // url 为空 => 只本地落盘,不上传
    const bool ok = client.StartHandler(
        handler_path_fp, db_path, db_path,
        std::string(),   // url
        std::string(),   // http_proxy
        annotations, arguments,
        true,            // restartable:handler 崩溃后可重启
        true);           // asynchronous_start
    if (!ok)
        return std::string();

    g_installed = true;
    return "crashpad";
}

void backend_crashpad_shutdown() {
    // crashpad 为进程外 handler,随进程退出自动清理,无需显式关闭
}

#endif // CRASH_HAVE_CRASHPAD
