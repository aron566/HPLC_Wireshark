/// @file rawframeimporter.h
/// @brief 裸 hex 文本导入工具
#ifndef RAWFRAMEIMPORTER_H
#define RAWFRAMEIMPORTER_H

#include <QObject>
#include "serialreader.h"

class RawFrameImporter : public QObject {
    Q_OBJECT
public:
    explicit RawFrameImporter(QObject* parent = nullptr);
    static int import_text(const QString& path, QByteArray& out);
};

#endif // RAWFRAMEIMPORTER_H
