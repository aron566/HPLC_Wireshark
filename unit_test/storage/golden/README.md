# golden

PacketEntry 块序列化 golden：合成 1/57/250/1000 条目块，
编码后打印 `n/bytes/sha256`。

## 构建与运行

```
./build.sh
mkdir -p build/run && cd build/run && ../golden/golden
```

**不要在用例根目录运行**：程序把 `v3_n%1.bin` 写到当前目录，
在根目录运行会覆盖已入库的 golden 文件。
正确做法是在 `build/run/`（忽略目录）里跑，再比对 sha256。

## 状态：已入库 golden 已过期

再生字节与已入库文件全部不匹配
（330 vs 312、5802 vs 5514、24207 vs 22720、95505 vs 90080 字节，
约每条目 +5 字节），说明 `packetentry_serialize` 的格式自 golden
录制后已漂移。已入库文件原样保留未覆盖；是否重录 golden
需另行决定——归集时不擅自用新输出覆盖参考文件。

## 搬迁说明

`main.cpp` 的输出路径原为绝对旧路径
`/home/hatch/workspace/golden/v3_n%1.bin`，已改为 CWD 相对
`v3_n%1.bin`（纯路径字面量，编码/打印逻辑不动）。
