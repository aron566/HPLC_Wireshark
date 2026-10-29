# frame-probe-variant3（原名 frame_probe/variant3）

NW_2021 MSDU 变体探针（`nw_2021_msdu_parser.h`）。

## 构建与运行

```
./build.sh
./build/probe/nw_msdu_probe <帧.bin>
```

帧输入见 `../../frames/frame-probe/`（如 `nw_frame_20261022.bin`，
0x3C…0x3E 容器格式）。实测该帧 present=1，exit 0。
