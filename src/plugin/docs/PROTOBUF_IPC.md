# 插件进程 IPC 的 Protobuf 封套

当前插件架构为主程序分别连接各自的 `bplc-plugin-host`；插件之间没有
直接 socket、地址发现或消息路由。本次改造只覆盖既有主程序↔宿主链路，
不引入插件间通信 API。

## 线格式

仍使用本机 `QLocalSocket` 和 4 字节大端长度前缀。长度后的完整帧现为
`src/plugin/ipc/plugin_wire.proto` 中的 `bplc.plugin.ipc.Envelope`：

| 字段 | 含义 |
|---|---|
| `wire_format_version` | IPC 格式版本；当前为 8，与握手严格校验 |
| `message_type` | `plugin_ipc::MsgType` 的数值，沿用既有类型编号 |
| `payload` | 当前各消息的消息体字节 |

Envelope 由 `plugin_ipc::encode_frame` / `decode_frame` 统一编码和校验。
非法 Protobuf、未知格式版本或超出消息类型范围的帧会按协议错误断开，
既有长度上限仍用于限制不可信对端输入。

## 消息体与兼容性

此次迁移将消息类型、传输版本及封帧从“长度 + 类型字节”替换为 Protobuf
Envelope；既有 `plugin_serialization` 中复杂的帧、解析树、状态和 `QImage`
消息体仍由项目现有 Qt 编解码器承载在 Envelope `payload` 中。这样不改变
插件后端/解析 API，且保留已有字段树深度与容器反序列化防护。消息体后续
逐项迁移到具名 Protobuf message 时，应继续复用稳定的 `message_type` 编号，
使用新增字段号，提升 IPC 格式版本，并更新 IPC 集成测试。

格式握手仍拒绝不匹配的主程序/宿主版本，不承诺跨版本兼容；旧版本二进制
需与同版本程序配套。插件清单的 `api_version` 不变。

## 构建

`3rdparty/protobuf/protobuf.pri` 独立负责 Protobuf C++ runtime 的 include/link
设置；应用、独立宿主和 IPC 集成测试都引用该 pri。runtime 来自源码编库
（vendored protobuf v36.2 + abseil-cpp 20250512.1，静态链接，两个子库
各钉一个 commit，见 `3rdparty/protobuf/README.md`）：

```sh
git submodule update --init --depth 1 3rdparty/protobuf-src 3rdparty/abseil-cpp
bash 3rdparty/build_protobuf.sh
```

产物装到 `3rdparty/install/{protobuf,absl}`。普通构建不需要手动跑脚本：
`protobuf.pri` 会在哨兵缺失时在 qmake 期同步构建，失败则 `error()` 中断。
生成的 C++ 文件已纳入仓库，普通构建不需要 `protoc`；重生成必须用子库
编出的 `protoc`（签入代码是 protoc 36.2 产的，3.12/3.21 的生成代码与
v36.2 头文件不兼容）：

```sh
./3rdparty/install/protobuf/bin/protoc -Isrc/plugin/ipc --cpp_out=src/plugin/ipc src/plugin/ipc/plugin_wire.proto
```

如需用外部安装前缀代替 vendored 库，设置 `PROTOBUF_ROOT` 指向其安装
前缀（不要混用不同大版本的 protoc 生成代码与运行库）。
子库升级是显式的人工动作，永不自动拉最新（流程见
`3rdparty/protobuf/README.md`）。Windows 侧 MinGW 一次编过尚未实测，
见该 README 与本仓库的待核实清单。
