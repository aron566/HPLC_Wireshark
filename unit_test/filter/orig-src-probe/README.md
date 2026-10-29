# orig-src-probe

聚合/多站点帧 Orig Src 多源列探针。

## 构建与运行

```
./build.sh
./build/orig_src_probe ../../protocol-gw/gw-topo-merge-probe/gw_topo_merge.bin \
                       ../../protocol-gw/gw-topo-merge-probe/gw_topo_merge_multi.bin
```

输入复用 gw-topo-merge-probe 的构造帧（不复制文件）：
- `gw_topo_merge.bin`（3 帧，每帧单源 [1:CCO]）
- `gw_topo_merge_multi.bin`（1 帧，两站上行、源 TEI 41/42 不同）
模型/过滤检查使用探针内合成条目。

## 检查内容

- 解析不变量：GW 多站点帧合并后的 `orig_srcs` 等于**写死的期望序列**
  （不反推）——单源帧期望 `[1:CCO_MAC]`，多源帧期望
  `[41:...EE29, 42:...EE2A]`，验证「该收集却没收集」的场景。
- 模型列：多源显示 `STA-11 [aa:bb:cc:dd:ee:0b],STA-12 [01:02:03:04:05:06]`
 （逗号分隔、重复源去重）；单帧回退不变；拒收帧为空；TEI 1 显示 CCO。
 前提条件：仅当帧级无原始 TEI 时才用多源；帧级已有源时只显示单源，
 且过滤时不含各段/各站源的词。
- 过滤：`sta-12`/`sta-11` 命中（search_text 整词匹配，非 hop TEI）；
  `sta-1` 不误命中 `sta-11`；`sta-99` 不命中；MAC 模式 `01:02`/`aa:bb`
  经映射表展开后命中；子串 `dd:ee:0` 命中；单帧帧级源 `sta-5` 可搜。

## 预期结果

`RESULT: ALL PASS`，exit 0。
