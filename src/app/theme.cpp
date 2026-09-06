/// @file theme.cpp
/// @brief 界面主题实现:主题 = 基础风格(Fusion)+ 配套 QPalette + (深色) QSS
/// @details 控件底色统一由 QPalette 提供,不再在各处/各控件零散写死:
///          - 深色:QDarkStyleSheet 资源(darkstyle.qrc -> ":/qdarkstyle/dark/darkstyle.qss")
///            配暗色调色板,漏网控件(视口/滚动条/未覆盖子类)回落暗色而非白底;
///          - 浅色:浅色调色板(不再用 QSS 把 QTableView 等写死成 white);
///          - auto:跟随 Windows 深浅色(Qt6.5+ QStyleHints::colorScheme)。
///          QDarkStyleSheet 上游文件保持原样(MIT,LICENSE.rst),修正以追加覆盖段实现。
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

/// @brief 深色 QSS 本地追加修正(置于上游样式之后):
///        下拉弹层选项加内边距与最小行高,outline 清除焦点虚线残留
const char kDarkExtraQss[] =
    "\n/* local fixes (BPLC monitor) */\n"
    "QComboBox QAbstractItemView {\n"
    "  outline: 0;\n"
    "  padding: 2px;\n"
    "}\n"
    "QComboBox QAbstractItemView::item {\n"
    "  min-height: 1.4em;\n"
    "  padding: 3px 6px;\n"
    "}\n";

/// @brief 浅色调色板(对应旧浅色 QSS 观感,但作用于所有控件的统一底色)
QPalette light_palette() {
    const QColor window(0xf0, 0xf0, 0xf0);
    const QColor text(0x20, 0x20, 0x20);
    QPalette p;
    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            Qt::white);
    p.setColor(QPalette::AlternateBase,   QColor(0xf7, 0xf7, 0xf7));
    p.setColor(QPalette::ToolTipBase,     Qt::white);
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          window);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Highlight,       QColor(0x3d, 0x6f, 0x9f));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link,            QColor(0x3d, 0x6f, 0x9f));
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x90, 0x90, 0x90));
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x90, 0x90, 0x90));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x90, 0x90, 0x90));
    return p;
}

/// @brief 暗色调色板(与 QDarkStyleSheet qss 同色系 #19232D,兜底其未覆盖的控件)
QPalette dark_palette() {
    const QColor window(0x19, 0x23, 0x2D);   // #19232D 同 qss QWidget 背景
    const QColor button(0x37, 0x41, 0x4F);   // #37414F 同 qss 按钮面
    const QColor borderish(0x45, 0x53, 0x64); // #455364
    const QColor text(0xDF, 0xE1, 0xE2);     // #DFE1E2 同 qss 前景
    const QColor disabled(0x78, 0x8D, 0x9C); // #788D9C
    const QColor highlight(0x34, 0x67, 0x92);// #346792
    QPalette p;
    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            window);
    p.setColor(QPalette::AlternateBase,   window);
    p.setColor(QPalette::ToolTipBase,     window);
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          button);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, text);
    p.setColor(QPalette::Link,            QColor(0x1A, 0x72, 0xBB));
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Mid,             borderish);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text,       disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight,  disabled);
    return p;
}

/// @brief 读深色 QSS(资源)并追加本地修正,失败返回空
QString dark_qss() {
    QFile f(QStringLiteral(":/qdarkstyle/dark/darkstyle.qss"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()) + QLatin1String(kDarkExtraQss);
}

}  // namespace

namespace theme {

/// @brief 当前系统深浅色(dark/light);QStyleHints 不可用时回落 light
QString resolve_auto() {
    if (QGuiApplication::styleHints() &&
        QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
        return QStringLiteral("dark");
    return QStringLiteral("light");
}

void apply(const QString& name) {
    const QString n = (name == QLatin1String("auto")) ? resolve_auto() : name;

    QApplication* app = qApp;
    // 统一 Fusion 基础风格(深/浅均按 Fusion 设计,保证两态一致)
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (n == QLatin1String("light")) {
        // 浅色:仅调色板(统一底色,无全局 QSS,避免控件白底写死/漏网深色花脸)
        app->setPalette(light_palette());
        app->setStyleSheet(QString());
        return;
    }
    // 深色:QDarkStyleSheet + 暗色调色板(兜底 qss 未覆盖的控件底色)
    app->setPalette(dark_palette());
    const QString qss = dark_qss();
    if (qss.isEmpty()) {
        app->setStyleSheet(QString());   // 资源缺失:仅 palette 兜底
        return;
    }
    app->setStyleSheet(qss);
}

}  // namespace theme
