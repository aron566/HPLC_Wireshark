# learn-probe（原名 learn_probe）

NW_2026 TEI↔MAC 学习探针：信标、TEI 列表、Gather、级联、
DL/UL-OFDMA 等学习点的对账。

## 构建与运行

```
./build.sh
./build/learn_probe files crafted/*.bin
./build/learn_probe scan <length-prefixed-mpdus.bin>   # 数据集不入库
```

`crafted/` 下 5 个构造帧已入库；再生脚本 `craft_frames.py`
（输出已改为脚本所在目录的 `crafted/`）。

## 预期结果

`files` 模式 exit 0，stderr `scanned 5 frames`，打印 4 条 FRAME 行。
`craft_ul_data.bin` 不打印 FRAME 行是探针设计：
FRAME 行只打印给 interesting 帧（学到对、级联、有站点、
mme 0x0084/0x003A、或非 Other topo），UL 数据帧 5 项全无，
故无行、无对——不是失败。
