# plugin-proxy-fallback-probe (A2)

逐帧懒回退:插件协议不可用时,代理按帧懒取内置解析器(移植来源 parser 分支 A2)。

## 机制

- 直接构造 `PluginParserProxy`（无运行时、无 host 进程），喂 BEACON 帧
  （帧构造抄 plugin-unregister-fallback-probe 的 build_frame）。
- GW_2022 好 CRC 帧 → 内置接受；坏 CRC 帧 → 内置拒收（拒因是内置的 CRC
  拒因，不是 "plugin unavailable"）；重复解析复用缓存的回退实例。
- GW_2022 注册一个 Ext 条目（计数替身）后，代理仍走内置（替身 0 次调用），
  证明回退只取内置层、永不取他方 Ext 条目。
- 无内置的新 id PROBE_PURE_NEW → 拒收 "plugin unavailable (no builtin
  fallback)"。

## 运行

```bash
./build.sh
./build/plugin_proxy_fallback_probe/plugin_proxy_fallback_probe
```

预期 `RESULT: ALL PASS (0 failures)`，exit 0。
旧代码跑本探针：好帧断言 FAIL（拒收 "plugin unavailable"）。
