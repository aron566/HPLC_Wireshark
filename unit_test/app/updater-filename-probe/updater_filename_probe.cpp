/// @file updater_filename_probe.cpp
/// @brief 更新包文件名净化回归(L8 + 保留名补丁):sanitized_download_file_name
///        须把 URL 纯文件名净化后落盘;Windows 保留设备名(CON/PRN/AUX/NUL/
///        COM1-9/LPT1-9,大小写不敏感)即使带后缀也不能落盘,一律前缀 '_' 改名。
///        另覆盖 L8 原有回归:路径穿越、非法字符、空名回退 update.bin。
/// @details 无网络:只调纯函数。预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。
#include <QApplication>
#include <QString>
#include <cstdio>

// sanitized_download_file_name 定义在 Updater.cpp(全局作用域,供探针验证);
// 此处只声明,不动上游形态的 Updater.h。
QString sanitized_download_file_name(const QString& download_url);

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}
static void check_name(const char* label, const QString& url, const QString& expect) {
    const QString got = sanitized_download_file_name(url);
    if (got != expect)
        std::printf("  got: %s expect: %s\n", qPrintable(got), qPrintable(expect));
    check(label, got == expect);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 保留设备名:前缀 '_' 改名(后缀/大小写/行尾 '.'/' ' 照此)
    check_name("reserved: CON renamed", QStringLiteral("https://example.com/CON"),
               QStringLiteral("_CON"));
    check_name("reserved: con lowercase renamed", QStringLiteral("https://example.com/con"),
               QStringLiteral("_con"));
    check_name("reserved: NUL.exe renamed", QStringLiteral("https://example.com/NUL.exe"),
               QStringLiteral("_NUL.exe"));
    check_name("reserved: COM1.zip renamed", QStringLiteral("https://example.com/COM1.zip"),
               QStringLiteral("_COM1.zip"));
    check_name("reserved: CON.txt renamed", QStringLiteral("https://example.com/CON.txt"),
               QStringLiteral("_CON.txt"));
    check_name("reserved: LPT9.bin renamed", QStringLiteral("https://example.com/LPT9.bin"),
               QStringLiteral("_LPT9.bin"));
    check_name("reserved: AUX renamed", QStringLiteral("https://example.com/AUX"),
               QStringLiteral("_AUX"));
    check_name("reserved: PRN. renamed", QStringLiteral("https://example.com/PRN."),
               QStringLiteral("_PRN"));
    check_name("reserved: com9 renamed", QStringLiteral("https://example.com/com9"),
               QStringLiteral("_com9"));
    check_name("reserved: lpt1.zip renamed", QStringLiteral("https://example.com/lpt1.zip"),
               QStringLiteral("_lpt1.zip"));
    check_name("reserved: CON%20 renamed", QStringLiteral("https://example.com/CON%20"),
               QStringLiteral("_CON"));

    // 非保留名:原样通过(COM10 两位数、CON 开头但非保留名均不在保留名之列)
    check_name("not reserved: COM10.zip kept", QStringLiteral("https://example.com/COM10.zip"),
               QStringLiteral("COM10.zip"));
    check_name("not reserved: CONTROLLER kept", QStringLiteral("https://example.com/CONTROLLER"),
               QStringLiteral("CONTROLLER"));
    check_name("not reserved: console kept", QStringLiteral("https://example.com/console"),
               QStringLiteral("console"));
    check_name("not reserved: update-1.2.3.exe kept",
               QStringLiteral("https://example.com/update-1.2.3.exe"),
               QStringLiteral("update-1.2.3.exe"));
    check_name("not reserved: normal.exe kept", QStringLiteral("https://example.com/normal.exe"),
               QStringLiteral("normal.exe"));

    // L8 原有回归:穿越/非法字符/空名
    check_name("L8: .. falls back to update.bin", QStringLiteral("https://example.com/.."),
               QStringLiteral("update.bin"));
    check_name("L8: empty path falls back", QStringLiteral("https://example.com/"),
               QStringLiteral("update.bin"));
    check_name("L8: query stripped", QStringLiteral("https://example.com/x.exe?y=z"),
               QStringLiteral("x.exe"));
    check_name("L8: trailing dot trimmed", QStringLiteral("https://example.com/file."),
               QStringLiteral("file"));
    // 反斜杠用例按平台区分期望:QFileInfo 对 '\' 的语义不同——
    // · 非 Windows:'\' 是普通字符,QFileInfo::fileName() 取到 "..\..\evil.exe",
    //   再被非法字符正则换成 '_' ⇒ ".._.._evil.exe"(穿越被中和);
    // · Windows:'\' 是路径分隔符(与 '/' 同级),fileName() 直接取 basename "evil.exe",
    //   结果不含任何分隔符、无穿越(反而更安全)。
#ifdef Q_OS_WIN
    check_name("L8: backslashes neutralised to plain name",
               QStringLiteral("https://example.com/..\\..\\evil.exe"),
               QStringLiteral("evil.exe"));
#else
    check_name("L8: backslashes neutralised to plain name",
               QStringLiteral("https://example.com/..\\..\\evil.exe"),
               QStringLiteral(".._.._evil.exe"));
#endif
    check_name("L8: illegal chars replaced", QStringLiteral("https://example.com/a<b>c.exe"),
               QStringLiteral("a_b_c.exe"));

    std::printf("\nRESULT: %s (%d failures)\n", g_fails == 0 ? "ALL PASS" : "FAILURES", g_fails);
    return g_fails == 0 ? 0 : 1;
}
