# 崩溃转储模块(src/crash)

与业务代码解耦的崩溃捕获模块。业务侧只调一行:

```cpp
CrashHandler::install(CrashHandler::Options());
```

## 两套后端

| 后端 | 说明 | 崩溃产物 |
|------|------|----------|
| sentry-native | 官方 C/C++ SDK,内嵌 Crashpad,崩溃后上报到 Sentry 服务(自建或 sentry.io SaaS 均可) | minidump + 上传到 Sentry |
| crashpad 原生 | 直接调 Crashpad API,配 DSN 则自动上报,否则纯本地落盘 | minidump 到本地目录/上报到 Sentry |

## 编译开关(qmake CONFIG)

BPLC_STA_Monitor.pro **默认启用 crashpad**。关闭默认用
`CONFIG+=crash_no_default`(不要用 `CONFIG-=crash_crashpad`:
qmake 命令行的 `-=` 在 .pro 求值前处理,删不掉 .pro 里默认加上的开关,
`contains()` 照样看到它,2026-09-30 实测 CI 矩阵因此翻车)。
显式控制:

```bash
# 只编 sentry(需先关掉默认的 crashpad)
qmake BPLC_STA_Monitor.pro "CONFIG+=crash_no_default" "CONFIG+=crash_sentry"
# 只编 crashpad(默认,不用加参数)
qmake BPLC_STA_Monitor.pro
# 两个都编(编译期可共存;运行时按 backend 互斥二选一,auto 时优先 sentry)
qmake BPLC_STA_Monitor.pro "CONFIG+=crash_sentry"
# 都不编(CrashHandler::install 返回空)
qmake BPLC_STA_Monitor.pro "CONFIG+=crash_no_default"
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
dsn=              # Sentry DSN,如 http://<key>@host:9000/1;空=只本地落盘不上报
db_path=          # dump 目录,空=exe 同级 crashpad_db/
```

环境变量 `SENTRY_DSN` 优先于配置文件。

注意:`backend=sentry` 时若 sentry 后端安装失败(如 handler 缺失),
会静默回退到 crashpad 后端;两个后端都装不上才返回空
(程序照常启动,只是无崩溃捕获,不会崩溃)。
需要严格确认当前生效后端时,检查 `CrashHandler::install()` 返回值或启动日志。

## crashpad 后端如何配置上传 dump

crashpad 后端默认只本地落盘(`<db>/pending/*.dmp`;`reports/` 只是 handler
搬运前的瞬态目录,稳定位置看 `pending/`),配了 DSN 才会自动上报,
逻辑与 sentry 后端一致——**配 DSN 就上报,不配就只落盘**:

1. 在 `config.ini [crash]` 里填 `dsn=http://<key>@<host>:<port>/<project_id>`
   (或设环境变量 `SENTRY_DSN`)。
2. 后端启动时自动把 DSN 派生为 Sentry minidump 上报地址并传给
   `crashpad_handler`:
   `http(s)://<host>[:port]/api/<project_id>/minidump/?sentry_key=<key>`
3. `crashpad_handler` 还要求数据库层面的上传开关,后端已自动打开
   (`CrashReportDatabase::GetSettings()->SetUploadsEnabled(true)`);
   崩溃后 handler 在后台把 `reports/` 里的 dump 以 multipart
   (`upload_file_minidump` 字段) POST 到该地址。
4. 若上报地址不是标准 Sentry minidump 端点(如自研收集服务),可直接指定完整
   URL(代码侧 `CrashHandler::Options::upload_url`,优先级高于 DSN 派生)。

注意:
- 上传由 `crashpad_handler` 进程在后台完成,不是崩溃瞬间同步发出,实测延迟
  约 1 分钟;上报失败会在本地保留 dump,下次启动 handler 时重试。
- 自建 Sentry 服务端需开启 minidump 接收(标准 Sentry 即支持,见 `docker/sentry/README.md`)。
- 本地验证:`bash src/crash/tests/run_crash_tests.sh` 会起 `3rdparty/mock_sentry.py`,
  用 DSN 跑 crashpad 后端并断言 mock 收到带 minidump 的上报。

## 发布注意

- `crashpad_handler`(Linux) / `crashpad_handler.exe`(Windows) 必须放在 exe 同目录。
  缺失时对应后端 install 失败:另一后端可用则静默回退,都不行才返回空
  (程序照常启动,只是无崩溃捕获,不会崩溃)。
  sentry 后端同样依赖同目录的 `crashpad_handler`(sentry-native 内嵌 Crashpad,
  缺失则崩溃时无 minidump)。
