# Protocol Buffers C++ runtime integration for qmake targets.
#
# Vendored protobuf v36.2 + abseil-cpp 20250512.1（静态，子库入库）:
#   3rdparty/protobuf-src  protocolbuffers/protobuf v36.2
#                          commit 2c74169b34066ceb8ddb6b882fcb3fb32d737a55，跟踪 36.x 分支
#   3rdparty/abseil-cpp   abseil/abseil-cpp 20250512.1
#                          commit 76bb24329e8bf5f39704eb10d21b9a80befa7c81
# 产物：3rdparty/install/{protobuf,absl}，由 3rdparty/build_protobuf.sh 构建；
# 缺失时 qmake 期同步构建（仿 src/crash/crash.pri 的 crash_ensure_deps），失败则 error()。
# 仍认 PROTOBUF_ROOT（显式指定时走老路：外部安装前缀 + -lprotobuf）；缺省走 vendored。
# 注意：3rdparty/protobuf/ 是父仓的集成目录（本文件 + README.md），
# 上游源码子库挂在 3rdparty/protobuf-src/，避免与父仓跟踪文件同路径冲突。
#
# ABI 钉死：v36.2 的 port_def.inc 按逐编译单元的 ABSL_HAVE_ADDRESS_SANITIZER
# 开关 PROTOBUF_MESSAGE_GLOBALS（消息表换新 ABI）。本仓同一份静态库要同时
# 链进普通目标与 -fsanitize=address 目标（如 host-exit-uaf-probe）：若只让
# ASan 侧开新 ABI，两边表布局不一致，任意 Parse/Serialize 都在 MergeFromImpl
# 或 CheckHasBitConsistency 里 SEGV（实测：空输入/合法 Envelope 也崩；普通
# 构建正常）。故在此强制旧 ABI：上游预留的 opt-out 宏，两边（库侧无 ASan
# 本来就是旧 ABI、消费侧由此宏钉死）一致才安全。v36.2 有效，升级 protobuf
# 时须复核该宏是否还在。
DEFINES += PROTOBUF_MESSAGE_GLOBALS_TEMPORARY_OPTOUT
PROTOBUF_ROOT_ENV = $$(PROTOBUF_ROOT)
isEmpty(PROTOBUF_ROOT): PROTOBUF_ROOT = $$PROTOBUF_ROOT_ENV

PB_PWD = $$PWD
PB_3RDPARTY = $$PWD/..

