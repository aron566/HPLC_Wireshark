# Protocol Buffers integration

The plugin IPC schema and generated C++ files live in `src/plugin/ipc/`.
This directory provides the project's independent qmake integration file,
`protobuf.pri`, used by the application, plugin host, and IPC integration test.

## Vendored runtime (protobuf v36.2 + abseil-cpp 20250512.1, static)

The runtime is built from source, not taken from the system package:

| Submodule | Upstream | Version | Pinned commit |
|---|---|---|---|
| `3rdparty/protobuf-src` | https://github.com/protocolbuffers/protobuf.git | v36.2 | `2c74169b34066ceb8ddb6b882fcb3fb32d737a55` |
| `3rdparty/abseil-cpp` | https://github.com/abseil/abseil-cpp.git | 20250512.1 | `76bb24329e8bf5f39704eb10d21b9a80befa7c81` |

The submodules track the `36.x` / `20250512.1` branches in `.gitmodules`
(intent only). The pinned commits above are what actually builds; never run
`git submodule update --remote` — it would silently move the pins.

Note: `3rdparty/protobuf/` itself stays a parent-repo directory (this file
plus `protobuf.pri`); the upstream protobuf source lives in the sibling
`3rdparty/protobuf-src/` because a submodule path cannot hold parent-tracked
files.

## How to build

```sh
git submodule update --init --depth 1 3rdparty/protobuf-src 3rdparty/abseil-cpp
bash 3rdparty/build_protobuf.sh
```

The script builds abseil first, then protobuf (static `libprotobuf.a` +
`protoc`), and installs both under `3rdparty/install/{protobuf,absl}`.
It is idempotent: when the sentinels already exist it exits immediately.
A fresh build takes about 17 minutes on a 2-core machine with ~1 GB free
memory (absl ~2 min, protobuf ~15 min); set `JOBS` to raise parallelism on
bigger machines. Build trees live in `3rdparty/build/` (git-ignored,
deletable). The script never downloads dependencies: it passes
`-Dprotobuf_LOCAL_DEPENDENCIES_ONLY=ON`, so a missing local abseil fails the
CMake configure step loudly instead of fetching from GitHub.

Ordinary qmake builds need no manual step: `protobuf.pri` checks the
sentinels and runs the script automatically when they are missing, failing
with `error()` if the build fails.

## Regenerating the checked-in message code

Generated code is checked in so ordinary builds do not need `protoc`.
Regenerate only with the vendored `protoc` (the previously checked-in code
was produced by protoc 3.12.4 and no longer compiles against the v36.2
headers):

```sh
./3rdparty/install/protobuf/bin/protoc -Isrc/plugin/ipc --cpp_out=src/plugin/ipc src/plugin/ipc/plugin_wire.proto
```

The `.proto` itself is unchanged by this; only `plugin_wire.pb.{h,cc}` move
(the 3.12 -> 36 codegen change rewrites most of both files).

## Escape hatch: PROTOBUF_ROOT

Set the `PROTOBUF_ROOT` environment/qmake value to use an external protobuf
install prefix instead of the vendored one (old behaviour: `-lprotobuf`).
Do not mix: protoc 3.21 generates code that does not compile against the
checked-in v36.2 generated code, and vice versa.

## ABI pin: PROTOBUF_MESSAGE_GLOBALS_TEMPORARY_OPTOUT

v36.2 switches the per-message table ABI (`PROTOBUF_MESSAGE_GLOBALS`) on
`ABSL_HAVE_ADDRESS_SANITIZER`, which is a per-translation-unit macro (set
iff that TU is compiled with `-fsanitize=address`). The same static
`libprotobuf.a` is linked into both plain targets and ASan targets (e.g.
`host-exit-uaf-probe`): if only the ASan side switched ABI, the two sides
would disagree on the parse-table layout and any parse/serialize would
SEGV inside `MergeFromImpl` / `CheckHasBitConsistency` (verified: even a
valid Envelope or empty input crashes under ASan, while plain builds are
fine). `protobuf.pri` therefore defines
`PROTOBUF_MESSAGE_GLOBALS_TEMPORARY_OPTOUT` for every consumer, pinning
the old ABI on both sides (the library itself is built without sanitizers,
so it is already on the old ABI). Re-check that this opt-out macro still
exists when upgrading protobuf (it is named TEMPORARY upstream).

## Explicit upgrades

Upgrades are a deliberate human action, never automatic:

1. Move each submodule to the new commit and verify the SHAs.
2. Regenerate `plugin_wire.pb.{h,cc}` with the new vendored `protoc`.
3. Run the local verification (host build + IPC probes + i18n checks).
4. Land it as its own PR.
