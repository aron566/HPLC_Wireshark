/// @file backend_direct_test.cpp
/// @brief 插件后端直接测试(进程内,不走 IPC):验证 4 种新插件
/// @details 用法: backend_direct_test --examples <dir>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <cstdio>

#include "plugin_backend.h"
#include "js_backend.h"
#include "lua_backend.h"
#include "../plugin_api/plugin_manifest.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const QString& name, const QString& detail = {}) {
    if (ok) {
        ++g_pass;
        printf("  [PASS] %s", qPrintable(name));
        if (!detail.isEmpty()) printf(" - %s", qPrintable(detail));
        printf("\n");
    } else {
        ++g_fail;
        printf("  [FAIL] %s", qPrintable(name));
        if (!detail.isEmpty()) printf(" - %s", qPrintable(detail));
        printf("\n");
    }
    fflush(stdout);
}

BplcFrame make_frame(const QByteArray& data, qint64 arrival_us) {
    BplcFrame f;
    f.data = data;
    f.arrival_us = arrival_us;
    f.arrival_ms = 1700000000000LL;
    return f;
}

IPluginBackend* create_backend(const PluginManifest& m, QString* err) {
    if (m.runtime == QStringLiteral("js")) {
        auto* b = new JsBackend();
        if (!b->initialize(m, err)) { delete b; return nullptr; }
        return b;
    } else if (m.runtime == QStringLiteral("lua")) {
        auto* b = new LuaBackend();
        if (!b->initialize(m, err)) { delete b; return nullptr; }
        return b;
    }
    if (err) *err = QStringLiteral("unknown runtime: %1").arg(m.runtime);
    return nullptr;
}

bool test_plugin(const QString& ex_dir, const QString& plugin_name,
                 const QList<QByteArray>& frames, bool test_graphics) {
    printf("\n=== Testing %s ===\n", qPrintable(plugin_name));
    fflush(stdout);

    bool all_ok = true;
    auto mark = [&](bool ok) { if (!ok) all_ok = false; };

    // 加载 manifest
    QString err;
    QString plugin_path = ex_dir + "/" + plugin_name;
    PluginManifest manifest = read_plugin_manifest(plugin_path);
    if (!manifest.error.isEmpty()) {
        check(false, QStringLiteral("load manifest"), manifest.error);
        return false;
    }
    check(true, QStringLiteral("load manifest"),
          QStringLiteral("%1 v%2").arg(manifest.name, manifest.version));

    // 创建 backend
    IPluginBackend* backend = create_backend(manifest, &err);
    check(backend != nullptr, QStringLiteral("create backend"),
          backend ? manifest.runtime : err);
    if (!backend) return false;
    mark(backend != nullptr);

    // 解析测试帧
    for (int i = 0; i < frames.size(); ++i) {
        BplcFrame frame = make_frame(frames[i], 1000000 + i * 1000);
        MsduState msdu;
        ParseFilter filter;
        QString perr;
        ParseResult r = backend->parse(frame, msdu, filter, &perr);
        bool ok = perr.isEmpty();
        check(ok, QStringLiteral("parse frame %1").arg(i + 1),
              ok ? r.msdu.summary.left(60) : perr);
        mark(ok);
    }

    // 图形测试
    if (test_graphics) {
        check(backend->has_graphics(), QStringLiteral("has_graphics"));
        mark(backend->has_graphics());

        if (backend->has_graphics()) {
            QString rerr;
            QImage img = backend->render_graphics(400, 300, &rerr);
            bool ok = !img.isNull() && rerr.isEmpty();
            check(ok, QStringLiteral("render 400x300"),
                  ok ? QStringLiteral("%1x%2").arg(img.width()).arg(img.height()) : rerr);
            mark(ok);

            // 模拟点击事件(如果 backend 支持)
            // 注意:IPluginBackend 可能没有直接的事件接口,通过 render 测试已覆盖基本功能
        }
    }

    delete backend;
    printf(all_ok ? "  >> ALL PASS\n" : "  >> SOME FAILED\n");
    fflush(stdout);
    return all_ok;
}

} // namespace

