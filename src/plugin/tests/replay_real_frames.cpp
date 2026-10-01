/// @file replay_real_frames.cpp
/// @brief 用真实抓包文件回灌测试 4 个插件
/// @details 用法: replay_real_frames --examples <dir> --bin <file> [--max N]
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QFile>
#include <QElapsedTimer>
#include <cstdio>

#include "plugin_backend.h"
#include "js_backend.h"
#include "lua_backend.h"
#include "../plugin_api/plugin_manifest.h"

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

QList<QByteArray> extract_frames(const QByteArray& data, int max_n) {
    QList<QByteArray> frames;
    int i = 0;
    while (i < data.size() && frames.size() < max_n) {
        if ((unsigned char)data[i] == 0x3C) {
            int j = data.indexOf('\x3e', i + 1);
            if (j > 0 && j - i < 2048) {
                frames.append(data.mid(i, j - i + 1));
                i = j + 1;
                continue;
            }
        }
        ++i;
    }
    return frames;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCommandLineParser p;
    p.addOption(QCommandLineOption(QStringList() << "examples", "examples dir", "dir"));
    p.addOption(QCommandLineOption(QStringList() << "bin", "bin file", "file"));
    p.addOption(QCommandLineOption(QStringList() << "max", "max frames", "n", "5000"));
    p.process(app);

    QString ex_dir = p.value("examples");
    QString bin_file = p.value("bin");
    int max_n = p.value("max").toInt();

    QFile f(bin_file);
    if (!f.open(QIODevice::ReadOnly)) {
        printf("FAIL: cannot open %s\n", qPrintable(bin_file));
        return 1;
    }
    QByteArray data = f.readAll();
    f.close();
    printf("bin 大小: %lld 字节\n", (long long)data.size());

    QList<QByteArray> frames = extract_frames(data, max_n);
    printf("提取 %d 帧用于回灌\n\n", frames.size());

    QStringList plugins = {"js_topo", "lua_diag", "lua_report"};
    bool all_ok = true;

    for (const QString& pname : plugins) {
        printf("=== %s ===\n", qPrintable(pname));
        QString err;
        PluginManifest m = read_plugin_manifest(ex_dir + "/" + pname);
        if (!m.error.isEmpty()) {
            printf("  [FAIL] manifest: %s\n", qPrintable(m.error));
            all_ok = false;
            continue;
        }
        IPluginBackend* b = create_backend(m, &err);
        if (!b) {
            printf("  [FAIL] backend: %s\n", qPrintable(err));
            all_ok = false;
            continue;
        }

        QElapsedTimer t;
        t.start();
        int accept = 0, reject = 0;
        QString last_summary;
        for (int i = 0; i < frames.size(); ++i) {
            BplcFrame fr;
            fr.data = frames[i];
            fr.arrival_us = i * 1000LL;
            fr.arrival_ms = 1700000000000LL;
            MsduState msdu;
            ParseFilter filter;
            QString perr;
            ParseResult r = b->parse(fr, msdu, filter, &perr);
            if (perr.isEmpty()) { ++accept; last_summary = r.msdu.summary; }
            else { ++reject; }
        }
        qint64 ms = t.elapsed();
        printf("  解析: %d 帧, 接受 %d, 拒绝 %d, 耗时 %lld ms (%.1f 帧/秒)\n",
               frames.size(), accept, reject, ms,
               ms > 0 ? frames.size() * 1000.0 / ms : 0);
        if (!last_summary.isEmpty())
            printf("  最后摘要: %s\n", qPrintable(last_summary.left(80)));

        // 插件特有检查
        if (pname == "js_topo") {
            if (b->has_graphics()) {
                QString err;
                QImage img = b->render_graphics(400, 300, &err);
                printf("  拓扑渲染: %dx%d %s\n", img.width(), img.height(),
                       img.isNull() ? "[FAIL]" : "[PASS]");
                if (img.isNull()) all_ok = false;
            }
        } else {
            printf("  [PASS] 解析完成 (成功 %d / 失败 %d)\n", accept, reject);
        }
        delete b;
        printf("\n");
    }

    printf("========================================\n");
    printf(all_ok ? "回灌测试全部通过\n" : "回灌测试有失败项\n");
    return all_ok ? 0 : 1;
}
