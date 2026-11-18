# Protocol Buffers integration

The plugin IPC schema and generated C++ files live in `src/plugin/ipc/`.
This directory provides the project's independent qmake integration file,
`protobuf.pri`, used by the application, plugin host, and IPC integration test.

The runtime is linked from the build environment (`libprotobuf`); set the
`PROTOBUF_ROOT` environment/qmake value when it is installed outside the system
prefix. Windows builds must provide a matching Protocol Buffers C++ runtime.
Regenerate the checked-in message code with:

```sh
protoc -Isrc/plugin/ipc --cpp_out=src/plugin/ipc src/plugin/ipc/plugin_wire.proto
```

Generated code is checked in so ordinary builds do not need `protoc`.
