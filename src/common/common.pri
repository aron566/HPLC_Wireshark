# common.pri - 公共数据结构 + 中/英显示翻译
#
# 模块内文件:
#   - bplcframe.h    帧/元信息/条目定义
#   - ringbuffer.h   字节环形缓冲 + 帧队列(备用)
#   - i18n.h/.cpp    运行时中文/英文显示翻译(L()/register_en)
#
# 依赖:QtCore(QByteArray/QString/QDateTime)

HEADERS += \
    $$PWD/bplcframe.h \
    $$PWD/ringbuffer.h \
    $$PWD/i18n.h

SOURCES += \
    $$PWD/i18n.cpp

INCLUDEPATH += $$PWD
