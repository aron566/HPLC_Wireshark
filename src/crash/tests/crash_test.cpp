// crash_test.cpp - 崩溃捕获独立测试(无 Qt 依赖)
//
// 用法:
//   crash_test <backend> [db_dir]
//   backend: sentry | crashpad
//
// 行为:安装 CrashHandler 后故意触发 SIGSEGV。
// 外层脚本检查 db_dir/reports/*.dmp 是否生成。
#include "crash_handler.h"

#include <cstdio>
#include <string>

static void do_crash() {
    // 故意空指针写,触发 SIGSEGV
    volatile int* p = nullptr;
    *p = 42;
}

int main(int argc, char** argv) {
    const std::string backend = argc > 1 ? argv[1] : "auto";
    const std::string db = argc > 2 ? argv[2] : "./crash_test_db";

    CrashHandler::Options opts;
    opts.backend = backend;
    opts.database_path = db;
    opts.release = "test-1.0";
    if (const char* dsn = std::getenv("SENTRY_DSN"))
        opts.dsn = dsn;

    const std::string active = CrashHandler::install(opts);
    std::printf("[crash_test] backend=%s active=%s db=%s\n",
                backend.c_str(), active.c_str(), db.c_str());
    std::fflush(stdout);

    if (active.empty()) {
        std::printf("[crash_test] install FAILED\n");
        return 2;
    }
    std::printf("[crash_test] crashing now...\n");
    std::fflush(stdout);
    do_crash();

    std::printf("[crash_test] SURVIVED (unexpected)\n");
    return 3;
}
