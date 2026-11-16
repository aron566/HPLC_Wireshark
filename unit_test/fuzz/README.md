# fuzz:帧解析入口 libFuzzer 目标(第二份审查报告 L6)

全仓此前无 fuzz 目标/语料。本目录补一个最小可用的 libFuzzer 目标,
覆盖帧解析入口三条线:`get_bits`(QByteArray 版,H1 边界检查)、
`pb_asm::assemble_pb_blocks`(国网/南网 PB 组装 + 级联拆分)、
`add_fields`(L3 截断标记分支)。

## 目标与语料

| 文件 | 说明 |
|---|---|
| `fuzz_frame_parse.cpp` | 目标源码(header-only 公共层 + `src/common/i18n.cpp`) |
| `build.sh` | 构建 + 默认 30 秒短跑;先探测 `-fsanitize=fuzzer` 是否可用 |
| `corpus/gw2026-ofdma.bin` | GW 2026 OFDMA 下行 2 站实测帧(440B,源自 `unit_test/frames/frame-probe/gw2026_ofdma_sim.bin`) |
| `corpus/gw2026-tda.bin` | GW 2026 TDA 下行 2 站实测帧(304B,源自同目录 `gw2026_tda_sim.bin`) |
| `corpus/nw2026-frame.bin` | 南网实测帧(548B,源自同目录 `nw_frame_20261021.bin`) |
| `corpus/tiny.bin` | 16 字节全零短输入,探截断分支 |

输入即原始帧字节,头 3 字节兼作组装参数(布局选择/PB 数/PB 大小),
其后字节驱动随机 `get_bits` 三元组。单次执行工作量有界
(至多 8 块 × 215B + 16 次 `get_bits` + 1 次 `add_fields`),
超 64KiB 的输入直接丢弃。

## 构建与运行

```bash
./build.sh            # 构建 + 30 秒短跑(默认语料 corpus/)
./build.sh norun      # 只构建
./build.sh -runs=10000 corpus/        # 自定义轮数
./build.sh -max_total_time=300 corpus/  # 自定义时长
```

默认短跑在 `build/corpus-run/`(语料的临时拷贝)上跑,新覆盖输入
不会写回入库语料;显式传参时按给定语料目录跑。

`build.sh` 先做特性探测:`clang++ -fsanitize=fuzzer,address` 编译链接
最小目标,失败时明确报错退出(不静默跳过)。

## 工具链要求(本机现状)

- 需要带 libFuzzer 的 **clang**(Ubuntu noble:`sudo apt-get install -y clang`,
  自带 clang 18;运行时缺失时再装 `libclang-rt-18-dev`)。
- 本机原装 **g++ 13.3 不支持 `-fsanitize=fuzzer`**(实测 `unrecognized argument`);
  2026-11-15 已在该 VM 上 `apt-get install -y clang`(18.1.3)装好,目标
  已编起来并短跑 30 秒:776,945 次执行,无崩溃/超时/泄漏产物,覆盖 482
  (`-runs=1` 种子轮:6 次执行全过)。注意 VM 会不定期被替换,clang
  随 VM 消失后需重装(见上);`build.sh` 开头的特性探测会明确报错。
- **已接进 CI**:`.github/workflows/protocol-regression.yml` 的独立 job
  `fuzz-frame-parse-linux`(与各回归 job 并行,失败不拖垮回归探针):job 内
  `sudo apt-get install -y clang libclang-rt-18-dev`(noble 自带 clang 18;
  libFuzzer 运行时随 `libclang-common-18-dev` 的传递依赖带进来,
  `libclang-rt-18-dev` 再显式点一遍)后直接调 `./build.sh`(默认 30 秒短跑)。
  CI 镜像本身无 clang,但 runner 有 sudo apt 权限,与 wireshark-lua-linux
  装 lua5.4 同一路子,不必等镜像提供 clang。

## 发现崩溃后

libFuzzer 会在当前目录留下 `crash-<hash>`;用以下命令复现:

```bash
./build/fuzz_frame_parse crash-<hash>
```

修完后把该输入(去重后)补进 `corpus/`,回归时跑
`./build/fuzz_frame_parse -runs=1 corpus/` 全量过一遍。
