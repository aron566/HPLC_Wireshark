# gw-topo-merge-probe（原名 gw_topo_merge_probe）

多站点 GW 帧的 topo 事件合并语义探针。

## 构建与运行

```
./build.sh
./build/gw_topo_merge_probe gw_topo_merge.bin build/topo_merge_out.txt
```

输入 `gw_topo_merge.bin`（927B，构造帧）已随用例入库；
再生脚本 `gw_topo_merge_craft.py`（输出已改为脚本所在目录）。
脚本另生成 `gw_topo_merge_multi.bin`（1 帧，两站上行源不同），
供 `filter/orig-src-probe` 测 orig_srcs 多源收集分支用，本探针不读。

## 预期结果

`AGG frames=3 PASS=3 FAIL=0`，exit 0。每帧 6 项检查：
`topo_first_non_other`（首个非 Other 事件获胜）、`pairs_full_merge`
（tei_mac_pairs 全并）、`present_false`、`summary_empty`、
`extras_in_order`（后续事件按序进 `topo_extra_events`）、
`topo_state_nodes`（首事件 + extras 逐个 apply 后 TopoState 含全部站点）。

## 注意

本用例原记录**修复前**行为（后续事件丢弃）；`fix/topo-events-list`
分支已将其断言更新为 Shape J 新语义：首事件检查不变，新增 extras
顺序检查与 TopoState 全站点检查。预期 extras：F1=[AssocCnf TEI12]、
F2=[GatherInd]、F3=空。
