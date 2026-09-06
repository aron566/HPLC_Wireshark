# common.pri - 公共数据结构(无 Qt 业务逻辑)
#
# 模块内文件:
#   - bplcframe.h    帧/元信息/条目定义
#   - ringbuffer.h   字节环形缓冲 + 帧队列(备用)
#
# 依赖:QtCore(QByteArray/QString/QDateTime)

HEADERS += \
    $$PWD/bplcframe.h \
    $$PWD/ringbuffer.h

INCLUDEPATH += $$PWD
