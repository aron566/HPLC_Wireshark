# nextgen-probe-verify（原名 nextgen_probe_verify）

与 nextgen-probe 相同的批量跑器（`main.cpp` 内容相同），
区别只在 `.pro`（3735 vs 3873 字节，链接/包含列表不同）。

## 构建与运行

```
./build.sh
./build/probe/nextgen_probe <nw2021|nw2026|gw2022|gw2026> <抓包文件> [detail]
```

输入必须是 **0x3C…0x3E 容器格式**抓包。注意：
`topo-route-verify/user_frame_container.bin` 是 reader 容器格式
（[长度][NTB][媒介头]，不是 0x3C 串口格式），喂给本探针会得到
`total=0`——那是输入格式不匹配，不是探针失败。
