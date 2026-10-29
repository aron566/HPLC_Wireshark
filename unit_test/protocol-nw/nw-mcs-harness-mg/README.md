# nw-mcs-harness-mg（原名 nw_mcs_harness_mg）

NW MCS 字段 harness（_mg 变体）。

## _mg 陷阱（必读）

本目录的 `harness.pro` 编译的是**原版**
`../nw-mcs-harness/main.cpp`，本目录自己的 `main.cpp`
**从不被编译**。改探针断言必须改原版 `main.cpp`。

## 构建与运行

```
./build.sh
./build/harness/nw_mcs_harness
```

预期：exit 0，打印字段树（转储型，无显式 RESULT 行）。
