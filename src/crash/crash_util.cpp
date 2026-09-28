// crash_util.cpp
#include "crash_util.h"

#include <cerrno>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#else
#  include <limits.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace crash_util {

std::string exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::string();
    std::wstring w(buf, n);
    const size_t p = w.find_last_of(L"\\/");
    std::wstring dir = (p == std::wstring::npos) ? L"." : w.substr(0, p);
    // 转窄字符(路径一般为 ASCII;含中文时退化处理)
    std::string out;
    out.reserve(dir.size());
    for (wchar_t c : dir)
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
#else
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return std::string();
    buf[n] = '\0';
    std::string p(buf);
    const size_t s = p.find_last_of('/');
    return (s == std::string::npos) ? "." : p.substr(0, s);
#endif
}

bool ensure_dir(const std::string& path) {
    if (path.empty())
        return false;
#ifdef _WIN32
    // 递归创建:逐级 _wmkdir
    std::wstring w;
    w.reserve(path.size());
    for (char c : path)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'/' || w[i] == L'\\') {
            std::wstring part = w.substr(0, i);
            if (!part.empty())
                _wmkdir(part.c_str());
        }
    }
    return _wmkdir(w.c_str()) == 0 || errno == EEXIST;
#else
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' && cur.size() > 1) {
            if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST)
                return false;
        }
    }
    return ::mkdir(cur.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty())
        return b;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (a.back() == '/' || a.back() == '\\')
        return a + b;
    return a + sep + b;
}

} // namespace crash_util
