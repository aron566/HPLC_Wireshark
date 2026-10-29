# topo-route-verify

NW_2026 拓扑路由扫描：对长度前缀 MPDU 流逐帧打印 topo 事件
（tab 分隔行 + stderr `scanned N frames`）。

## 构建与运行

```
./build.sh
./build/probe/topo_scan <mpdus.bin> [verbose_idx]
```

- 主数据集 `mpdus.bin`（8.3M，45827 帧，**未入库**），
  原位置 `~/workspace/topo-route-verify/mpdus.bin`（只读使用）。
  再生脚本 `extract.py`：输入
  `~/workspace/user/files/TOPO_SPLC_20261027_095658.bin`，
  输出 `mpdus.bin` + `frames_meta.json` 回原位置
  （输出超 1MB，路径故意未改）。
- `user_frame_container.bin` / `user_frame_mpdu.bin`（已入库）是
  frame-45558 调查的单帧附带物，也可作 `nw2026-frame-test` 的输入。