- 发布包建议带调试符号(或另存 `.debug` 文件),否则 dump 无法符号化到源码行。

## 测试

```bash
# 功能测试:两套后端各触发一次 SIGSEGV,检查 .dmp 生成;sentry 另测上传
src/crash/tests/run_crash_tests.sh

# 失败路径:handler 缺失 / db 不可写 / DSN 无效 / 无 DSN
src/crash/tests/run_failure_tests.sh

# dump 结构校验
python3 3rdparty/check_minidump.py <xxx.dmp>

# 真实应用验证:发布包 --self-crash-test 触发崩溃,符号化定位到
# crash_selftest_trigger 及其 main.cpp 行号(只验结构不算通过)
src/crash/tests/run_app_crash_test.sh

# App 编译组合矩阵:default/sentry-only/both/none 四种 CONFIG 各自构建、
# 冒烟、崩溃捕获;带后端的组合必须符号化定位到崩溃处(CI app-config-matrix)
src/crash/tests/run_app_config_matrix.sh
```

sentry 上传链路用 `3rdparty/mock_sentry.py` 本地模拟验证(真服务部署见 `docker/sentry/README.md`)。

## 已验证(Linux)

- sentry 后端:SIGSEGV -> .dmp 生成 -> gzip multipart 上传到 mock,带 `upload_file_minidump`
- crashpad 后端:SIGSEGV -> .dmp 本地落盘
- App 四种编译组合(default/sentry-only/both/none)的 CI 矩阵
  (`app-config-matrix` job):2026-09-30 CI #30 全绿,通过
  (run 36605011698,feat/crash-dump@8e31483,总耗时 8m28s)。
  之前首跑(#29)24/25 挂在 `none` 组合——根因是旧脚本用
  `CONFIG-=crash_crashpad` 关默认,实际关不掉(见"编译开关"节),
  `none` 编出来仍带 crashpad,崩溃后有 dump,测试正确判 FAIL;
  `sentry-only` 当时实际编进了双后端(测试只验了 sentry 路径,侥幸通过)。
  已改用 `CONFIG+=crash_no_default` 方案并重跑验证通过。
  注意:build-windows 在本轮有一条 "Process completed with exit code 1"
  的 error 标注,但 run 总体结论为 Success(疑为 continue-on-error 步骤,
  未及逐条核实,详见 Actions 页面)。
- 失败路径 5 项全过
- dump 经 `check_minidump.py` 确认为有效 minidump(含 Exception/ModuleList 等关键流)
- 符号化链路:真实发布包 dump 经 `dump_syms` + `minidump_stackwalk`
  定位到 `crash_selftest_trigger [main.cpp : 36]`
- sentry.io SaaS 端到端:崩溃事件已上报,服务端符号化到函数名+源码行

## Windows(在 CI 验证)

已验证:2026-09-30 CI #35 全绿(run 36655433605,feat/crash-dump@6da9a57),
build-windows 所有步骤通过:
- sentry 依赖构建为必需步骤(不再是"实验性,允许失败"):sentry-native 0.17.1
  在 MinGW 下内嵌编译 crashpad 成功
- `run_crash_tests_win.sh` 按编译选择验证两个后端,均通过:
  crashpad(Access Violation -> .dmp -> addr2line 定位到 `do_crash`)与
  sentry(编译+链接+崩溃捕获+本地落盘)
- 过程中修了三个 Windows 特有问题:ZLIB 缺失
  (`-DCRASHPAD_ZLIB_SYSTEM=OFF`)、内嵌 crashpad 编 `capture_context.asm`
  找不到 uasm(`build_sentry` 补调 `ensure_uasm`)、sentry 静态库链接需
  `-DSENTRY_BUILD_STATIC` + `-lsynchronization`

- 依赖构建:`bash 3rdparty/build_crash_deps.sh crashpad`
  (Git Bash + MinGW;crashpad 的 getsentry fork 支持 MinGW,脚本自动源码自举)
- 崩溃测试:`bash src/crash/tests/run_crash_tests_win.sh`
  (编译 `crash_test_crashpad.exe`,触发 Access Violation,检查 `db/reports/*.dmp`)
- 符号化:MinGW 生成 DWARF(无 PDB),不走 breakpad `.sym` 流程,
  用 `scripts/symbolize_win.py <xxx.dmp> <xxx.exe>`(内部调 MinGW 的 `addr2line`),
  直接输出函数名与源码行。exe 须带 `-g` 且未 strip。
- 打包:`scripts/package_ci.sh` 会把 `crashpad_handler.exe` 拷到 exe 同目录;
  缺失时只告警(后端回退,程序照常启动,无崩溃转储)。
- 发布包冒烟:CI 里 `BPLC_STA_Monitor.exe -platform offscreen` 跑 20 秒,
  要求 `release/crashpad_db` 下无 `.dmp`(无启动期崩溃)。
- App 级 `--self-crash-test` 符号化定位目前仅 Linux CI 覆盖
  (`run_app_crash_test.sh` / `app-config-matrix` 均为 Linux job),Windows 待补。

## 未验证

- 真实 self-hosted Sentry 端到端(sentry.io SaaS 已验证;本地无 Docker,
  自建部署文档已写,待有资源机器验证)

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

### 6. coredump 定位(gdb,开发机深度调试)

minidump 只能看到堆栈;coredump 是内核在进程死于信号时写下的**完整内存快照**,
用 gdb 打开能看到所有线程的堆栈、局部变量、堆内存、寄存器。
两条路线不冲突(见下),选哪个看场景:

| | minidump(crashpad) | coredump(内核) |
|---|---|---|
| 谁写的 | crashpad_handler | Linux 内核 |
| 体积 | 几十 KB | 几 MB ~ 数 GB(全内存) |
| 分析工具 | `minidump_stackwalk` + 符号包 | gdb 直接打开 |
| 能看到 | 崩溃线程堆栈、模块列表 | 全部线程堆栈、变量值、堆内存、寄存器 |
| 适用 | 用户现场、生产环境、归档上报 | 开发机复现、想看变量/内存时 |
| 短板 | 看不到变量值 | 文件大、可能含敏感数据、用户机器一般拿不到 |

**关键:crashpad 不会吞掉 coredump。**
实测我们的崩溃进程退出码是 139(=128+SIGSEGV),说明 crashpad 写完 minidump 后
恢复了信号的默认处置并重新抛出,进程最终仍死于信号本身。
因此只要系统允许(`ulimit -c`),两者兼得:`crashpad_db/` 下有 `.dmp`,
系统 core 位置同时有一份 coredump。

#### 6.1 开启 coredump

```bash
# 1) 当前 shell 允许写 core(很多发行版默认是 0=禁止)
ulimit -c unlimited

# 2) 看 core 会写到哪
cat /proc/sys/kernel/core_pattern
```

几种常见情况:

- `core`:写到进程当前目录,文件名就叫 `core`
- `|/usr/share/apport/apport ...`:Ubuntu 的 apport 接管,桌面会弹窗,
  去 `/var/crash/` 找,或用 `apport-cli`
- `|/usr/lib/systemd/systemd-coredump ...`:systemd 接管,用 `coredumpctl` 管理:
  ```bash
  coredumpctl list                  # 列出收到的崩溃
  coredumpctl dump <pid> -o core    # 导出某次崩溃的 core
  coredumpctl gdb <pid>             # 直接进 gdb
  ```
- 开发机想固定落盘(推荐):
  ```bash
  sudo sysctl -w kernel.core_pattern=/tmp/core.%e.%p.%t
  # %e=程序名 %p=pid %t=时间戳,避免多次崩溃互相覆盖
  ```

注意:
- `ulimit -c` 是按进程继承的;用 systemd 服务或桌面启动器起的程序,
  要在对应 unit 里配 `LimitCORE=infinity`,在 shell 里设了也没用
- 容器/CI 里可能 hard limit 就是 0(如本开发 VM),那就写不出 core,
  只能用下面的 gdb 现场生成

#### 6.2 复现崩溃,拿到 core

```bash
ulimit -c unlimited
cd /tmp && ./BPLC_STA_Monitor   # 在你选定的 core 目录下启动,复现操作
# 终端打印: Segmentation fault (core dumped)
ls -lh /tmp/core*               # 确认 core 已生成
```

#### 6.3 gdb 定位(核心步骤)

二进制必须与崩溃时**严格是同一个**(我们的 release 默认带 `-g`,行号可用):

```bash
gdb ./BPLC_STA_Monitor /tmp/core.BPLC_STA_Monitor.1234
```

进去后常用命令(按定位顺序):

```gdb
bt                   # 崩溃线程完整堆栈 ← 先看这个,#0 就是崩溃点
thread apply all bt  # 所有线程堆栈(死锁、跨线程野指针、Qt 信号槽跨线程必看)
frame 0              # 切到崩溃帧(编号按 bt 的 #n 来)
list                 # 显示崩溃点前后 10 行源码
info args            # 崩溃函数的参数值
info locals          # 崩溃帧的局部变量
p ptr                # 打印指针值,看是不是 0x0
p *ptr               # 解引用看对象内容
p str.toUtf8().data  # Qt:调方法看 QString 内容(需符号,偶尔调不通就换 pretty-printer)
x/16xg 0x7fffffffd0  # 按 16 个 64 位看一块内存
info registers       # 寄存器(rip=崩溃指令地址)
up / down            # 沿堆栈上下走,逐帧看变量
```

真实输出示例(Ubuntu 24.04 + gdb 15.1 实测):

```
Core was generated by `/tmp/coretest/app --self-crash-test'.
Program terminated with signal SIGSEGV, Segmentation fault.
#0  0x000055555555dfa7 in crash_selftest_trigger () at app_standin.cpp:8
8       *(volatile int*)g_crash_test_addr = 0xdead;
#1  0x000055555555ddde in main (argc=<optimized out>, ...) at app_standin.cpp:21
```

读法:
- 第一行确认:谁、死于什么信号
- `#0` 是崩溃点:函数名 + 源码文件 + 行号,下面直接贴出那行代码
- `#1` 是调用者,依次往下就是完整调用链
- `argc=<optimized out>` 是 -O2 的正常现象:变量被优化掉了,不是符号坏了

#### 6.4 Qt 程序的额外技巧

- **变量 `<optimized out>`**:release 是 -O2,大量局部变量会被优化掉。
  只想看堆栈和崩溃行不受影响;真要深挖变量值,用 `CONFIG+=debug`
  重新编一个 debug 版复现(`qmake "CONFIG+=debug"`,不要发给用户)。
- **QString/QList 显示为内部结构**:gdb 原生不懂 Qt 类型。
  装 Qt Creator 自带的 pretty printers(一套 python 打印脚本,配到 `~/.gdbinit`),
  或者土办法:`p *(QStringData*)ptr` 按内部布局硬看。
- **多线程**:Qt 程序崩溃先 `thread apply all bt`,找到标 `SIGSEGV` 的那个线程;
  gdb 打开 core 时默认选中的一般就是收到致命信号的线程,直接 `bt` 多半就是它。
- **库版本**:`info sharedlibrary` 确认 gdb 加载的 Qt 库就是崩溃机器上跑的那些;
  混用不同版本 Qt 的二进制+core,行号会对不上。

#### 6.5 没 core 时的现场生成(gdb)

容器里 `ulimit -c` 打不开、或用户机器不方便配 core_pattern 时,
在开发机上用 gdb 直接复现,崩溃瞬间生成 core:

```bash
gdb ./BPLC_STA_Monitor
(gdb) run --self-crash-test        # 或正常操作复现
# Program received signal SIGSEGV ...
(gdb) bt                           # 直接看,不用等 core
(gdb) generate-core-file /tmp/core.app   # 需要存档时再导出 core
```

#### 6.6 注意事项

- core 文件很大,分析完及时删,别进 git(`dist/`、`*.core`、`core` 已在 `.gitignore`)
- core 含进程**全部内存**:用户数据、密码、密钥都可能在里面。
  不要发公开渠道,内部分享先评估,能只给 minidump 就别给 core
- 用户现场一般拿不到 core(apport 弹窗/权限/ulimit),minidump 仍是首选上报手段;
  coredump 是开发机复现时的重武器
- gdb 直接读二进制里的 DWARF,不需要额外符号包;
  但二进制必须与崩溃时严格一致,差一个 commit 行号就可能错位

#### 6.7 选型速查

- 用户发来 `.dmp` → `scripts/symbolize.sh`(第 3 步)
- 自己机器能复现、想看变量和内存 → coredump + gdb(本节)
- crashpad 已接管时的崩溃:minidump 和 coredump 同时产生,
  前者上报归档,后者本地深挖(两者不互斥)
- 崩溃在 main 极早期、crashpad 还没装上 → 只有 coredump 能抓到
- 要归档、上报、进 Sentry → minidump(小、可传输)

### 工具链说明

- `3rdparty/build_sym_tools.sh`:从 breakpad 源码构建 `dump_syms`/`minidump_stackwalk`
  (产物在 `3rdparty/install/symtools/`,git 忽略,缺失时 `symbolize.sh` 自动构建)
- `scripts/symbolize.sh`:一键符号化,用法见上面第 3 步
- `scripts/package_linux.sh`:打包时自动生成符号包(`-g` 已默认加入 release 编译选项)
- 符号包不进用户安装包,只随 Release 附件发布供开发分析
