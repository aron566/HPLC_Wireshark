# 工单：插件帧协议位（B2 阶段 P1 设计缺口）

发起：sytem-arch-analysis｜复核方：code-review-bot
状态：**已实施并加固**（vibe-coding 实施 → sytem-arch-analysis 验证发现接缝缺陷 → 补修 `entry_protocol_id()` + `[proto map]` 4 条断言）

> **工单本身的缺陷（留档）**：§2.5 只列了 3 个比较点，漏了 `mainwindow.cpp:911` 重建 `e.protocol` 的 `switch`。
> 实施方照单执行无误，缺陷源头在工单不完整 —— **写工单不能只 grep 比较点，必须追数据流的赋值点**。
> 当前基线：`tree_dispatch_check` **26/26**、主程序 build exit 0（104,223,017 B）、回放 2457 帧 / 50 ms / 崩溃 0。

---

## 0. 仓库与基线

- 仓库：`D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor`（Qt 6.11.2 / MinGW / qmake）
- 工作区**已有大量未提交内容**：只改本工单列出的文件，别碰其它
- 构建（PATH 需含 `C:/Qt/6.11.2/mingw_64/bin`、`C:/Qt/Tools/mingw1310_64/bin`）：
  ```
  qmake && mingw32-make -f Makefile.Release -j4
  ```
- 回归基线（**必须保持全绿，现在 20/20**）：
  ```
  cd samples/tree_dispatch_build && QT_QPA_PLATFORM=offscreen ./release/tree_dispatch_check.exe
  ```
- 回放（应 2457 帧、崩溃关键字 0，日志在 `release/logs/`）：
  ```
  QT_QPA_PLATFORM=offscreen ./release/BPLC_STA_Monitor.exe --replay samples/replay_test.bin
  ```
- 链接报 `Permission denied` = 还有 `BPLC_STA_Monitor.exe` 在跑，先
  `MSYS_NO_PATHCONV=1 taskkill /F /PID <pid>`（MSYS 下 `//F` 会被路径转换吃掉）

## 1. 问题

插件解析的帧，`PacketEntry::protocol` 落在内置值 0–3（`src/plugin/ipc/plugin_serialization.cpp:215` 附近
`static_cast<ProtocolVariant>(proto)`）。

插件的字段树 `PacketEntry::dissect_fields` **不落盘**（设计如此：它是渲染派生数据，
落盘会让每条解码多 27× 体积）。于是换页读回后 `dissect_fields` 为空 →
`ProtocolTree::show_packet`（`src/ui/protocoltree.cpp:150-156`）走 `builder_for(e)` →
**按内置协议布局画树** → 用户会把插件帧当内置协议读，字段全错位。

## 2. 要实现的修复

约束：**不新增结构体字段、不改 block 布局、不升版本号**。

1. `src/common/bplcframe.h`：在 `kProtocolGw2022 / kProtocolNw2021 / kProtocolGw2026 / kProtocolNw2026`
   那组常量旁新增
   ```cpp
   constexpr quint8 kProtocolPluginBit = 0x80;  ///< 高位=该帧由插件解析;低 7 位仍是内置协议编号
   ```
2. **在插件结果成型的那个位置**置位（IPC 反序列化之后、交给上层之前），不要 host 侧和 app 侧各置一次：
   ```cpp
   r.protocol = static_cast<ProtocolVariant>(quint8(r.protocol) | kProtocolPluginBit);
   ```
   `ProtocolVariant` 是 `enum class`（底层 int），0x80 可表示。
   如果发现置位点必须在 host/插件侧而非 app 侧，先说明原因再动手。
3. `src/ui/protocoltree.cpp` `builder_for()`：`e.protocol & kProtocolPluginBit` → **返回 nullptr**。
4. `show_packet()` 分派改三态：
   - `dissect_fields` 非空 → 通用渲染（现状不变）
   - 插件位 且 `dissect_fields` 为空 → 在协议段挂**一个提示节点**，文案例如
     `(插件字段树需重新解剖：该帧已换页，点击本行重新获取)`，**不要空着、不要画内置树**
   - 否则 → `builder_for(e)->build()`
5. 修掉加高位后失效的比较（掩掉高位再比），已知 3 处：
   - `src/app/mainwindow.cpp:935` — `e.protocol == kProtocolNw2021 || e.protocol == kProtocolNw2026`
   - `src/common/bplcframe.h:409` — `dir_display()` 里同样的南网判断
   - `src/ui/packetlistmodel.cpp:235` — `e.protocol == kProtocolGw2026`
   - `src/common/tests/block_format_test.cpp:68` 是往返比较，确认无需改即可
   统一写成 `(e.protocol & 0x7F) == kProtocolXxx` 形式。

## 3. 验收（缺一不可）

1. 构建 exit 0。
2. `tree_dispatch_check` 仍 **20/20 PASS**（含 `[dissect]`、`[RF SOF SNID]`、`[SNID merge]` 三阶段）。
3. 回放 2457 帧、崩溃关键字 0。
4. **新增 2 条断言**（加在 `samples/tree_dispatch_check.cpp`，该工程已链接 protocoltree /
   protocolfactory），总数 20 → **22**：
   - 构造 `PacketEntry`：`protocol = kProtocolGw2022 | kProtocolPluginBit`、`dissect_fields` 为空、
     `raw_bytes` 给 64 字节 → `show_packet` 后树里**不出现**内置 GW_2022 特征串 `"Net ID"`，
     且**出现**提示文案的关键子串。
   - 同一帧填入 `dissect_fields`（复用第 4 阶段的构造）→ 走通用渲染，仍不出现 `"Net ID"`。

## 4. 不要做

- 不改 `packetentry_serialize.h` 的 block 布局或 `block_version`（v10 刚定，未发布）
- 不改 `dissect_fields` 的"不落盘"策略
- 不顺便裁 `MpduInfo`（另一个更大任务，另行安排）
- 不动 `samples/autofit_*` 与 `docs/*` 已有内容

## 5. 回报内容

改了哪些文件、每处行为变化、22/22 的实际输出、以及"插件结果成型点"是否如预期好找。
