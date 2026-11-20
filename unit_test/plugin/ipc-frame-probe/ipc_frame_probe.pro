# IPC 帧长上限回归探针(M1):plugin_ipc::try_take_frame 对非法/超大帧长必须拒绝且不死循环
QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = ipc_frame_probe
TEMPLATE = app

# plugin_ipc.h 包含签入的 plugin_wire.pb.h，需 protobuf 头文件路径
# （main 上 plugin_ipc.h 不含此 include，缺口直到 protobuf-ipc 线才暴露）。
include(../../../3rdparty/protobuf/protobuf.pri)

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/plugin
SOURCES += ipc_frame_probe.cpp
