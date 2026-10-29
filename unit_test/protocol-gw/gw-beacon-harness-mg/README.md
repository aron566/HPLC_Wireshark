# gw-beacon-harness-mg（原名 gw_beacon_harness_mg）

GW_2022 信标解析 harness（_mg 变体）。

## _mg 陷阱（必读）

本目录的 `harness.pro` 编译的是**原版**
`../gw-beacon-harness/main.cpp`，本目录自己的 `main.cpp`
**从不被编译**。改探针断言必须改原版 `main.cpp`
（或两份同步改），只改本目录版会假装修好实则没变。

## 构建与运行

```
./build.sh
./build/harness/gw_beacon_harness
```

预期：`RESULT: PASS`，exit 0。
