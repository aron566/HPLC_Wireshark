# Downloader 重命名失败回归(M6):QFile::rename 失败必须发 downloadFailed、
# 不得发 downloadFinished、不得进 installUpdate。本地 HTTP 服务器提供小文件;
# 目标路径预先建同名目录使重命名失败。
QT += core gui widgets network
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = downloader_rename_probe
TEMPLATE = app

REPO = $$PWD/../../..
include($$REPO/src/updater/updater.pri)
INCLUDEPATH += $$REPO/src/common $$REPO/src/updater

SOURCES += downloader_rename_probe.cpp \
           $$REPO/src/common/i18n.cpp
