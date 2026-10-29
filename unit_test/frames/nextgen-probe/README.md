# nextgen-probe（原名 nextgen_probe）

跨四协议批量跑器：对抓包逐帧跑指定解析器，
统计接受/拒绝原因/帧型/MSDU/PB-CRC。

## 构建与运行

```
./build.sh
./build/probe/nextgen_probe <nw2021|nw2026|gw2022|gw2026> <抓包文件> [detail]
```

输入是 **0x3C…0x3E 容器格式**（0x3D 转义 `0xFF-b`）的抓包文件；
本用例无自带输入，可用 `../frame-probe/nw_frame_20261022.bin`
（单帧容器）。`detail` 为 N 时打印前 N 帧明细。

## 预期结果

单帧容器 nw2021：`total=1 accepted=1 sof=1 pb_crc_ok=1`，exit 0。
