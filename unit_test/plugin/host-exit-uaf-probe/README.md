# host-exit-uaf-probe

插件宿主进程退出/崩溃时 `QProcess` 释放后使用(UAF)回归探针(PR #13 收尾)。

修复前 `PluginManager::shutdown_runtime()` 在 `proc->state() == QProcess::NotRunning` 时直接 `delete proc`。
而这条路径正是从该 `QProcess` 自己的信号里进来的:

```
QProcess::errorOccurred / finished → on_process_error / on_process_finished
  → disable_plugin → shutdown_runtime → delete proc        // 仍在 proc 自己的信号发射中
```

信号返回后 `QProcessPrivate::processFinished` 还会继续 `emit readChannelFinished()` / `finished()`
(崩溃时先 `errorOccurred(Crashed)` 再 `finished`),访问已释放对象。修复:`rt->process = nullptr; proc->deleteLater();`。
`stop_all()` 与 `waitForStarted` 失败处的同步 `delete` 不在 QProcess 信号里,保持不变。

用例(探针把自己复制成 `bplc-plugin-host`,按插件目录 `mode` 文件决定宿主行为):

1. `crash`(握手后 SIGKILL)、`exit`(握手后 `_exit(3)`)、`early`(握手前 SIGKILL)三个宿主退出 ⇒ 插件停用、主程序不崩;
   `early` 从未连 socket,停用必经 QProcess 信号 ⇒ 每次都确定走到原 `delete` 路径;
   事件循环跑过后 `PluginManager` 名下的 `QProcess` 只剩仍在运行的宿主(`deleteLater` 确实回收,不泄漏)。
2. `restart_all`:崩溃/退出停用的 3 个插件重新拉起并就绪;`meta.json` `enabled=false` 的 2 个跳过(`skipped_disabled=2`)不复活。
3. 重启后的第二代宿主再崩溃(`restart_plugin` 路径)同样不崩;`stop_all` 后无残留 `QProcess`。

**必须 ASan 构建 + 释放填充**:越界访问发生在未插桩的 Qt 库内部,ASan 默认不改写已释放内存,
旧代码读到的是"完好"的旧数据而不崩。`free_fill_byte` 让已释放的 `QProcess` 被填成 `0xbe…`,
旧代码必然 `SEGV on unknown address 0xbebebebe…`(本地对旧代码 3/3 复现)。

```
JOBS=1 QT_ROOT=<Qt 前缀> bash build.sh       # 已带 CONFIG+=sanitizer sanitize_address sanitize_undefined
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:max_free_fill_size=4096:free_fill_byte=190 \
UBSAN_OPTIONS=halt_on_error=1 QT_QPA_PLATFORM=offscreen \
  ./build/host_exit_uaf_probe/host_exit_uaf_probe   # 期望 asan=1 与 RESULT: ALL PASS (0 failures)
```

CI:`unit_test/ci/probe_suite.sh plugin/host-exit-uaf-probe`(protocol-regression.yml runtime-probes-linux)。
