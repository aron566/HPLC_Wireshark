# mac-filter-probe

主界面 MAC 过滤探针：MAC 形状 token（2~6 组两位十六进制冒号分隔，
如 `03:02`，大小写不敏感）在包列表模型自身的 TEI↔MAC 映射表上
按 DNF 展开为命中 TEI 的或；无命中则该组作废、整表达式无组即永假
（不回落子串）；模式可在 MAC 任意字节偏移处出现（顺序敏感，
完整 6 组仍精确）。

## 构建与运行

```
./build.sh
./build/mac_filter_probe
```

## 状态：main 源码下部分失败（预期内，非探针缺陷）

MAC 过滤特性在未合入 main 的分支 `feat/main-filter-mac-search`
（d5d4629）。对 main（`39e6e42`）源码构建运行：
**28/47 通过，19 项 MAC 展开断言失败，exit 1**；
旧回归断言（sta-5、beacon、空串、not-mac 回退等）全部通过。
补充验证：同一探针链接该分支的 `packetlistmodel.{cpp,h}`
（只读 `git show` 到忽略的 `build/feature/` scratch，不入库）→
**47/47，`RESULT: 0 failures`，exit 0**。
特性合入 main 后，本用例应对 main 全过；届时若仍有失败，
先查特性实现而非探针。
