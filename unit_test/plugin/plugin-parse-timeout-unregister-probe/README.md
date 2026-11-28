# plugin-parse-timeout-unregister-probe (A3)

解析超时后,工作线程内同步注销注册表条目(移植来源 parser 分支 A3)。

## 机制

- 可执行文件自复制为 `bplc-plugin-host`,`--socket` 参数进入 fake-host 模式。
- fake host 用 Envelope 帧格式(Ping 回 Pong,心跳不断),但对 ParseRequest
  故意不回,工作线程调 `parse_via_plugin` 5 秒超时返回 false。
- 主线程在等待期间只 msleep 轮询,不泵事件循环:排队到 GUI 的注销/状态
  回调跑不起来,以此区分"工作线程内同步注销"与"GUI 排队注销"。
- 关键断言在 GUI 泵之前就做:注册表条目已空、按归属取解析器为空。

## 运行

```bash
./build.sh
./plugin_parse_timeout_unregister_probe
```

预期 `RESULT: ALL PASS (0 failures)`,exit 0,运行时约 8 秒。
旧代码跑本探针:注册表条目仍在,"registry entry already gone before GUI
pump" 与 "find_protocol_for_owner returns null" 断言 FAIL。
