// test_dsn_url.cpp - sentry_dsn_to_minidump_url 单元测试(无第三方依赖)
#include "crash_util.h"

#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;

static void check(const char* name, const std::string& dsn, const std::string& expect) {
    const std::string got = crash_util::sentry_dsn_to_minidump_url(dsn);
    if (got == expect) {
        ++g_pass;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n  dsn:    %s\n  expect: %s\n  got:    %s\n",
                    name, dsn.c_str(), expect.c_str(), got.c_str());
    }
}

int main() {
    check("http 标准", "http://testkey@127.0.0.1:9000/1",
          "http://127.0.0.1:9000/api/1/minidump/?sentry_key=testkey");
    check("https 无端口", "https://abc123@sentry.example.com/42",
          "https://sentry.example.com/api/42/minidump/?sentry_key=abc123");
    check("空 DSN", "", "");
    check("无 scheme", "testkey@host/1", "");
    check("非法 scheme", "ftp://key@host/1", "");
    check("无 @", "http://host/1", "");
    check("空 key", "http:///host/1", "");
    check("无 project", "http://key@host/", "");
    check("缺 project 段", "http://key@host", "");

    std::printf("PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
