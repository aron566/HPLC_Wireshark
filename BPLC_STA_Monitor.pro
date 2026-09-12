QT       += core gui serialport widgets
CONFIG   += c++17 \
           windows

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = BPLC_STA_Monitor
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

# Windows 资源:exe 文件图标 + 程序版本信息
RC_ICONS = icons/app.ico
VERSION = 1.0.16
QMAKE_TARGET_PRODUCT = "BPLC STA Monitor"
QMAKE_TARGET_DESCRIPTION = "BPLC/HRF protocol STA frame monitor"
QMAKE_TARGET_COPYRIGHT = "Copyright (c) 2026 aron566"
RESOURCES += BPLC_STA_Monitor.qrc

# Qt 6.10 win32 GUI app 默认自动链接 libQt6EntryPoint,该库要求 qMain()
# 所以主函数用 qMain(名字)(没有 Qt 宏展开,真是函数符号名)

# 模块依赖顺序(从底层到顶层):
#   updater  <- 无依赖(第三方自包含,放在最前)
#   common  <- protocol, io
#   io      <- app
#   ui      <- app
#   protocol <- app
include(src/updater/updater.pri)
include(src/common/common.pri)
include(src/protocol/protocol.pri)
include(src/io/io.pri)
include(src/ui/ui.pri)
include(src/app/app.pri)

# main.cpp 在工程根目录,不在任何模块里
SOURCES += main.cpp
HEADERS +=
TARGET = BPLC_STA_Monitor  # 重置(模块里设置过)

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
else: target.path = $$[QT_INSTALL_BINS]/$${TARGET}

!isEmpty(target.path): INSTALLS += target
