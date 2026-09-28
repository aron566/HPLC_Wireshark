# crash_crashpad.pri - crashpad 原生后端构建配置
#
# 前置: 3rdparty/build_crash_deps.sh crashpad
# 产物: 3rdparty/install/crashpad/{include,lib,bin}
#
# 定义 CRASH_HAVE_CRASHPAD,链接 crashpad_client 等静态库。
# crashpad_handler 二进制需随程序发布(放 exe 同目录)。

CRASH_CRASHPAD_ROOT = $$PWD/../../3rdparty/install/crashpad

!exists($$CRASH_CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h) {
    error("crash_crashpad: 找不到 crashpad 头文件,请先跑 3rdparty/build_crash_deps.sh crashpad")
}

DEFINES += CRASH_HAVE_CRASHPAD

SOURCES += \
    $$PWD/backend_crashpad.cpp

INCLUDEPATH += $$CRASH_CRASHPAD_ROOT/include
# crashpad 头文件内部按 "util/..." "client/..." 互相引用
INCLUDEPATH += $$CRASH_CRASHPAD_ROOT/include/crashpad
# mini_chromium 的 base/ 头文件(crashpad 代码按 "base/..." 引用)
INCLUDEPATH += $$CRASH_CRASHPAD_ROOT/include/crashpad/mini_chromium

# 静态链接 crashpad_client 及其依赖(顺序重要)
# install/crashpad/lib 下实际产物:
#   crashpad_{client,compat,handler_lib,minidump,mpack,snapshot,tools,util}, mini_chromium
unix {
    LIBS += -L$$CRASH_CRASHPAD_ROOT/lib
    LIBS += -lcrashpad_client -lcrashpad_compat -lcrashpad_handler_lib -lcrashpad_minidump
    LIBS += -lcrashpad_mpack -lcrashpad_snapshot -lcrashpad_tools -lcrashpad_util
    LIBS += -lmini_chromium
    LIBS += -lcurl -lz -ldl -lpthread
}
win32 {
    LIBS += -L$$CRASH_CRASHPAD_ROOT/lib
    LIBS += -lcrashpad_client -lcrashpad_compat -lcrashpad_handler_lib -lcrashpad_minidump
    LIBS += -lcrashpad_mpack -lcrashpad_snapshot -lcrashpad_tools -lcrashpad_util
    LIBS += -lmini_chromium
    LIBS += -lwinhttp -ldbghelp -lversion -lws2_32
}
