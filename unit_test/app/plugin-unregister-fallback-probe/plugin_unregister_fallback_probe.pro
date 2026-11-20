# 插件注销热回退探针(A-8):注销后分发器重建回退内置、下一帧正常解析;
# 注册到达时采集中不切换、空闲时切换。
# 在 FrameDispatcher 层复现 MainWindow::on_plugin_status_changed 的三个
# 分支(槽本身是 3 行分发,行为链在此验证)。对照信号:内置 GW_2022 拒收
# 坏 FCH CRC 帧(计 dropped),替身插件解析器恒接受并计数调用次数。
# 断言进退出码(失败返回非 0)。
QT       += core gui widgets serialport
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = plugin_unregister_fallback_probe
TEMPLATE  = app

REPO = $$PWD/../../..
INCLUDEPATH += $$REPO/src $$REPO/src/app $$REPO/src/common $$REPO/src/io $$REPO/src/protocol $$REPO/src/protocol/common $$REPO/3rdparty/qslog
include($$REPO/3rdparty/qslog/QsLog.pri)
include($$REPO/src/protocol/protocol.pri)

SOURCES += plugin_unregister_fallback_probe.cpp \
           $$REPO/src/app/framedispatcher.cpp \
           $$REPO/src/common/i18n.cpp
HEADERS += $$REPO/src/app/framedispatcher.h
