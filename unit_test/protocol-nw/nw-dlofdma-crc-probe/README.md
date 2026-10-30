# nw-dlofdma-crc-probe

NW_2026 DL-OFDMA 站点 1+ 非聚合分支 PB CRC 门回归用例。

## 测什么

载波 SOF version=2、DL-OFDMA（类型 0）2 站点合成帧，每站 1 块 264B
（TMI 9），站点 PB 体为 NW AssocReq MSDU：

- 好 CRC：站点 0 对（TEI 27）与站点 1 对（TEI 42）都学到；
- 站点 1 块 CRC 翻转：站点 0 对仍学到，站点 1 对不得学到
  （坏 CRC 的 PB 不进学习解析）。

对应修复：`fix/nw-dlofdma-pbcrc`（9832d1b），已合入
`fix/remove-parse-filter`（11afa31）。未修复代码上翻转用例失败
（站点 1 对仍被学到），即缺陷复现。

## 构建与运行

```
./build.sh
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=$HOME/workspace/build_deps/libfix:$HOME/qt/6.11.2/gcc_64/lib ./build/nw_dlofdma_crc_probe/nw_dlofdma_crc_probe
```

无输入文件（全合成帧）。预期：`RESULT: ALL PASS`，exit 0。

## 说明

- 站点 0 的对走帧级 `msdu_src` 字段，不进 `tei_mac_pairs`；
  站点 1 的对走 `tei_mac_pairs` 合并，探针断言据此区分。
- 聚合分支不受此门影响：组装器已剔除与坏块相交的级联记录。
