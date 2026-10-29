# frame-probe-variant（原名 frame_probe/variant）

NW_2021 帧变体探针（`nw_2021_parser.h` + `nw_2021_msdu_parser.h`）。

## 状态：构建失败（已记录，未改源码）

与 coord-probe 同因：`main.cpp:68` 读 `MpduInfo.coord_option`，
现为 `coord_duration`。原样记录，待另行修复。

## 原运行形式（恢复构建后）

```
./build.sh
./build/probe/nw_frame_probe_variant <帧.bin>
```

帧输入见 `../../frames/frame-probe/`（如 `nw_frame_20261022.bin`）。
