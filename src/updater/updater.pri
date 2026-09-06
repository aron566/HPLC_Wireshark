# updater.pri — 自包含第三方更新检查模块(QSimpleUpdater,上游源码原样)
#
# 来源:  https://github.com/alex-spataru/QSimpleUpdater  (MIT License)
# 说明:  Qt6 兼容的"检查更新"库:内置 JSON 更新清单解析 + 下载器 +
#        AuthenticateDialog/Downloader 两个 .ui 对话框。文件保持上游原貌,
#        不做本地风格改造;许可副本见本目录 LICENSE.md。
#
# 用法:  #include "QSimpleUpdater.h"
#        updater()->setModuleVersion("app", appVersion);
#        updater()->setUpdateURL("app", "https://your-host/update.json");
#        updater()->checkForUpdates();
#        更新清单 JSON 格式参见上游 README(按平台分节的 "updates" 对象)。

QT += network

DEFINES += QSU_INCLUDE_MOC=1
INCLUDEPATH += $$PWD/include

SOURCES += \
    $$PWD/QSimpleUpdater.cpp \
    $$PWD/Updater.cpp \
    $$PWD/Downloader.cpp \
    $$PWD/AuthenticateDialog.cpp

HEADERS += \
    $$PWD/include/QSimpleUpdater.h \
    $$PWD/Updater.h \
    $$PWD/Downloader.h \
    $$PWD/AuthenticateDialog.h

FORMS += \
    $$PWD/Downloader.ui \
    $$PWD/AuthenticateDialog.ui

RESOURCES += $$PWD/resources/qsimpleupdater.qrc
