# nw-trunc-pbcrc-probe

NW_2026 截断帧 `pb_crc_ok` 语义回归用例（定法 a）。

## 测什么

语义：`pb_crc_ok` = 全部声明块都已收到且 CRC24 通过（不只是已收块全过）；
截断帧即使已收块全过也为 false，与红行谓词“含截断”意图一致。

构帧：SPLC 单站点载波 SOF version=2，声明 2 块 264B（TMI 9），块体为
NW AssocReq MSDU：

- 全 2 块好 CRC：`pb_crc_ok=true`，oks=[true, true]；
- 声明 2 块仅收 1 好块：`pb_crc_ok=false`，oks=[true]，摘要含“截断”；
- 声明 2 块收 0 块：`pb_crc_ok=false`，oks 为空；
- 2 块、第 2 块 CRC 翻转：`pb_crc_ok=false`，oks=[true, false]
  （旧口径亦 false，防退化）。

未修复代码上第 2、3 用例失败（截断时 `pb_crc_ok` 仍为 true）。

## 构建与运行

```
./build.sh
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=$HOME/workspace/build_deps/libfix:$HOME/qt/6.11.2/gcc_64/lib ./build/nw_trunc_pbcrc_probe/nw_trunc_pbcrc_probe
```

无输入文件（全合成帧）。预期：`RESULT: ALL PASS`，exit 0。

## 说明

- 同款口径已在 NW_2021 落地（`pb_crc_oks.size()==pb_num`），GW 侧截断直接拒收；
  本用例只覆盖 NW_2026 通用 SOF 路。
