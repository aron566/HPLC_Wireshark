# topo-nid-probe（原名 topo_nid_probe）

NID 下拉 `refresh_nids` / `set_current_nid` 逻辑的
**独立 Qt 复刻**（只用 QComboBox，不链接仓库源码）。

## 构建与运行

```
./build.sh
./build/probe/nid_probe
```

预期：`RESULT: ALL PASS`，exit 0。

## 注意

本探针是逻辑复刻而非仓库源码直测；其与当前
`set_current_nid` 实现的保真度此前曾被标记“验证前须先同步改探针”
（复刻的是旧顺序）。全过只说明复刻体自身一致，
不等于仓库实现已验证——复核仓库实现时以仓库源码为准。
