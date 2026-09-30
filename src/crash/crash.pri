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
# 第三方依赖构建:已绑定进 qmake 流程,无需手动执行。
# 开启任一后端即把"先编依赖"作为前置条件(见下 crash_ensure_deps):
# 缺哨兵头文件时在 qmake 阶段同步执行 3rdparty/build_crash_deps.sh
# [sentry|crashpad|all](默认 all;需联网 + CMake,首次约数分钟),
# 成功后复验哨兵,失败则 error 中断构建。
# 手动构建仍可用: 3rdparty/build_crash_deps.sh [sentry|crashpad|all]
# (CI 即显式先构建再 qmake;哨兵已存在时自动构建直接跳过,零开销)
#
# 模块内文件:
#   - crash_handler.h/.cpp   唯一对外接口 + 后端分发
#   - crash_backend.h         后端内部接口
#   - crash_util.h/.cpp       exe 目录/建目录小工具
#   - backend_sentry.cpp      sentry-native 后端
#   - backend_crashpad.cpp    crashpad 原生后端
#   - backend_stub.cpp        未编译后端的空实现

CRASH_PWD = $$PWD
CRASH_3RDPARTY = $$PWD/../../3rdparty

# crash_ensure_deps(backend, sentinel): 把"先编依赖"绑定为本模块的前置顺序。
# backend: sentry | crashpad ; sentinel: 依赖构建完成的哨兵头文件。
# 哨兵缺失时在 qmake 阶段同步构建依赖,失败则 error 中断(有牙齿,非摆设)。
# 平台策略:
#   unix      直接用 bash;cmake 用 which 找(找不到则报错指引安装)
#   win32-g++ 用 Git for Windows 自带的 bash(PATH 扫描 + 默认安装路径);
#             cmake 找 PATH 或 Qt 自带的 C:/Qt/Tools/CMake_64/bin,
#             mingw32-make 找 PATH;cmake/make 以绝对路径经 CMAKE_BIN/MAKE_BIN
#             环境变量传给脚本(不用 PATH:bash 的 PATH 是 : 分隔,盘符路径会被拆错)
#   任一找不到都 error,指引安装 Git for Windows / CMake,或手动执行脚本
#   win32-msvc 脚本仅支持 MinGW,直接 error 指引
defineTest(crash_ensure_deps) {
    backend = $$1
    sentinel = $$2
    !exists($$sentinel) {
        # 注:提示信息用英文,qmake message()/error() 输出 UTF-8,在中文 Windows 的
        # Qt Creator 输出面板会被当成 GBK 显示为乱码。
        message("crash: $$backend dependency missing, auto-building at qmake time (needs network + CMake, several minutes on first run)...")
        CRASH_SCRIPT = $$CRASH_3RDPARTY/build_crash_deps.sh

        # --- 找 bash ---
        # Windows:扫 qmake 进程的 PATH(按 ; 切分,避开 where 多行输出的换行解析坑),
        # 再加 Git for Windows 默认安装路径
        win32 {
            win32-msvc*: error("crash: auto-build of third-party deps supports only the MinGW toolchain on Windows (script uses MinGW Makefiles); switch to a MinGW kit or run manually: bash 3rdparty/build_crash_deps.sh $$backend")
            CRASH_WINPATH = $$(PATH)
            CRASH_BASH_DIRS = $$split(CRASH_WINPATH, ;)
            CRASH_BASH_DIRS += "C:/Program Files/Git/bin" "C:/Program Files (x86)/Git/bin"
            # 必须是 Git for Windows/MSYS 的 bash:PATH 靠前的 C:\Windows\System32\bash.exe
            # 是 WSL 存根,会把命令丢进 WSL Linux 里跑,Windows 路径全部失效。用
            # msys-2.0.dll 做标记识别真正的 MSYS bash(Git 的 bin 或 usr/bin 布局)。
            for(d, CRASH_BASH_DIRS) {
                isEmpty(CRASH_BASH): exists($$d/bash.exe): exists($$d/msys-2.0.dll): CRASH_BASH = $$d/bash.exe
                isEmpty(CRASH_BASH): exists($$d/bash.exe): exists($$d/../usr/bin/msys-2.0.dll): CRASH_BASH = $$d/bash.exe
            }
            isEmpty(CRASH_BASH): error("crash: auto-build needs bash; on Windows install Git for Windows (includes Git Bash), or run manually: bash 3rdparty/build_crash_deps.sh $$backend")
        } else {
            CRASH_BASH = $$system(which bash 2>/dev/null)
            isEmpty(CRASH_BASH): error("crash: auto-build needs bash (not found on PATH), or run manually: bash 3rdparty/build_crash_deps.sh $$backend")
        }

        # --- 找 cmake(脚本 configure/build 需要) ---
        win32 {
            CRASH_CMAKE_DIRS = $$split(CRASH_WINPATH, ;)
            CRASH_CMAKE_DIRS += "C:/Qt/Tools/CMake_64/bin"
            for(d, CRASH_CMAKE_DIRS) {
                isEmpty(CRASH_CMAKE): exists($$d/cmake.exe): CRASH_CMAKE = $$d/cmake.exe
            }
        } else {
            CRASH_CMAKE = $$system(which cmake 2>/dev/null)
        }
        isEmpty(CRASH_CMAKE): error("crash: auto-build needs CMake (not found on PATH); on Windows install the Qt CMake component (C:/Qt/Tools/CMake_64/bin), or run manually: bash 3rdparty/build_crash_deps.sh $$backend")

        # --- 构建工具以绝对路径经环境变量传给脚本 ---
        # (不用 PATH 传递:Windows 下 bash 的 PATH 是 : 分隔,盘符路径 C:/... 会被拆错;
        # git/curl/unzip 随 Git Bash 自带,无需处理)
        win32 {
            CRASH_MAKE_DIRS = $$split(CRASH_WINPATH, ;)
            for(d, CRASH_MAKE_DIRS) {
                isEmpty(CRASH_MAKE): exists($$d/mingw32-make.exe): CRASH_MAKE = $$d/mingw32-make.exe
            }
            isEmpty(CRASH_MAKE): error("crash: auto-build needs mingw32-make (ships with the MinGW kit, not found on PATH), or run manually: bash 3rdparty/build_crash_deps.sh $$backend")
            # 诊断行:远端排查时一眼看到 qmake 实际用的工具路径
            message("crash: auto-build tools: bash=$$CRASH_BASH cmake=$$CRASH_CMAKE make=$$CRASH_MAKE")
            CRASH_BUILD_CMD = "$$CRASH_BASH" -c "CMAKE_BIN='$$CRASH_CMAKE' MAKE_BIN='$$CRASH_MAKE' exec '$$CRASH_SCRIPT' $$backend"
        } else {
            CRASH_BUILD_CMD = "CMAKE_BIN='$$CRASH_CMAKE'" "$$CRASH_BASH" "$$CRASH_SCRIPT" $$backend
        }

        system($$CRASH_BUILD_CMD) {
            message("crash: $$backend dependency build command finished, verifying artifacts...")
        } else {
            error("crash: auto-build of $$backend dependency failed (see log above); or run manually: bash 3rdparty/build_crash_deps.sh $$backend")
        }
        !exists($$sentinel): error("crash: build command returned success but $$sentinel still not found, check the log above")
        message("crash: $$backend dependency ready")
    }
}

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
