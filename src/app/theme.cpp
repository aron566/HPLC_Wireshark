/// @file theme.cpp
/// @brief 界面主题实现(深色 = QDarkStyleSheet;浅色 = 内置浅色样式)
/// @details 深色主题资源来自 qdarkstyle/ 子目录(MIT License,见 LICENSE.rst):
///          darkstyle.qrc 编译为资源,运行时读 ":/qdarkstyle/dark/darkstyle.qss"
///          全局应用。浅色主题即原 Wireshark 风格浅色样式(移自 mainwindow)。
///          支持 "auto":跟随 Windows 深浅色(Qt6.5+ QStyleHints::colorScheme)。
#include "theme.h"
#include "appconfig.h"

#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QGuiApplication>
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
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString qss = QString::fromUtf8(f.readAll());
    // 本地覆盖(追加于上游样式之后):下拉弹层选项加内边距与最小行高,
    // 避免深色下选项文字贴边/末项被裁;outline 清除焦点虚线残留。
    qss += QStringLiteral(
        "\n/* local fixes (BPLC monitor) */\n"
        "QComboBox QAbstractItemView {\n"
        "  outline: 0;\n"
        "  padding: 2px;\n"
        "}\n"
        "QComboBox QAbstractItemView::item {\n"
        "  min-height: 1.4em;\n"
        "  padding: 3px 6px;\n"
        "}\n");
    return qss;
}

}  // namespace

namespace theme {

/// @brief 当前系统深浅色(dark/light);QStyleHints 不可用(旧平台)时按默认深
QString resolve_auto() {
    if (QGuiApplication::styleHints() &&
        QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        return QStringLiteral("dark");
    return QStringLiteral("light");
}

void apply(const QString& name) {
    const QString n = (name == QLatin1String("auto")) ? resolve_auto() : name;

    QApplication* app = qApp;
    // 统一 Fusion 基础风格(深/浅 QSS 均按 Fusion 设计,保证两态一致)
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (n == QLatin1String("light")) {
        app->setStyleSheet(QString::fromUtf8(kLightQss));
        return;
    }
    // 深色(QDarkStyleSheet)
    const QString qss = dark_qss();
    if (qss.isEmpty()) {
        app->setStyleSheet(QString::fromUtf8(kLightQss));   // 资源缺失兜底
        return;
    }
    app->setStyleSheet(qss);
}

}  // namespace theme