isEmpty(PROTOBUF_ROOT) {
    PB_ROOT = $$PB_3RDPARTY/install/protobuf
    ABSL_ROOT = $$PB_3RDPARTY/install/absl

    # pb_ensure_deps(): 把“先编依赖”绑定为本模块的前置顺序。
    # 哨兵缺失时在 qmake 阶段同步构建依赖，失败则 error 中断构建（有牙齿，非摆设）。
    # 平台策略（照抄 crash_ensure_deps）:
    #   unix      直接用 bash；cmake 用 which 找（找不到则报错指引安装）
    #   win32-g++ 用 Git for Windows 自带的 bash（PATH 扫描 + 默认安装路径）；
    #             cmake 找 PATH 或 Qt 自带的 C:/Qt/Tools/CMake_64/bin，
    #             mingw32-make 找 PATH；cmake/make 以绝对路径经 CMAKE_BIN/MAKE_BIN
    #             环境变量传给脚本（不用 PATH：bash 的 PATH 是 : 分隔，盘符路径会被拆错）
    #   任一找不到都 error，指引安装 Git for Windows / CMake，或手动执行脚本
    #   win32-msvc 脚本仅支持 MinGW，直接 error 指引
    defineTest(pb_ensure_deps) {
        # 四个哨兵全齐才算就绪（只查一个头文件时，中断的构建留下的半成品
        # install/ 会骗过检查、到链接才报看不懂的错；VM 替换杀构建是真事）。
        PB_SENTINEL = $$PB_ROOT/include/google/protobuf/port_def.inc
        PB_SENTINEL2 = $$PB_ROOT/lib/libprotobuf.a
        PB_SENTINEL3 = $$PB_ROOT/bin/protoc
        PB_SENTINEL4 = $$ABSL_ROOT/lib/cmake/absl/abslConfig.cmake
        !exists($$PB_SENTINEL)|!exists($$PB_SENTINEL2)|!exists($$PB_SENTINEL3)|!exists($$PB_SENTINEL4) {
            # 注：提示信息用英文，qmake message()/error() 输出 UTF-8，在中文 Windows 的
            # Qt Creator 输出面板会被当成 GBK 显示为乱码。
            message("protobuf: vendored dependency missing, auto-building at qmake time (needs CMake, several minutes on first run)...")
            PB_SCRIPT = $$PB_3RDPARTY/build_protobuf.sh

            # --- 找 bash ---
            # Windows：扫 qmake 进程的 PATH（按 ; 切分，避开 where 多行输出的换行解析坑），
            # 再加 Git for Windows 默认安装路径
            win32 {
                win32-msvc*: error("protobuf: auto-build of vendored protobuf supports only the MinGW toolchain on Windows (script uses MinGW Makefiles); switch to a MinGW kit or run manually: bash 3rdparty/build_protobuf.sh")
                PB_WINPATH = $$(PATH)
                PB_BASH_DIRS = $$split(PB_WINPATH, ;)
                PB_BASH_DIRS += "C:/Program Files/Git/bin" "C:/Program Files (x86)/Git/bin"
                # 必须是 Git for Windows/MSYS 的 bash：PATH 靠前的 C:\Windows\System32\bash.exe
                # 是 WSL 存根，会把命令丢进 WSL Linux 里跑，Windows 路径全部失效。用
                # msys-2.0.dll 做标记识别真正的 MSYS bash（Git 的 bin 或 usr/bin 布局）。
                for(d, PB_BASH_DIRS) {
                    isEmpty(PB_BASH): exists($$d/bash.exe): exists($$d/msys-2.0.dll): PB_BASH = $$d/bash.exe
                    isEmpty(PB_BASH): exists($$d/bash.exe): exists($$d/../usr/bin/msys-2.0.dll): PB_BASH = $$d/bash.exe
                }
                isEmpty(PB_BASH): error("protobuf: auto-build needs bash; on Windows install Git for Windows (includes Git Bash), or run manually: bash 3rdparty/build_protobuf.sh")
            } else {
                PB_BASH = $$system(which bash 2>/dev/null)
                isEmpty(PB_BASH): error("protobuf: auto-build needs bash (not found on PATH), or run manually: bash 3rdparty/build_protobuf.sh")
            }

            # --- 找 cmake（脚本 configure/build 需要）---
            win32 {
                PB_CMAKE_DIRS = $$split(PB_WINPATH, ;)
                PB_CMAKE_DIRS += "C:/Qt/Tools/CMake_64/bin"
                for(d, PB_CMAKE_DIRS) {
                    isEmpty(PB_CMAKE): exists($$d/cmake.exe): PB_CMAKE = $$d/cmake.exe
                }
            } else {
                PB_CMAKE = $$system(which cmake 2>/dev/null)
            }
            isEmpty(PB_CMAKE): error("protobuf: auto-build needs CMake (not found on PATH); on Windows install the Qt CMake component (C:/Qt/Tools/CMake_64/bin), or run manually: bash 3rdparty/build_protobuf.sh")

            # --- 构建工具以绝对路径经环境变量传给脚本 ---
            # （不用 PATH 传递：Windows 下 bash 的 PATH 是 : 分隔，盘符路径 C:/... 会被拆错；
            # git/curl/unzip 随 Git Bash 自带，无需处理）
            win32 {
                PB_MAKE_DIRS = $$split(PB_WINPATH, ;)
                for(d, PB_MAKE_DIRS) {
                    isEmpty(PB_MAKE): exists($$d/mingw32-make.exe): PB_MAKE = $$d/mingw32-make.exe
                }
                isEmpty(PB_MAKE): error("protobuf: auto-build needs mingw32-make (ships with the MinGW kit, not found on PATH), or run manually: bash 3rdparty/build_protobuf.sh")
                # 诊断行：远端排查时一眼看到 qmake 实际用的工具路径
                message("protobuf: auto-build tools: bash=$$PB_BASH cmake=$$PB_CMAKE make=$$PB_MAKE")
                # cmd /c 解析带空格的 exe 路径会拆错（加引号也救不了，见 cmd 旧引号规则），
                # 所以 exe 位置只放裸 bash（无空格），其目录先塞进子进程 PATH 再调。
                # 另：qmake 会把 "..." 和 '...' 都当成分组语法吃掉，字面引号只能用裸 \" /
                # \' 经变量展开拼入（展开得到的引号不会被二次解析）。
                PB_DQ = \"
                PB_SQ = \'
                PB_BASH_DIR = $$dirname(PB_BASH)
                PB_BUILD_CMD = set PATH=$$PB_BASH_DIR;%PATH% && bash -c $${PB_DQ}CMAKE_BIN=$${PB_SQ}$$PB_CMAKE$${PB_SQ} MAKE_BIN=$${PB_SQ}$$PB_MAKE$${PB_SQ} exec $${PB_SQ}$$PB_SCRIPT$${PB_SQ}$${PB_DQ}
            } else {
                PB_BUILD_CMD = "CMAKE_BIN='$$PB_CMAKE'" "$$PB_BASH" "$$PB_SCRIPT"
            }

            system($$PB_BUILD_CMD) {
                message("protobuf: vendored dependency build command finished, verifying artifacts...")
            } else {
                error("protobuf: auto-build of vendored protobuf failed (see log above); or run manually: bash 3rdparty/build_protobuf.sh")
            }
            !exists($$PB_SENTINEL)|!exists($$PB_SENTINEL2)|!exists($$PB_SENTINEL3)|!exists($$PB_SENTINEL4): error("protobuf: build command returned success but a sentinel is still not found, check the log above")
            message("protobuf: vendored dependency ready")
        }
    }
    pb_ensure_deps()

    INCLUDEPATH += $$PB_ROOT/include $$ABSL_ROOT/include
    DEPENDPATH += $$PB_ROOT/include $$ABSL_ROOT/include

    # 链接闭包（Linux 实测结论）：
    #   libprotobuf.a 依赖约 35 个 absl 目标 + libutf8_validity.a
    #   （protobuf-targets.cmake 的 INTERFACE_LINK_LIBRARIES；libupb 只被 libprotoc
    #   需要，我们不链它）。静态库单遍链接有顺序要求：absl glob 必须包在
    #   --start-group/--end-group 里，否则报大量 undefined reference。
    #   Windows 分支（--start-group 在 MinGW ld 下是否同样有效、系统库是否只需
    #   -ladvapi32）未经实测，待 U2/U3 补验。
    LIBS += -Wl,--start-group
    LIBS += -L$$PB_ROOT/lib -lprotobuf
    LIBS += $$files($$ABSL_ROOT/lib/libabsl_*.a)
    LIBS += -L$$PB_ROOT/lib -lutf8_validity
    LIBS += -Wl,--end-group
    unix: LIBS += -lpthread
    win32: LIBS += -ladvapi32

    # protoc 优先用子库编出的；普通构建不需要 protoc（生成代码已签入）。
    PROTOC = $$PB_ROOT/bin/protoc
    !exists($$PROTOC): PROTOC = $$[QT_HOST_BINS]/protoc
    !exists($$PROTOC): PROTOC = protoc
} else {
    # 外部前缀模式：沿用老逻辑（-lprotobuf），absl 由外部前缀自带。
    # 注意：系统 libprotobuf 3.21 没有 absl 依赖；3.21 的 protoc 生成代码
    # 与签入的 v36.2 生成代码不兼容，不要混用。
    INCLUDEPATH += $$PROTOBUF_ROOT/include
    DEPENDPATH += $$PROTOBUF_ROOT/include
    LIBS += -L$$PROTOBUF_ROOT/lib -lprotobuf
    win32: error("Protocol Buffers runtime not found. Set PROTOBUF_ROOT to its install directory.")

    PROTOC = $$[QT_HOST_BINS]/protoc
    !exists($$PROTOC): PROTOC = protoc
}

# Generated C++ is checked in; protoc is only needed when regenerating schema.
OTHER_FILES += $$PWD/README.md
