# gw-multi-crc-probe

GW_2026 OFDMA/TDA 多站逐站 CRC 进 `mpdu.pb_crc_oks` 回归用例。

## 测什么

直接调用 `gw_2026_sof::assemble()`，载波 SOF（表 7 帧子类型 2=OFDMA /
3=TDA）2 站合成帧，每站 1 块 136B（分集拷贝模式 2，HDC2.0 4-1
物理层 表 18），GW 布局头 1B + 体 + CRC24 3B（用户 2026-10-30 定：
逐站 CRC 进 `mpdu.pb_crc_oks`，且该字段只针对 PB 块 CRC）：

- OFDMA 全好：`pb_crc_oks == [true, true]`；
- OFDMA 翻转站 0 / 站 1 / 双站：对应位 false，其余 true；
- TDA 全好：`[true, true]`；TDA 翻转站 1：`[true, false]`；
- 多站汇总 `pb_num` 为 0（不参与均一组装）。

## 运行

```sh
./build.sh
./build/gw_multi_crc_probe/gw_multi_crc_probe
```

期望 `RESULT: ALL PASS`，exit 0。
