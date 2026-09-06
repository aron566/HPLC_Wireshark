/// @file theme.cpp
/// @brief 界面主题实现(深色 = QDarkStyleSheet;浅色 = 内置浅色样式)
/// @details 深色主题资源来自 qdarkstyle/ 子目录(MIT License,见 LICENSE.rst):
///          darkstyle.qrc 编译为资源,运行时读 ":/qdarkstyle/dark/darkstyle.qss"
///          全局应用。浅色主题即原 Wireshark 风格浅色样式(移自 mainwindow)。
#include "theme.h"
#include "appconfig.h"

#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include <QPalette>
#include <QColor>

namespace {

const char kLightQss[] =
    // 注意:不可用裸 "QWidget" 选择器——它会命中 QMenu 等所有子类,
    // 把弹出菜单拖进 QSS 绘制路径,导致菜单项文字与快捷键列重叠。
    "QMainWindow { background-color: #f0f0f0; }\n"
    "QSplitter   { background-color: #f0f0f0; }\n"
    "QToolBar { background-color: #e8e8e8; border-bottom: 1px solid #c0c0c0; spacing: 2px; }\n"
    "QLineEdit { padding: 2px 4px; }\n"
    "QTableView { background-color: white; alternate-background-color: #f7f7f7; }\n"
    "QTreeWidget { background-color: white; alternate-background-color: #f7f7f7; }\n";

/// @brief 读深色 QSS(资源),失败返回空
QString dark_qss() {
    QFile f(QStringLiteral(":/qdarkstyle/dark/darkstyle.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(f.readAll());
    return {};
}

}  // namespace

namespace theme {

void apply(const QString& name) {
    QApplication* app = qApp;
    // 统一 Fusion 基础风格(深/浅 QSS 均按 Fusion 设计,保证两态一致)
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (name == QLatin1String("light")) {
        app->setStyleSheet(QString::fromUtf8(kLightQss));
        return;
    }
    // 默认深色(QDarkStyleSheet)
    const QString qss = dark_qss();
    if (qss.isEmpty()) {
        app->setStyleSheet(QString::fromUtf8(kLightQss));   // 资源缺失兜底
        return;
    }
    app->setStyleSheet(qss);
}

}  // namespace theme
