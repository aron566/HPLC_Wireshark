/// @file plugin_ipc.h
/// @brief 插件 IPC 协议定义(主程序 ↔ bplc-plugin-host)
/// @details 传输:QLocalSocket;帧格式:[quint32 总长][quint8 消息类型][QDataStream 载荷]
///          QDataStream 版本固定为 Qt_6_5,保证主程序与 host 一致。
#ifndef BPLC_PLUGIN_IPC_H
#define BPLC_PLUGIN_IPC_H

#include <QCoreApplication>
#include <QDataStream>
#include <QString>
#include <QtGlobal>

namespace plugin_ipc {

/// @brief 插件 API 版本(主程序与 host 握手时校验,不一致则拒绝)
constexpr int kApiVersion = 1;

/// @brief QDataStream 序列化版本(双方必须一致)
constexpr int kStreamVersion = QDataStream::Qt_6_5;

/// @brief IPC 消息类型
enum class MsgType : quint8 {
    Hello = 1,          ///< host→app: {api_version:int, plugin_id:QString}
    HelloAck = 2,       ///< app→host: {ok:bool, reason:QString}
    ParseRequest = 10,  ///< app→host: {seq:quint64, frame:BplcFrame, msdu:MsduState, filter:ParseFilter}
    ParseResponse = 11, ///< host→app: {seq:quint64, result:ParseResult, msdu:MsduState}
    Ping = 20,          ///< app→host: {seq:quint64} 心跳
    Pong = 21,          ///< host→app: {seq:quint64}
    Shutdown = 30,      ///< app→host: 优雅退出
    Error = 40,         ///< host→app: {seq:quint64, message:QString} 解析异常等
    // 图形插件(Phase3)
    RenderRequest = 50,  ///< app→host: {seq:quint64, width:int, height:int} 请求重绘
    RenderResponse = 51, ///< host→app: {seq:quint64, ok:bool, image:QImage} 图片(PNG 压缩字节经 QDataStream)
    GraphicsEventMsg = 52, ///< app→host: {seq:quint64, event:GraphicsEvent} 鼠标/滚轮/缩放
    EventAck = 53,       ///< host→app: {seq:quint64, needs_redraw:bool}
    RequestRedraw = 54,  ///< host→app: {} 插件主动请求重绘(如动画/数据更新)
};

/// @brief 本机 socket 名(主程序按插件 id 生成唯一名)
/// @param plugin_id 插件 id
inline QString socketName(const QString& plugin_id) {
    return QStringLiteral("bplc_plugin_%1_%2")
        .arg(plugin_id)
        .arg(QCoreApplication::applicationPid());
}

} // namespace plugin_ipc

#endif // BPLC_PLUGIN_IPC_H
