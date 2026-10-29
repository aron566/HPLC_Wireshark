# nw2026-ul-probe（原名 frame_probe/nw2026_ul_probe）

NW_2026 UL-OFDMA 三件套探针：trigger + UL 数据 + SACK。

## 构建与运行

```
./build.sh
./build/probe/nw2026_ul_probe <trigger> <ul> <sack>
```

三个输入都在 `../../frames/frame-probe/`：
`nw2026_trigger.bin`（32B）、`nw2026_ul_ofdma_data.bin`（296B）、
`nw2026_dl_sack.bin`（32B）。实测 trigger 与 UL 数据均 accept=1，
exit 0。
