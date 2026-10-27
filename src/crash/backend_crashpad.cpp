// backend_crashpad.cpp - crashpad 原生后端(无 Sentry 服务依赖)
//
// 崩溃时由 crashpad_handler(进程外)生成 minidump 到 database_path/reports。
// 上传:Options::upload_url 非空则 handler 自动上报到该地址;
//      为空但 dsn 非空时,自动从 Sentry DSN 派生 minidump 上报地址
//      (http(s)://host/api/<project>/minidump/?sentry_key=<key>);
//      两者都为空则只本地落盘。
// 需要 CRASH_HAVE_CRASHPAD 编译宏(由 crash_crashpad.pri 定义)。
#ifdef CRASH_HAVE_CRASHPAD

#include "crash_backend.h"
#include "crash_handler.h"
#include "crash_util.h"

#include "crashpad/client/crash_report_database.h"
#include "crashpad/client/crashpad_client.h"
#include "crashpad/client/settings.h"

#include <csignal>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>   // CONTEXT / RtlCaptureContext(SIGABRT 兜底抓上下文)
#endif

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

// DSN "http(s)://<key>@<host>[:port]/<project>" => Sentry minidump 上报地址
// (逻辑在 crash_util::sentry_dsn_to_minidump_url,此处仅做选择)。
std::string resolve_upload_url(const CrashHandler::Options& opts) {
    if (!opts.upload_url.empty())
        return opts.upload_url;
    return crash_util::sentry_dsn_to_minidump_url(opts.dsn);
}

} // namespace

// sentry-native 同款做法:从环境变量取代理地址传给 handler。
// 企业代理环境下不传代理会导致 handler 直连上传失败。
std::string resolve_proxy(const std::string& url) {
    const char* env = nullptr;
    if (url.compare(0, 8, "https://") == 0)
        env = std::getenv("https_proxy");
    else if (url.compare(0, 7, "http://") == 0)
        env = std::getenv("http_proxy");
    return env ? env : "";
}

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

    // url 非空 => handler 自动把 reports 里的 dump 上报到该地址(multipart,
    // 文件字段名 upload_file_minidump);为空 => 只本地落盘。
    const std::string url = resolve_upload_url(opts);
    if (!url.empty()) {
        // crashpad 要求数据库层面显式打开上传开关,否则 --url 也不会传。
        crashpad::Settings* settings = database->GetSettings();
        if (settings)
            settings->SetUploadsEnabled(true);
    }

    crashpad::CrashpadClient client;
    const bool ok = client.StartHandler(
        handler_path_fp, db_path, db_path,
        url,             // upload url
        resolve_proxy(url), // http_proxy:从环境变量取,代理环境下上传必需
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

namespace {

// 抓当前线程上下文 + 落一份非崩溃 minidump(SEH 未接管时的手动路径)。
// 非 Windows:空操作——Linux 的 abort dump 由 crashpad 自带 SIGABRT 捕获负责,
// 不要在这里补抓,也不要为此安装应用层钩子(会顶掉 crashpad 的处理器)。
void capture_and_dump_without_crash() {
#ifdef _WIN32
    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    crashpad::CrashpadClient::DumpWithoutCrash(ctx);
#endif
}

// SIGABRT 兜底(仅 Windows 安装):abort()/std::abort()/Qt qFatal(Q_ASSERT 失败)
// 最终触发 SIGABRT,这里抓上下文落一份 dump 后恢复默认并重新 raise,
// 让进程以正常 abort 语义终止。消息钩子不再重复落 dump,故这里是唯一一份。
// handler 内只做最小动作(抓上下文 + dump + raise)。
void abort_signal_handler(int) {
    capture_and_dump_without_crash();
    std::signal(SIGABRT, SIG_DFL);
    std::raise(SIGABRT);
}

} // namespace

void backend_crashpad_install_abort_hook() {
#ifdef _WIN32
    // Windows:SEH 只拦访问违例等异常,abort() 不走 SEH,必须由应用层钩子
    // 抓上下文落 dump;release 下 QT_FORCE_ASSERTS 让 Q_ASSERT 活着,
    // 失败走 qFatal→abort→SIGABRT,最终落到这里。
    std::signal(SIGABRT, abort_signal_handler);
#else
    // Linux:不要接管 SIGABRT。crashpad StartHandler 已安装自带的 SIGABRT
    // 捕获;应用层钩子会把它顶掉,反而使 abort 路径丢 dump(钩子内
    // capture_and_dump_without_crash 在非 Windows 上是空操作,随后复位重 raise
    // 直接按默认动作终止,无 minidump)。Linux 的 abort dump 全靠 crashpad。
#endif
}

void backend_crashpad_dump_without_crash() {
    capture_and_dump_without_crash();
}

#endif // CRASH_HAVE_CRASHPAD
