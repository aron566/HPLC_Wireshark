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
- 符号化链路:真实 crashpad dump 经 `dump_syms` + `minidump_stackwalk`
  符号化到函数名/文件名/行号(如 `level3() [crasher_crashpad.cc : 7]`)

## 未验证

- Windows 构建与崩溃捕获(无 Windows 环境)
- 真实 self-hosted Sentry 端到端(本地无 Docker,文档已写,待有资源机器验证)
- 完整 HPLC_Wireshark 整机崩溃(本地 Qt 6.4.2,工程需 6.5+,待 CI)

## 崩溃后处理:从 dump 到堆栈

程序崩溃后,按以下步骤定位问题。

### 1. 找到 dump

- **crashpad 后端**(默认):程序目录下 `crashpad_db/pending/*.dmp`
  (可在 `config.ini` `[crash] db_path` 改路径)
- **sentry 后端**:配了 DSN 会自动上传到 Sentry 服务端,网页上直接看;
  没配 DSN 时行为同 crashpad,只本地落盘。

### 2. 确认 dump 有效

```bash
python3 3rdparty/check_minidump.py crashpad_db/pending/*.dmp
```
看魔数 `MDMP OK`、流数量、有没有 `Exception` 流。无效的 dump(0 字节/损坏)直接丢弃,
检查磁盘空间和 `crashpad_handler` 是否与主程序同目录。

### 3. 符号化:dump -> 函数名/行号

需要**与崩溃版本严格对应**的符号包(`*_symbols.tar.gz`,CI 每次打包自动生成,
随版本存档,不要混用不同版本的符号)。

```bash
# 一键符号化
bash scripts/symbolize.sh <xxx.dmp> --symdir <解压后的符号目录>
```

输出示例:

```
Crash reason:  SIGSEGV /SEGV_MAPERR
Crash address: 0x0
Thread 0 (crashed)
 0  BPLC_STA_Monitor!frame_dispatcher::dispatch [framedispatcher.cpp : 123 + 0x1a]
 1  BPLC_STA_Monitor!mainwindow::on_data [mainwindow.cpp : 456 + 0x2b]
```

读法:
- `Thread 0 (crashed)` 是崩溃线程,只看它
- 帧 0 是崩溃点,往下是调用栈(谁调了谁)
- `Crash address: 0x0` = 空指针解引用;看帧 0 的源码行一般直接定位

### 4. 常见崩溃模式速查

| Crash reason | 含义 | 先查什么 |
|---|---|---|
| `SIGSEGV /SEGV_MAPERR` addr 0x0 | 空指针解引用 | 帧 0 那行哪个指针为空,往上找是谁没判空 |
| `SIGSEGV /SEGV_MAPERR` addr 非 0 很小 | 野指针/已释放对象 | 帧 0 的对象生命周期,跨线程访问? |
| `SIGABRT` | assert/terminate/未捕获异常 | 堆栈里找 `__assert_fail` 或异常类型 |
| `SIGILL` | 非法指令 | 二进制与 CPU 架构不匹配(少见) |

### 5. 上报/归档

- 保留:`.dmp` 文件 + 对应版本 + 复现步骤,三者缺一不可
- 符号包按版本号存档(`dist/*_symbols.tar.gz`),只在分析时解压,不发给用户
- 同一版本同一崩溃地址的 dump 只需分析一次

### 工具链说明

- `3rdparty/build_sym_tools.sh`:从 breakpad 源码构建 `dump_syms`/`minidump_stackwalk`
  (产物在 `3rdparty/install/symtools/`,git 忽略,缺失时 `symbolize.sh` 自动构建)
- `scripts/symbolize.sh`:一键符号化,用法见上面第 3 步
- `scripts/package_linux.sh`:打包时自动生成符号包(`-g` 已默认加入 release 编译选项)
- 符号包不进用户安装包,只随 Release 附件发布供开发分析
