# nw2026-frame-test

NW_2026 帧解析探针（容器 + isRF 标志）。

## 构建与运行

```
./build.sh
./build/probe/frame_test <container> <isRF 0|1>
```

本用例无自带输入；可用
`../../topo/topo-route-verify/user_frame_container.bin`
（reader 容器格式）。实测该容器 isRF=0 时 accept=1
（聚合帧 3 子帧），exit 0。