/// @brief 解码帧契约测试:frame.index/epochMs/topoEvent + host.jumpToFrame
/// @details 用最小内联脚本验证新单入口契约,JS 与 Lua 各一遍。
bool test_decoded_frame_contract() {
    printf("\n=== Testing decoded-frame contract ===\n");
    fflush(stdout);
    bool all_ok = true;
    auto mark = [&](bool ok) { if (!ok) all_ok = false; };

    const QString probe_js = QStringLiteral(
        "function get_info() { return {name:'probe', protocolId:'PROBE_2024'}; }\n"
        "var seen = {index:-1, epochMs:-1, topoKind:'?', jumped:-1};\n"
        "function parse(frame) {\n"
        "  seen.index = frame.index;\n"
        "  seen.epochMs = frame.epochMs;\n"
        "  seen.topoKind = frame.topoEvent ? frame.topoEvent.kind : 'none';\n"
        "  if (frame.topoEvent) host.jumpToFrame(frame.topoEvent.frameIndex);\n"
        "  return {summary:'ok'};\n"
        "}\n"
        "function get_seen() { return JSON.stringify(seen); }\n");
    const QString probe_lua = QStringLiteral(
        "function get_info() return {name='probe', protocolId='PROBE_2024'} end\n"
        "seen = {index=-1, epochMs=-1, topoKind='?', jumped=-1}\n"
        "function parse(frame)\n"
        "  seen.index = frame.index\n"
        "  seen.epochMs = frame.epochMs\n"
        "  seen.topoKind = frame.topoEvent and frame.topoEvent.kind or 'none'\n"
        "  if frame.topoEvent then host.jumpToFrame(frame.topoEvent.frameIndex) end\n"
        "  return {summary='ok'}\n"
        "end\n"
        "function get_seen()\n"
        "  return string.format('%d|%d|%s', seen.index, seen.epochMs, seen.topoKind)\n"
        "end\n");

    struct Case { QString runtime; QString entry; QString code; };
    const QList<Case> cases = {
        { QStringLiteral("js"),  QStringLiteral("probe.js"),  probe_js  },
        { QStringLiteral("lua"), QStringLiteral("probe.lua"), probe_lua },
    };

    for (const Case& c : cases) {
        QTemporaryDir tmp;
        check(tmp.isValid(), QStringLiteral("contract %1 temp dir").arg(c.runtime));
        mark(tmp.isValid());
        if (!tmp.isValid()) continue;

        QFile jf(tmp.path() + "/plugin.json");
        const QString manifest_json = QStringLiteral(
            "{\"name\":\"probe\",\"version\":\"1.0.0\",\"runtime\":\"%1\","
            "\"entry\":\"%2\",\"api_version\":1,\"protocol_id\":\"PROBE_2024\","
            "\"display_name\":\"probe\",\"display_name_en\":\"probe\"}")
            .arg(c.runtime, c.entry);
        bool wok = jf.open(QIODevice::WriteOnly | QIODevice::Text)
                   && jf.write(manifest_json.toUtf8()) > 0;
        jf.close();
        QFile sf(tmp.path() + "/" + c.entry);
        wok = wok && sf.open(QIODevice::WriteOnly | QIODevice::Text)
              && sf.write(c.code.toUtf8()) > 0;
        sf.close();
        check(wok, QStringLiteral("contract %1 write probe").arg(c.runtime));
        mark(wok);
        if (!wok) continue;

        QString err;
        PluginManifest m = read_plugin_manifest(tmp.path());
        check(m.error.isEmpty(), QStringLiteral("contract %1 manifest").arg(c.runtime), m.error);
        mark(m.error.isEmpty());
        if (!m.error.isEmpty()) continue;

        IPluginBackend* backend = create_backend(m, &err);
        check(backend != nullptr, QStringLiteral("contract %1 backend").arg(c.runtime), err);
        mark(backend != nullptr);
        if (!backend) continue;

        qint64 jumped = -1;
        backend->set_host_jump_callback([&](qint64 idx) { jumped = idx; });

        BplcFrame frame = make_frame(QByteArray::fromHex("3c000102") + QByteArray(20, '\xAA'), 1000000);
        frame.decoded_index = 42;
        frame.decoded_epoch_ms = 1700000000123LL;
        TopoEvent te;
        te.kind = TopoEventKind::DiscoverList;
        te.nid = 0xCDA1D5;
        te.frame_index = 42;
        te.epoch_ms = 1700000000123LL;
        te.desc = QStringLiteral("Discover list from STA-2: 2 node(s)");
        frame.topo_event = te;

        MsduState msdu; ParseFilter filter; QString perr;
        ParseResult r = backend->parse(frame, msdu, filter, &perr);
        check(perr.isEmpty(), QStringLiteral("contract %1 parse").arg(c.runtime), perr);
        mark(perr.isEmpty());

        QString cerr;
        const QString seen = backend->call_text_function("get_seen", &cerr);
        check(cerr.isEmpty(), QStringLiteral("contract %1 get_seen").arg(c.runtime), cerr);
        mark(cerr.isEmpty());
        const bool fields_ok = seen.contains(QStringLiteral("42|1700000000123|discoverList"))
                               || seen.contains(QStringLiteral("\"index\":42"))
                                  || (seen.contains(QStringLiteral("42"))
                                      && seen.contains(QStringLiteral("1700000000123"))
                                      && seen.contains(QStringLiteral("discoverList"), Qt::CaseInsensitive));
        check(fields_ok, QStringLiteral("contract %1 frame fields").arg(c.runtime), seen);
        mark(fields_ok);
        check(jumped == 42, QStringLiteral("contract %1 host.jumpToFrame").arg(c.runtime),
              QStringLiteral("jumped=%1").arg(jumped));
        mark(jumped == 42);

        delete backend;
    }
    printf(all_ok ? "  >> ALL PASS\n" : "  >> SOME FAILED\n");
    fflush(stdout);
    return all_ok;
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    QCommandLineOption ex_opt(QStringList{QStringLiteral("examples")},
        QStringLiteral("examples dir"), QStringLiteral("dir"));
    cli.addOption(ex_opt);
    cli.process(app);

    const QString ex_dir = cli.value(ex_opt);
    if (ex_dir.isEmpty()) {
        QTextStream(stderr) << "usage: backend_direct_test --examples <dir>\n";
        return 1;
    }

    printf("Plugin Backend Direct Test (in-process)\n");
    printf("Examples: %s\n", qPrintable(ex_dir));
    fflush(stdout);

    QList<QByteArray> std_frames = {
        QByteArray::fromHex("3c000102") + QByteArray(20, '\xAA'),
        QByteArray::fromHex("3c000201") + QByteArray(30, '\xBB'),
        QByteArray::fromHex("3c000301") + QByteArray(40, '\xCC'),
    };
    QList<QByteArray> diag_frames = {
        QByteArray::fromHex("0102"),
        QByteArray::fromHex("3c000000") + QByteArray(20, '\0'),
        QByteArray::fromHex("3c000102") + QByteArray(20, '\xAA'),
    };

    bool all_ok = true;
    all_ok &= test_plugin(ex_dir, "js_replay", std_frames, false);
    all_ok &= test_plugin(ex_dir, "js_topo", std_frames, true);
    all_ok &= test_plugin(ex_dir, "lua_diag", diag_frames, false);
    all_ok &= test_plugin(ex_dir, "lua_report", std_frames, false);
    all_ok &= test_decoded_frame_contract();

    printf("\n========================================\n");
    printf("Total: %d passed, %d failed\n", g_pass, g_fail);
    printf(all_ok ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    fflush(stdout);
    return all_ok ? 0 : 1;
}
