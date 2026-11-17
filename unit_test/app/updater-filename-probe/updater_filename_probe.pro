# 更新包文件名净化回归(L8):sanitized_download_file_name 须把 URL 纯文件名
# 净化后落盘;Windows 保留设备名(CON/NUL/COM1 等,大小写不敏感)一律前缀 '_'
# 改名。另覆盖 L8 原有回归:路径穿越、非法字符、空名回退 update.bin。
QT += core gui widgets network
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = updater_filename_probe
TEMPLATE = app

REPO = $$PWD/../../..
include($$REPO/src/updater/updater.pri)
INCLUDEPATH += $$REPO/src/common $$REPO/src/updater

SOURCES += updater_filename_probe.cpp \
           $$REPO/src/common/i18n.cpp
