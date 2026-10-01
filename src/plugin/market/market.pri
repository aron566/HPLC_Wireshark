# 插件市场模块:远端 feed / 下载安装 / 离线安装 / 启停 / 卸载 + VS Code 式对话框
QT += network

INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/../../../3rdparty/miniz

SOURCES += \
    $$PWD/plugin_market.cpp \
    $$PWD/market_dialog.cpp \
    $$PWD/../../../3rdparty/miniz/miniz.c \
    $$PWD/../../../3rdparty/miniz/miniz_tdef.c \
    $$PWD/../../../3rdparty/miniz/miniz_tinfl.c \
    $$PWD/../../../3rdparty/miniz/miniz_zip.c

HEADERS += \
    $$PWD/plugin_market.h \
    $$PWD/market_dialog.h
