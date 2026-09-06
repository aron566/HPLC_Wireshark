/// @file rawframeimporter.cpp
/// @brief 裸 hex 文本导入工具
#include "rawframeimporter.h"
#include <QFile>

RawFrameImporter::RawFrameImporter(QObject* parent) : QObject(parent) {}

int RawFrameImporter::import_text(const QString& path, QByteArray& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    out = f.readAll();
    f.close();
    return out.size();
}
