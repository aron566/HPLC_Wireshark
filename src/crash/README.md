# 崩溃转储模块(src/crash)

与业务代码解耦的崩溃捕获模块。业务侧只调一行:

```cpp
CrashHandler::install(CrashHandler::Options());
```

## 两套后端

| 后端 | 说明 | 崩溃产物 |
|------|------|----------|
| sentry-native | 官方 C/C++ SDK,内嵌 Crashpad,崩溃后上传到自建 Sentry 服务 | minidump + 上传到 Sentry |
| crashpad 原生 | 直接调 Crashpad API,纯本地落盘 | minidump 到本地目录 |

## 编译开关(qmake CONFIG)

BPLC_STA_Monitor.pro **默认启用 crashpad**(`CONFIG += crash_crashpad`),直接 qmake 即可。
显式控制:

```bash
# 只编 sentry(需先关掉默认的 crashpad,避免两个 handler 冲突)
qmake BPLC_STA_Monitor.pro "CONFIG-=crash_crashpad" "CONFIG+=crash_sentry"
# 只编 crashpad(默认,不用加参数)
qmake BPLC_STA_Monitor.pro
# 两个都编(运行时二选一)
qmake BPLC_STA_Monitor.pro "CONFIG+=crash_sentry"
# 都不编(CrashHandler::install 返回空)
qmake BPLC_STA_Monitor.pro "CONFIG-=crash_crashpad"
```

## 第三方依赖构建

```bash
# 需联网 + CMake(约 10 分钟)
3rdparty/build_crash_deps.sh sentry    # -> 3rdparty/install/sentry
3rdparty/build_crash_deps.sh crashpad  # -> 3rdparty/install/crashpad
3rdparty/build_crash_deps.sh all       # 两个都编
```

sentry 用 `SENTRY_BACKEND=crashpad` 静态编译,自带 `crashpad_handler`。
crashpad 用 getsentry fork 独立构建(与 sentry 内嵌的是同一 fork)。

## 运行配置(config.ini)

```ini
[crash]
backend=auto      # auto/sentry/crashpad
dsn=              # Sentry DSN,空=只本地落盘不上报
db_path=          # dump 目录,空=exe 同级 crashpad_db/
```

环境变量 `SENTRY_DSN` 优先于配置文件。

## 发布注意

- `crashpad_handler`(Linux) / `crashpad_handler.exe`(Windows) 必须放在 exe 同目录,
  否则后端 install 优雅失败(返回空,不崩溃)。
- 发布包建议带调试符号(或另存 `.debug` 文件),否则 dump 无法符号化到源码行。

## 测试

```bash
# 功能测试:两套后端各触发一次 SIGSEGV,检查 .dmp 生成;sentry 另测上传
src/crash/tests/run_crash_tests.sh

# 失败路径:handler 缺失 / db 不可写 / DSN 无效 / 无 DSN
src/crash/tests/run_failure_tests.sh

# dump 结构校验
python3 3rdparty/check_minidump.py <xxx.dmp>
```

sentry 上传链路用 `3rdparty/mock_sentry.py` 本地模拟验证(真服务部署见 `docker/sentry/README.md`)。

## 已验证(Linux)

- sentry 后端:SIGSEGV -> .dmp 生成 -> gzip multipart 上传到 mock,带 `upload_file_minidump`
- crashpad 后端:SIGSEGV -> .dmp 本地落盘
- 四种编译组合(none/sentry/crashpad/both)均编译通过
- 失败路径 5 项全过
- dump 经 `check_minidump.py` 确认为有效 minidump(8 流,含 Exception/ModuleList)

## 未验证

- Windows 构建与崩溃捕获(无 Windows 环境)
- 真实 self-hosted Sentry 端到端(本地无 Docker,文档已写,待有资源机器验证)
- 完整 HPLC_Wireshark 整机崩溃(本地 Qt 6.4.2,工程需 6.5+,待 CI)
