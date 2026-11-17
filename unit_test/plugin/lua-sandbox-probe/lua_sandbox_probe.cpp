/// @file lua_sandbox_probe.cpp
/// @brief Lua 插件沙箱回归探针(src/plugin/host/lua_backend.cpp open_sandboxed_libs)
/// @details 合成一个探测插件:脚本在 get_info() 里逐项自检沙箱,全部通过才返回
///          与清单一致的 protocolId,否则把首个失败项作为 protocolId 返回
///          (LuaBackend::initialize 会以 "protocolId mismatch" 报出)。
///          另外确认仓库自带的 Lua 示例插件(src/plugin/examples/lua_*)仍能加载。
///          预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <stdexcept>

#include "lua_backend.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

static bool write_file(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

// 每项:{条件表达式, 失败名}。条件为真 = 沙箱行为符合预期
static const char* kProbeScript = R"LUA(
local function bad_dofile(p) return not pcall(dofile, p) end
-- 把错误消息里回显的 '文件名' / '模块名' 归一,只比较其余部分
local function norm(e) return (tostring(e):gsub("'[^']*'", "''")) end
local function pack(...) return {n = select("#", ...), ...} end
local function same_loadfile(a, b)
  local ra, rb = pack(loadfile(a)), pack(loadfile(b))
  return ra.n == rb.n and ra.n == 2 and ra[1] == nil and rb[1] == nil
     and type(ra[2]) == "string" and norm(ra[2]) == norm(rb[2])
end
local function same_dofile(a, b)
  local oka, ea = pcall(dofile, a)
  local okb, eb = pcall(dofile, b)
  return not oka and not okb and type(ea) == type(eb) and norm(ea) == norm(eb)
end
local function same_require(a, b)
  local saved = package.path
  package.path = PARENT .. "/?.lua"
  package.loaded[a] = nil; package.loaded[b] = nil
  local oka, ea = pcall(require, a)
  local okb, eb = pcall(require, b)
  package.path = saved
  return not oka and not okb and norm(ea) == norm(eb)
end
local checks = {
  {function() return io == nil end, "io-present"},
  {function() return debug == nil end, "debug-present"},
  {function() return os.execute == nil and os.exit == nil end, "os.execute/exit-present"},
  {function() return os.remove == nil and os.rename == nil and os.tmpname == nil end, "os.fs-present"},
  {function() return os.getenv == nil end, "os.getenv-present"},
  {function() return type(os.time()) == "number" and type(os.clock()) == "number" end, "os.time-missing"},
  {function() return package.loadlib == nil and package.cpath == "" end, "package.loadlib/cpath"},
  {function() return #package.searchers == 2 end, "C-searchers-present"},
  {function() return require("helper").v == 42 end, "require-in-dir-broken"},
  {function() return dofile("helper.lua").v == 42 end, "dofile-in-dir-broken"},
  {function() return loadfile("helper.lua") ~= nil end, "loadfile-in-dir-broken"},
  {function() return loadfile("/etc/hostname") == nil end, "loadfile-abs-allowed"},
  {function() return loadfile("../outside.lua") == nil end, "loadfile-dotdot-allowed"},
  -- 防探测:沙箱外"存在的文件"与"不存在的文件"必须给出完全相同的结果
  -- (返回值个数/类型、错误消息去掉回显的文件名后逐字相同),否则插件能
  -- 探出沙箱外任意路径是否存在。词法越界、绝对路径、symlink 逃逸、悬空
  -- 链接、插件目录内缺失文件,一律同一结果。
  {function() return same_loadfile("../outside.lua", "../no-such-file.lua") end, "loadfile-outside-exists-vs-missing-differ"},
  {function() return same_loadfile(PARENT .. "/outside.lua", PARENT .. "/no-such-file.lua") end, "loadfile-abs-exists-vs-missing-differ"},
  {function() return same_loadfile("../../../../../../etc/passwd", "../../../../../../etc/no-such-file") end, "loadfile-etc-exists-vs-missing-differ"},
  {function() return not HAVE_SYMLINKS or same_loadfile("escape_link.lua", "dangling_link.lua") end, "loadfile-symlink-escape-vs-dangling-differ"},
  {function() return not HAVE_SYMLINKS or same_loadfile("escape_link.lua", "missing.lua") end, "loadfile-symlink-escape-vs-missing-differ"},
  {function() return same_loadfile("../outside.lua", "missing.lua") end, "loadfile-outside-vs-inside-missing-differ"},
  {function() return same_dofile("../outside.lua", "../no-such-file.lua") end, "dofile-outside-exists-vs-missing-differ"},
  {function() return same_dofile(PARENT .. "/outside.lua", PARENT .. "/no-such-file.lua") end, "dofile-abs-exists-vs-missing-differ"},
  {function() return not HAVE_SYMLINKS or same_dofile("escape_link.lua", "dangling_link.lua") end, "dofile-symlink-escape-vs-dangling-differ"},
  {function() return same_dofile("../outside.lua", "missing.lua") end, "dofile-outside-vs-inside-missing-differ"},
  {function() return same_require("outside", "no_such_module") end, "require-outside-exists-vs-missing-differ"},
  -- 消息里不得再出现能区分两类失败的旧措辞
  {function() local _, e1 = loadfile("../outside.lua"); local _, e2 = loadfile("missing.lua")
     return not e1:find("does not exist", 1, true) and not e2:find("does not exist", 1, true)
        and not e1:find("is outside", 1, true) and not e2:find("is outside", 1, true) end, "loadfile-legacy-distinct-msg"},
  {function() return not HAVE_SYMLINKS or loadfile("escape_link.lua") == nil end, "loadfile-symlink-escape-allowed"},
  {function() return bad_dofile("../outside.lua") end, "dofile-outside-allowed"},
  -- L12:dofile/require 一个目录:luaL_loadfilex 失败走 lua_error/luaL_error,
  -- 旧实现此时栈上有待析构的 QString/QByteArray(longjmp 跳过析构);
  -- 之后 load/require 仍须通过,证明后端未损坏。
  {function() return bad_dofile("subdir") end, "dofile-dir-allowed"},
  {function() return not pcall(require, "badmod") end, "require-dir-allowed"},
  {function() return load("return 7")() == 7 end, "load-text-broken"},
  {function() local env = {x = 5}; return load("return x", "c", "t", env)() == 5 end, "load-env-broken"},
  {function() return load(string.dump(function() return 1 end)) == nil end, "load-bytecode-allowed"},
  {function() return load(string.dump(function() return 1 end), "b", "b") == nil end, "load-mode-b-allowed"},
  {function()
     package.path = PARENT .. "/?.lua"
     package.loaded["outside"] = nil
     local ok = pcall(require, "outside")
     return not ok
   end, "require-outside-allowed"},
  -- L14:package.searchpath 只做文件存在性检查、不经 sandbox_plugin_path 越界判定,
  -- 留着它插件脚本就能拿它探任意路径;沙箱里必须为 nil。
  {function() return package.searchpath == nil end, "package.searchpath-present"},
  -- L14:带 __gc 的元表必须被 setmetatable 拒绝(GC 回调可能落在宿主拆状态/切线程时刻)
  {function()
     local ok = pcall(setmetatable, {}, {__gc = function() end})
     return not ok
   end, "setmetatable-__gc-allowed"},
  -- 不带 __gc 的元表仍须可用,证明包装没有把 setmetatable 整体废掉
  {function()
     local t = setmetatable({}, {__index = function() return 9 end})
     return t.anything == 9
   end, "setmetatable-plain-broken"},
}
local fail = nil
for _, c in ipairs(checks) do
  local ok, res = pcall(c[1])
  if not (ok and res) then fail = c[2] .. (ok and "" or (": " .. tostring(res))); break end
end
function get_info() return {protocolId = fail or "SANDBOX_OK", displayName = "sandbox"} end
function parse(frame) return {accept = false} end
function dofile_once() return tostring(dofile("helper.lua").v) end
)LUA";

static void test_sandbox(const QString& root) {
    const QString parent = root + QStringLiteral("/p");
    const QString dir = parent + QStringLiteral("/lua-sandbox");
    QDir().mkpath(dir);
    write_file(parent + QStringLiteral("/outside.lua"), "return {v = 666}");
    write_file(dir + QStringLiteral("/helper.lua"), "return {v = 42}");
    QDir().mkpath(dir + QStringLiteral("/subdir"));       // L12:dofile(目录)
    QDir().mkpath(dir + QStringLiteral("/badmod.lua"));   // L12:require(目录)
    // 防探测:插件目录内指向沙箱外真实文件的 symlink,与悬空 symlink
    // (Windows 上 QFile::link 生成 .lnk 快捷方式而非 symlink,此时跳过 symlink 项)
    const bool have_symlinks =
        QFile::link(parent + QStringLiteral("/outside.lua"), dir + QStringLiteral("/escape_link.lua")) &&
        QFile::link(parent + QStringLiteral("/no-such-target.lua"),
                    dir + QStringLiteral("/dangling_link.lua")) &&
        QFileInfo(dir + QStringLiteral("/escape_link.lua")).isSymLink() &&
        QFileInfo(dir + QStringLiteral("/dangling_link.lua")).isSymLink();
    if (!have_symlinks) std::printf("  note: symlinks unavailable, symlink probes skipped\n");
    write_file(dir + QStringLiteral("/main.lua"),
               QByteArray("PARENT = [[") + parent.toUtf8() + "]]\n" +
                   (have_symlinks ? "HAVE_SYMLINKS = true\n" : "HAVE_SYMLINKS = false\n") +
                   kProbeScript);
    write_file(dir + QStringLiteral("/plugin.json"),
               R"({"name":"lua-sandbox","version":"1.0.0","runtime":"lua","entry":"main.lua",)"
               R"("api_version":1,"protocol_id":"SANDBOX_OK"})");
    const PluginManifest m = read_plugin_manifest(dir);
    check("sandbox: manifest valid", m.valid);
    LuaBackend b;
    QString err;
    const bool ok = b.initialize(m, &err);
    if (!ok) std::printf("  err: %s\n", qPrintable(err));
    check("sandbox: all in-script checks pass", ok);

    // P1(TOCTOU, fix A):一次 dofile 只解析沙箱路径一次(两次解析之间换
    // symlink 可越界)。黑盒并发换链测试写不出确定性的,故只做结构断言。
    LuaBackend::reset_sandbox_path_resolve_count();
    const QString v = b.call_text_function("dofile_once", &err);
    check("dofile resolves sandbox path exactly once",
          v == QStringLiteral("42") && LuaBackend::sandbox_path_resolve_count() == 1);

    // 入口脚本为字节码时拒绝加载
    const QString bdir = parent + QStringLiteral("/lua-bytecode");
    QDir().mkpath(bdir);
    write_file(bdir + QStringLiteral("/main.lua"), QByteArray("\x1bLua\x54\x00", 6));
    write_file(bdir + QStringLiteral("/plugin.json"),
               R"({"name":"lua-bytecode","version":"1.0.0","runtime":"lua","entry":"main.lua",)"
               R"("api_version":1,"protocol_id":"X"})");
    LuaBackend b2;
    check("sandbox: bytecode entry rejected",
          !b2.initialize(read_plugin_manifest(bdir), &err) &&
              err.contains(QStringLiteral("attempt to load a binary chunk")));
}

// N2:宿主回调抛出的 C++ 异常不能穿过 lua_pcall(未定义行为)。脚本 parse() 里
// 调 request_redraw(),回调抛 std::runtime_error;protected_entry 应接住转成
// Lua 错误,parse() 正常返回拒收、后端仍可用。旧实现异常穿出即崩溃(terminate)。
static void test_cpp_exception(const QString& root) {
    const QString dir = root + QStringLiteral("/lua-throw");
    QDir().mkpath(dir);
    write_file(dir + QStringLiteral("/main.lua"),
               "function get_info() return {protocolId = \"THROW_OK\", displayName = \"throw\"} end\n"
               "function parse(frame) request_redraw() return {accept = true} end\n");
    write_file(dir + QStringLiteral("/plugin.json"),
               R"({"name":"lua-throw","version":"1.0.0","runtime":"lua","entry":"main.lua",)"
               R"("api_version":1,"protocol_id":"THROW_OK"})");
    LuaBackend b;
    QString err;
    check("N2: throwing backend initializes", b.initialize(read_plugin_manifest(dir), &err));
    b.set_redraw_callback([]() { throw std::runtime_error("redraw boom"); });
    BplcFrame frame;
    MsduState msdu;
    ParseFilter filter;
    const ParseResult r = b.parse(frame, msdu, filter, &err);
    check("N2: C++ exception becomes Lua error, no crash",
          !r.accept && r.reject_reason.contains(QStringLiteral("redraw boom")));
    // 后端仍可用:回调不再抛错后同一脚本正常解析
    b.set_redraw_callback([]() {});
    const ParseResult r2 = b.parse(frame, msdu, filter, &err);
    check("N2: backend still usable after the exception", r2.accept);
}

static void test_bundled_examples() {
    const QDir ex(QStringLiteral(BPLC_REPO_DIR "/src/plugin/examples"));
    int n = 0;
    for (const QString& d : ex.entryList({QStringLiteral("lua_*")}, QDir::Dirs)) {
        const PluginManifest m = read_plugin_manifest(ex.absoluteFilePath(d));
        LuaBackend b;
        QString err;
        const bool ok = m.valid && b.initialize(m, &err);
        if (!ok) std::printf("  %s: %s\n", qPrintable(d), qPrintable(m.valid ? err : m.error));
        check(qPrintable(QStringLiteral("bundled example loads: %1").arg(d)), ok);
        ++n;
    }
    check("bundled lua examples found (>=4)", n >= 4);
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid()) return 2;
    test_sandbox(QDir(tmp.path()).canonicalPath());
    test_cpp_exception(QDir(tmp.path()).canonicalPath());
    test_bundled_examples();
    std::printf("\nRESULT: %s (%d failures)\n", g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
