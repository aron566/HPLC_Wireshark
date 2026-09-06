/// @file theme.h
/// @brief 界面主题:深色(QDarkStyleSheet,见 qdarkstyle/ 目录,MIT)+ 浅色(内置)
/// @details 主题应用为全局 QApplication 样式表,切换即时生效。
///          主题值存 config.ini [general] theme = dark(默认)/light。
#ifndef THEME_H
#define THEME_H

#include <QString>

namespace theme {

/// @brief 应用主题到整个应用
/// @param name "dark" 或 "light"
void apply(const QString& name);

}  // namespace theme

#endif // THEME_H
