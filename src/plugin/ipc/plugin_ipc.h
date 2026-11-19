/// @file plugin_ipc.h
/// @brief 插件 IPC 协议定义(主程序 ↔ bplc-plugin-host)
/// @details 传输:QLocalSocket;帧格式:[quint32 总长][Protobuf Envelope];Envelope 内载荷暂保留 QDataStream 域对象布局
///          QDataStream 版本固定为 Qt_6_5,保证主程序与 host 一致。
#ifndef BPLC_PLUGIN_IPC_H
#define BPLC_PLUGIN_IPC_H

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <QString>
#include <QtGlobal>
#include "plugin_wire.pb.h"

namespace plugin_ipc {

using Envelope = ::bplc::plugin::ipc::Envelope;

/// @brief 插件 API 版本(主程序与 host 握手时校验,不一致则拒绝)
constexpr int kApiVersion = 1;

/// @brief IPC Protobuf Envelope / 载荷格式版本。
/// @details 与 kApiVersion 分开:kApiVersion 是插件清单声明的 API 级别;本项
///          标识 Protobuf 信封及其承载的 app↔host 消息体布局。版本不匹配
///          即拒绝连接,不允许新旧消息体静默错位。
///          v1:旧布局(MsduInfo 无 route_hops_left、ParseResult 无 protocol)
///          v2:MsduInfo 增 route_hops_left、ParseResult 增 protocol
///          v3:三线合流并集,MpduInfo 增 pb_num_field(GW 线载波块数/无线大小码)
///          v4:MpduInfo 增 beacon_rsv(信标保留原始值)
///          v5:MpduInfo 增 pblen(南网无线载荷PB大小索引)
///          v6:MpduInfo 增 mcs(无线 MCS)
///          v7:ParseResult 增 dissect_fields(插件通用字段树);MpduInfo 删死字段
///             pb_index/coord_option/ack_search_freq
constexpr int kWireFormatVersion = 8;

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

/// @brief 单个长度前缀帧的 Protobuf Envelope 上限,超过视为协议错误
/// @details 帧头长度来自对端进程,不可信。上限 1 MB:覆盖解析结果与常规尺寸的图形帧。
constexpr qint64 kMaxIpcFrame = 1 * 1024 * 1024;

/// @brief try_take_frame 的结果
/// @brief 将消息类型和兼容载荷编码为 Protobuf Envelope。
inline QByteArray encode_envelope(quint8 type, const QByteArray& payload,
                                  QString* error = nullptr) {
    Envelope envelope;
    envelope.set_wire_format_version(kWireFormatVersion);
    envelope.set_message_type(type);
    envelope.set_payload(payload.constData(), size_t(payload.size()));
    std::string encoded;
    if (!envelope.SerializeToString(&encoded)) {
        if (error) *error = QStringLiteral("protobuf envelope serialization failed");
        return {};
    }
    return QByteArray(encoded.data(), int(encoded.size()));
}

/// @brief 解码并严格校验 Protobuf Envelope 的版本和消息类型。
inline bool decode_envelope(const QByteArray& encoded, quint8* type,
                            QByteArray* payload, QString* error = nullptr) {
    Envelope envelope;
    if (!envelope.ParseFromArray(encoded.constData(), encoded.size())) {
        if (error) *error = QStringLiteral("invalid protobuf envelope");
        return false;
    }
    if (envelope.wire_format_version() != uint32_t(kWireFormatVersion)) {
        if (error) *error = QStringLiteral("wire_format_version mismatch");
        return false;
    }
    if (envelope.message_type() > 0xFFu) {
        if (error) *error = QStringLiteral("message_type out of range");
        return false;
    }
    if (type) *type = quint8(envelope.message_type());
    if (payload) *payload = QByteArray(envelope.payload().data(),
                                       int(envelope.payload().size()));
    return true;
}

/// @brief 编码一个带长度头的 IPC 帧。
inline QByteArray encode_frame(quint8 type, const QByteArray& payload,
                               QString* error = nullptr) {
    const QByteArray body = encode_envelope(type, payload, error);
    if (body.isEmpty() || body.size() + 4 > kMaxIpcFrame + 4) return {};
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << quint32(body.size());
    frame.append(body);
    return frame;
}

/// @brief 将完整长度前缀帧解码为消息类型与消息载荷。
inline bool decode_frame(const QByteArray& frame, quint8* type, QByteArray* payload,
                         QString* error = nullptr) {
    return decode_envelope(frame, type, payload, error);
}

enum class FrameTake {
    Ok,        ///< 取出一帧,out 为 [type][payload]
    NeedMore,  ///< 数据不足,等待更多字节
    Bad,       ///< 帧长为 0 或超过 kMaxIpcFrame:调用方必须断开连接
};

/// @brief 从接收缓冲取出一帧 [quint32 len][Protobuf Envelope]
/// @details 主程序与 host 共用。长度统一按 qint64 比较,避免 4 + len 回绕。
/// @param buf 接收缓冲,成功时移除已取出的字节
/// @param out 输出帧(不含长度头)
/// @return 见 FrameTake; out 为 Envelope 序列化字节,随后由 decode_frame 解码
inline FrameTake try_take_frame(QByteArray& buf, QByteArray& out) {
    if (buf.size() < 4) return FrameTake::NeedMore;
    const auto* p = reinterpret_cast<const uchar*>(buf.constData());
    const qint64 len = (qint64(p[0]) << 24) | (qint64(p[1]) << 16) |
                       (qint64(p[2]) << 8) | qint64(p[3]);   // QDataStream 大端
    if (len < 1 || len > kMaxIpcFrame) return FrameTake::Bad;
    if (qint64(buf.size()) < 4 + len) return FrameTake::NeedMore;
    out = buf.mid(4, qsizetype(len));
    buf.remove(0, qsizetype(4 + len));
    return FrameTake::Ok;
}

/// @brief 本机 socket 名(主程序按插件 id + 进程号 + 序号生成唯一名)
/// @details 只带 plugin_id + PID 时,同一插件在"旧 host 尚未退出、新 host 已启动"的
///          重启窗口内会算出同一个名字:start_plugin 先 removeServer 再 listen,
///          旧 host 仍连着旧 server,新 host 连到新 server,两边同名 → 旧连接被
///          误判/新连接被拒。加一个单调递增的序号,每次 start_plugin 都得到新名字,
///          重启窗口内新旧 socket 互不覆盖。
/// @param plugin_id 插件 id
/// @param seq 本次启动的序号(PluginManager 单调递增;0 = 兼容旧调用)
inline QString socket_name(const QString& plugin_id, quint64 seq = 0) {
    return QStringLiteral("bplc_plugin_%1_%2_%3")
        .arg(plugin_id)
        .arg(QCoreApplication::applicationPid())
        .arg(seq);
}

} // namespace plugin_ipc

#endif // BPLC_PLUGIN_IPC_H
