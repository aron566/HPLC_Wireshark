# gw-topo-merge-probe（原名 gw_topo_merge_probe）

多站点 GW 帧的 topo 事件合并语义探针。

## 构建与运行

```
./build.sh
./build/gw_topo_merge_probe gw_topo_merge.bin build/topo_merge_out.txt
```

输入 `gw_topo_merge.bin`（927B，构造帧）已随用例入库；
再生脚本 `gw_topo_merge_craft.py`（输出已改为脚本所在目录）。

## 预期结果

`AGG frames=3 PASS=3 FAIL=0`，exit 0。每帧 4 项检查：
`topo_first_non_other`（首个非 Other 事件获胜）、`pairs_full_merge`
（tei_mac_pairs 全并）、`present_false`、`summary_empty`。

## 注意

本用例记录的是**修复前**行为。`fix/topo-events-list`（TopoEvent 列表化）
合入后，后续 topo 事件不再被丢弃，本用例的断言需随之更新——
届时不要直接沿用旧断言，也不要在本分支提前改探针逻辑。
