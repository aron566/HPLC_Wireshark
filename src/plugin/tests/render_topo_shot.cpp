/// @file render_topo_shot.cpp
/// @brief 回灌真实帧后渲染拓扑截图
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QImage>
#include <cstdio>

#include "plugin_backend.h"
#include "js_backend.h"
#include "../plugin_api/plugin_manifest.h"

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
    p.addOption(QCommandLineOption(QStringList() << "out", "output png", "png"));
    p.process(app);

    QFile f(p.value("bin"));
    if (!f.open(QIODevice::ReadOnly)) return 1;
    QByteArray data = f.readAll();
    f.close();
    QList<QByteArray> frames = extract_frames(data, p.value("max").toInt());
    printf("回灌 %d 帧...\n", frames.size());

    PluginManifest m = read_plugin_manifest(p.value("examples") + "/js_topo");
    if (!m.error.isEmpty()) { printf("manifest: %s\n", qPrintable(m.error)); return 1; }
    QString err;
    JsBackend b;
    if (!b.initialize(m, &err)) { printf("init: %s\n", qPrintable(err)); return 1; }

    for (int i = 0; i < frames.size(); ++i) {
        BplcFrame fr;
        fr.data = frames[i];
        fr.arrival_us = i * 1000LL;
        MsduState msdu;
        ParseFilter filter;
        QString perr;
        b.parse(fr, msdu, filter, &perr);
    }
    printf("解析完成，渲染拓扑...\n");

    QString rerr;
    QImage img = b.render_graphics(800, 600, &rerr);
    if (img.isNull()) { printf("render: %s\n", qPrintable(rerr)); return 1; }
    QString out = p.value("out");
    if (img.save(out)) printf("已保存: %s (%dx%d)\n", qPrintable(out), img.width(), img.height());
    else { printf("保存失败\n"); return 1; }
    return 0;
}
