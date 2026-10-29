# coord-probe（原名 coord_probe）

NW 协调（coord）字段探针。

## 状态：构建失败（已记录，未改源码）

探针读 `MpduInfo.coord_option`，但当前仓库
`src/common/bplcframe.h` 该字段已为 `coord_duration`
（“协调时隙占用时长 16-bit(ms)”）。属探针与仓库 API 漂移：
改探针源码可恢复构建，但归集任务不改探针逻辑，故原样记录，
待另行修复流程处理。

## 原运行形式（恢复构建后）

```
./build.sh
./build/probe/probe_coord <capture.txt>
```

输入为外部 capture 文本文件（未入库）。
