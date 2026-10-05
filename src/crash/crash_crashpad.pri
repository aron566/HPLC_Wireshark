# crash_crashpad.pri - crashpad 原生后端构建配置
#
# 前置: 3rdparty/build_crash_deps.sh crashpad
# 产物: 3rdparty/install/crashpad/{include,lib,bin}
#
# 定义 CRASH_HAVE_CRASHPAD,链接 crashpad_client 等静态库。
# crashpad_handler 二进制需随程序发布(放 exe 同目录)。

CRASH_CRASHPAD_ROOT = $$PWD/../../3rdparty/install/crashpad

# 本后端开启即绑定前置顺序:缺依赖时在 qmake 阶段自动构建,失败则中断构建
crash_ensure_deps(crashpad, $$CRASH_CRASHPAD_ROOT/include/crashpad/client/crashpad_client.h)

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

# crashpad_handler 二进制复制到 exe 输出目录(运行时 backend_crashpad 用 exe_dir()
# 找同目录的 crashpad_handler,缺了崩溃不落 dump)。仅当后端启用且 handler 存在时复制。
# 平台/工具链区分复制命令:
#   win32-msvc*(nmake,cmd shell) → copy /Y
#   win32-g++(mingw32-make,sh shell) / unix(Linux) → cp -f
CRASH_HANDLER_BIN = $$CRASH_CRASHPAD_ROOT/bin/crashpad_handler
win32: CRASH_HANDLER_BIN = $$CRASH_CRASHPAD_ROOT/bin/crashpad_handler.exe
exists($$CRASH_HANDLER_BIN) {
    # 目标目录 = OUT_PWD + DESTDIR。DESTDIR 在 qmake 求值 .pri 时尚未按
    # debug/release 赋值(qmake 生成 Makefile 时才赋 debug/ 或空),故用 Makefile
    # 变量 $(DESTDIR) 运行时展开,避免 Debug 构建把 handler 复制到 OUT_PWD 根。
    win32-msvc* {
        QMAKE_POST_LINK += $$quote(copy /Y $$shell_path($$CRASH_HANDLER_BIN) $$shell_path($$OUT_PWD)/$(DESTDIR))
    } else {
        QMAKE_POST_LINK += $$quote(cp -f $$shell_path($$CRASH_HANDLER_BIN) $$shell_path($$OUT_PWD)/$(DESTDIR))
    }
}
