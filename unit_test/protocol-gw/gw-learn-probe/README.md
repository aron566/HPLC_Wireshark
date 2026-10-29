# gw-learn-probe（原名 gw_learn_probe）

GW_2026 TEI↔MAC 学习探针：AssocCnf 体对、Gather 所列站、
OFDMA/TDA 全站、载波发现发送方对。

## 构建与运行

```
./build_probe.sh
./build/gw_learn_probe crafted.bin build/out.txt
```

- 构造输入 `crafted.bin`（1382B）已入库；再生脚本 `gw_craft.py`
  （输出已改为脚本所在目录）。
- 完整扫描需长度前缀 MPDU 数据集 `gw_mpdus.bin`（39M，**未入库**），
  原位置 `~/workspace/gw_learn_probe/gw_mpdus.bin`；
  再生脚本 `split_gw_capture.py`（输入为
  `~/workspace/your_files/BPLC_20260914_105445.bin`，输出回原位置；
  因输出超 1MB，路径故意未改，见主 README）。

## 预期结果

构造输入：exit 0，
`total=6 accepted=6 msdu_pairs=13`，
`ofdma=1 tda=1 stations=4 gather=1 assoc_cnf=2 discover=1`。
