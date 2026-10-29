# gw2026-tree-probe（原名 frame_probe/gw2026_tree_probe）

裸 MPDU -> GW_2026_Parser(from_raw) -> ProtocolTree，打印解析结果 +
字段树全节点。

## 构建（Qt 6.11.2，shadow 构建目录 ./build/，勿放 /tmp）

```
./build.sh
```

注意三点（都是实测踩过的坑）：

- 链接要 `-lGL`：VM 重置后系统 libGL 开发软链接丢失，用
  `LIBRARY_PATH=$HOME/workspace/build_deps/libfix` 兜底（含 libGL.so 软链接）。
  本用例的 `build.sh` 已设置该变量。
- QsLog 的 signal 实现靠 moc：`probe.pro` 的 HEADERS 必须列 QsLog 各头文件，
  否则报 `FunctorDestination::logMessageReady` 未定义。
- tree_add_item/tree_add_bit_field 在 protocol_tree_builder.cpp，不链它会缺符号。

## 运行（offscreen，不弹窗口）

```
QT_QPA_PLATFORM=offscreen \
LD_LIBRARY_PATH=$HOME/workspace/build_deps/libfix:$HOME/qt/6.11.2/gcc_64/lib \
./build/probe/gw2026_tree_probe <mpdu.bin>
```

输入为裸 MPDU（FCH byte0 起）；探针以 from_raw 模式喂给解析器
（行首补 1 字节 isRF=0），再按 kProtocolGw2026 建 PacketEntry 走
ProtocolTree::show_packet，与主程序同一条树路径。

## 已验帧

- `../../frames/frame-probe/gw2026_ofdma_sim.bin`（440B，OFDMA 下行 2 站）
- `../../frames/frame-probe/gw2026_tda_sim.bin`（304B，TDA 下行 2 站）
实测输出：OFDMA 构造帧 accept=1（旧输出 ofdma_out.txt、tda_out.txt
留在原目录 `~/workspace/frame_probe/gw2026_tree_probe/`，未入库）。
