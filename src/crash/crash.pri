# crash.pri - 崩溃转储模块总入口
#
# 与业务代码解耦:业务侧只在 main.cpp 调 CrashHandler::install() 一行。
# 本模块无 Qt 依赖(纯 C++11),便于移植。
#
# 编译开关(传给 qmake 的 CONFIG):
#   CONFIG += crash_sentry    编译 sentry-native 后端(需 3rdparty/install/sentry)
#   CONFIG += crash_crashpad  编译 crashpad 原生后端(需 3rdparty/install/crashpad)
#   两个开关独立,可单独开、同时开、都不开。
#   都不开:CrashHandler::install() 返回空,业务代码无需改动。
# BPLC_STA_Monitor.pro 默认 CONFIG += crash_crashpad(见该文件注释)。
#
# 第三方依赖构建(需联网,CMake):
#   3rdparty/build_crash_deps.sh [sentry|crashpad|all]
#
# 模块内文件:
#   - crash_handler.h/.cpp   唯一对外接口 + 后端分发
#   - crash_backend.h         后端内部接口
#   - crash_util.h/.cpp       exe 目录/建目录小工具
#   - backend_sentry.cpp      sentry-native 后端
#   - backend_crashpad.cpp    crashpad 原生后端
#   - backend_stub.cpp        未编译后端的空实现

CRASH_PWD = $$PWD

HEADERS += \
    $$CRASH_PWD/crash_handler.h

SOURCES += \
    $$CRASH_PWD/crash_handler.cpp \
    $$CRASH_PWD/crash_util.cpp \
    $$CRASH_PWD/backend_stub.cpp

INCLUDEPATH += $$CRASH_PWD

contains(CONFIG, crash_sentry) {
    include($$CRASH_PWD/crash_sentry.pri)
}

contains(CONFIG, crash_crashpad) {
    include($$CRASH_PWD/crash_crashpad.pri)
}
