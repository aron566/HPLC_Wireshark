# nw-rf-harness-mg（原名 nw_rf_harness_mg）

NW RF/PLC SOF harness（_mg 变体）。

## _mg 陷阱（必读）

本目录的 `harness.pro` 编译的是**原版**
`../nw-rf-harness/main.cpp`，本目录自己的 `main.cpp`
**从不被编译**。改探针断言必须改原版 `main.cpp`。

## 构建与运行

```
./build.sh
./build/harness/nw_rf_harness
```

预期：`SYNTH RESULT: PASS`，exit 0。
