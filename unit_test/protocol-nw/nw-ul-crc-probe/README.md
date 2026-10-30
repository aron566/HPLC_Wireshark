# nw-ul-crc-probe

NW_2026 UL-OFDMA 类型 1 逐站 CRC 进 `mpdu.pb_crc_oks` 回归用例。

## 测什么

载波 SOF version=2、多站合成帧（用户 2026-10-30 定：逐站 CRC 进
`mpdu.pb_crc_oks`，且该字段只针对 PB 块 CRC）：

- 无 trigger 时 UL 帧：无站点、`pb_crc_oks` 为空（不凭空造项）；
- trigger（类型 2，SPLC 第 4 部分 表 28）后 UL 全好：`pb_crc_oks == [true, true]`
 （站序即块序）；
- UL 站点 0 / 站点 1 / 双站块 CRC 翻转：对应位 false，其余 true；
- UL 站点 1 聚合块翻转：同样 `[true, false]`（聚合路径亦进帧级表）；
- DL-SACK（类型 1，单 16B PB）：单站 TEI 33、`pb_crc_oks == [true]`。

类型 3（UL-SACK）不在此：其无站点条目，无 PB 块 CRC 可进表（另案）。

## 运行

```sh
./build.sh
./build/nw_ul_crc_probe/nw_ul_crc_probe
```

期望 `RESULT: ALL PASS`，exit 0。
