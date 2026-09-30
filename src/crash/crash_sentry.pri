# crash_sentry.pri - sentry-native 后端构建配置
#
# 前置: 3rdparty/build_crash_deps.sh sentry
# 产物: 3rdparty/install/sentry/{include,lib,bin}
#
# 定义 CRASH_HAVE_SENTRY,链接 libsentry 静态库。
# crashpad_handler 二进制需随程序发布(放 exe 同目录),见下 INSTALLS。

CRASH_SENTRY_ROOT = $$PWD/../../3rdparty/install/sentry

# 本后端开启即绑定前置顺序:缺依赖时在 qmake 阶段自动构建,失败则中断构建
crash_ensure_deps(sentry, $$CRASH_SENTRY_ROOT/include/sentry.h)

DEFINES += CRASH_HAVE_SENTRY

# Windows 上 sentry.h 默认按 DLL 导入(__declspec(dllimport))声明 API,
# 而我们按 STATIC 编译,需定义 SENTRY_BUILD_STATIC 否则链接报
# undefined reference to __imp_sentry_*(CI #33 实测)
win32: DEFINES += SENTRY_BUILD_STATIC

SOURCES += \
    $$PWD/backend_sentry.cpp

INCLUDEPATH += $$CRASH_SENTRY_ROOT/include

# 静态链接 sentry 及其 crashpad 依赖(顺序重要)
# install/sentry/lib 下实际产物:
#   sentry, crashpad_{client,compat,handler_lib,minidump,mpack,snapshot,tools,util},
#   mini_chromium, unwind
unix: LIBS += -L$$CRASH_SENTRY_ROOT/lib
unix: LIBS += -lsentry
unix: LIBS += -lcrashpad_client -lcrashpad_compat -lcrashpad_handler_lib -lcrashpad_minidump
unix: LIBS += -lcrashpad_mpack -lcrashpad_snapshot -lcrashpad_tools -lcrashpad_util
unix: LIBS += -lmini_chromium -lunwind
win32 {
    LIBS += -L$$CRASH_SENTRY_ROOT/lib
    LIBS += -lsentry
    LIBS += -lcrashpad_client -lcrashpad_compat -lcrashpad_handler_lib -lcrashpad_minidump
    LIBS += -lcrashpad_mpack -lcrashpad_snapshot -lcrashpad_tools -lcrashpad_util
    LIBS += -lmini_chromium
    # Windows 下 sentry 静态库依赖
    # (-lsynchronization: sentry 用到 WaitOnAddress/WakeByAddressSingle,CI #34 实测)
    LIBS += -lwinhttp -ldbghelp -lversion -lsynchronization
}
# sentry 的 curl transport(Linux)及通用系统库
unix: LIBS += -lcurl -lz -ldl -lpthread

# crashpad_handler 随程序安装/发布
CRASH_SENTRY_HANDLER_SRC = $$CRASH_SENTRY_ROOT/bin/crashpad_handler
win32: CRASH_SENTRY_HANDLER_SRC = $$CRASH_SENTRY_ROOT/bin/crashpad_handler.exe
!exists($$CRASH_SENTRY_HANDLER_SRC) {
    warning("crash_sentry: crashpad_handler not found, runtime crash capture will be unavailable")
}
