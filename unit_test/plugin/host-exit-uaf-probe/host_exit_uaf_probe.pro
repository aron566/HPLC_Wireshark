# 插件宿主退出/崩溃时 QProcess 释放后使用(UAF)回归探针:
# PluginManager 在 QProcess::finished/errorOccurred 的回调链里(on_process_* → disable_plugin
# → shutdown_runtime)同步 delete 该 QProcess ⇒ 信号发射返回后 Qt 继续访问已释放对象。
# 必须以 ASan+UBSan 构建运行(build.sh 已加 CONFIG+=sanitizer sanitize_address sanitize_undefined),
# 并配 ASAN_OPTIONS=max_free_fill_size=...:free_fill_byte=...,让未插桩的 Qt 代码读到被填充的
# 已释放内存而必然崩溃(见 README)。
QT += core gui widgets network
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = host_exit_uaf_probe
TEMPLATE = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/app $$REPO/src/plugin $$REPO/src/common $$REPO/src/protocol $$REPO/src/protocol/common
include($$REPO/src/plugin/plugin.pri)

SOURCES += host_exit_uaf_probe.cpp \
    $$REPO/src/protocol/protocolfactory.cpp
HEADERS += $$REPO/src/protocol/protocolfactory.h
