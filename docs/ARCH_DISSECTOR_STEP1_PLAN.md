# 方案：接入通用字段树（Dissector Step 1）

来源：`origin/feat/plugin-dissector-arch`（项 1+3 精简版）
目标分支：`feat/next-gen-protocol`（已有 `IProtocolProvider` 注册表 + 按 `e.protocol` 逐帧分派）
状态：**已实施（2026-10-22）**，18/18 回归断言通过

实施结果（含并入的 3 个死字段删除）：

| 项 | 结果 |
|---|---|
| Release 构建 | exit 0，`release/BPLC_STA_Monitor.exe` 104,051,741 B |
| 回归 `tree_dispatch_check` | **20/20 PASS**（第 4 阶段 `[dissect]` 5 + 第 5 阶段 `[RF SOF SNID]` 1 + 第 6 阶段 `[SNID merge]` 1） |
| 回放 | 2457 帧 / 104 ms，崩溃关键字 0 |
| 换页块 v10 往返 | **PASS**（120 条跨 3 个压缩分块，逐字段比对含 `coord_rsv1/2/3` 与 `raw_bytes`） |
| 版本号 | `kWireFormatVersion` 6→**7**；`block_version()` 9→**10** |
| `sizeof(MpduInfo)` | **192 B**（实测；删 3 死字段前为同值量级，裁剪收益留到 B4） |

改动文件：`bplcframe.h`、`packetentry_serialize.h`、`iprotocolparser.h`、
`plugin_serialization.{h,cpp}`、`plugin_ipc.h`、`protocol_tree_builder.{h,cpp}`、
`protocoltree.cpp`、`mainwindow.cpp`（9 个 +1 行拷贝）。

---

## 1. 目标与范围

### 做什么
让"插件解析器"能返回一棵**与协议无关的字段树**，由主界面**统一渲染**，
行为与内置解析器一致（Hex 高亮联动、类型字段着色）。

### 为什么现在做不了
`IProtocolProvider::create_tree_builder()` 返回 `unique_ptr<IProtocolTreeBuilder>`，
而该接口用 `QTreeWidgetItem` 输出 → 插件进程里构造不了 QTreeWidget，
只能返回 `nullptr` → 插件协议的字段树只剩 `Frame/Physical` 公共节点。

### 不做什么（本期明确排除）
| 项 | 原因 |
|---|---|
| `IDissectorPlugin` / `DissectResult` / `FrameContext` | Step 2；本期用现成的 `IProtocolParser` 即可 |
| 列能力 `DissectColumn` / `column_values` | P1，侵入 `packetlistmodel` + `mainwindow`，冲突大 |
| 着色契约 `DissectColorRule` / `color_id` | 依赖列能力管线 |
| 插件总线、lifecycle、IPC 单读者修复 | P2 |

---

## 2. 端到端数据流（本期打通的部分）

```
native 插件实现 IProtocolParser::parse()
    → r.dissect_fields = { ... }          ← 新增：插件直接填（已 include iprotocolparser.h）
PluginParserProxy::parse → IPC(ParseResponse)
    → plugin_serialization: ParseResult 追加 dissect_fields（wire v7）
MainWindow::on_parsed
    → e.dissect_fields = r.dissect_fields  ← 新增 1 行
PacketListModel（换页落盘 / 读回）
    → packetentry_serialize: 追加字段（block v10）
ProtocolTree::show_packet
    → 非空：render_dissect_fields() 通用渲染
    →  空：builder_for(e)->build() 内置构建器（现状不变）
```

关键点：**不需要新的插件接口**。native 插件已经拿到 `ParseResult&`，直接填即可。

---

## 3. 契约（必须写进代码注释，否则一定踩）

1. **坐标基准 = `PacketEntry::raw_bytes`**（即 `ParseResult::payload_for_log`）。
   `DissectField::start` / `len` 与 Hex 高亮同一坐标系。
   插件若剥掉了媒介前缀，**必须自己把 start 加回剥离长度**；宿主不做偏移换算。
2. **`start = -1` 表示该字段无字节映射**（如分组节点、派生值），此时不参与高亮。
3. **`severity`**：0=无 1=提示 2=警告 3=错误；仅影响前景色。
4. **`name` 约定与 `MsduFieldNode` 一致**：位域字段名带 `[Nbit]` 后缀，
   以便 `tree_type_field_category()` 识别 `MMe Type` / `BusinessID` / `MSDUType` 并着色。
5. **内置解析器恒不填 `dissect_fields`** → 渲染路径零变化（这是本期"零回归"的依据）。

---

## 4. 逐文件改动

### 4.1 `src/common/bplcframe.h`

**插入点 A**：`struct MsduFieldNode` 之后、`/// @brief 管理帧携带的 TEI→MAC 学习对` 之前。

```cpp
/// @brief 通用解剖字段节点(自递归树)
/// @details 协议插件返回、主界面统一渲染(与 MsduFieldNode 同层,避免
///          common → plugin 的反向依赖)。坐标以 PacketEntry::raw_bytes
///          (=ParseResult::payload_for_log)为基准,与 Hex 高亮同一坐标系;
///          插件若剥离了媒介前缀,须自行把 start 加回剥离长度。
struct DissectField {
    QString name;                ///< 字段名(位域带 [Nbit] 标注,约定同 MsduFieldNode)
    QString value;               ///< 格式化值文本
    int     start = -1;          ///< raw_bytes 中的字节偏移,-1=无字节映射
    int     len   = 0;           ///< 覆盖字节数
    quint8  severity = 0;        ///< 0=无 1=提示 2=警告 3=错误(仅影响前景色)
    QVector<DissectField> children;

    DissectField() = default;
    DissectField(const QString& n, const QString& v, int s = -1, int l = 0)
        : name(n), value(v), start(s), len(l) {}
};
```

**插入点 B**：`PacketEntry` 内 `QString search_text;` 之后。

```cpp
    QVector<DissectField> dissect_fields; ///< 插件通用字段树(内置解析为空;仅协议树视图消费)
```

> `PacketEntry` 不进插件 ABI 指纹，加字段不影响插件兼容性。

---

### 4.2 `src/protocol/iprotocolparser.h`

`ParseResult` 内 `QByteArray payload_for_log;` 之后追加：

```cpp
    /// 插件解析器返回的通用字段树(内置解析器恒为空)。
    /// 非空时 ProtocolTree 走通用渲染,为空时走协议 builder(现状)。
    QVector<DissectField> dissect_fields;
```

> `ParseResult` **进**插件 ABI 指纹（`plugin_host_abi()` 含 sizeof）→ 本期改动会让
> 所有 native 插件 ABI 失配、被拦截加载。这是**预期行为**：旧插件填不了
> `dissect_fields`，且其二进制与新结构体布局不符。见 §7.2。

---

### 4.3 `src/protocol/protocol_tree_builder.h/.cpp`（纯重构，零行为变化）

**头文件**：`tree_type_field_color()` 声明之后加

```cpp
/// 给类型字段节点(MMe/APP/MSDU 类型)按类别+值着色;非类型字段不动。
/// 内置 tree_render_msdu 与插件通用渲染共用,保证着色行为一致。
void tree_apply_type_color(QTreeWidgetItem* it, const QString& name,
                           const QString& value);
```

**实现**：`tree_type_field_color()` 之后、`tree_render_msdu()` 之前加

```cpp
void tree_apply_type_color(QTreeWidgetItem* it, const QString& name,
                           const QString& value) {
    const int cat = tree_type_field_category(name);
    if (cat < 0) return;
    const QColor c = tree_type_field_color(cat, parse_leading_number(value));
    it->setForeground(0, c);
    it->setForeground(1, c);
}
```

**改 `tree_render_msdu()`**：把原有的 6 行内联着色

```cpp
        const int cat = tree_type_field_category(n.name);
        if (cat >= 0) {
            const QColor c = tree_type_field_color(cat, parse_leading_number(n.value));
            it->setForeground(0, c);
            it->setForeground(1, c);
        }
```

替换为

```cpp
        tree_apply_type_color(it, n.name, n.value);
```

---

### 4.4 `src/ui/protocoltree.cpp`

**(a)** 在已有匿名命名空间（含 `variant_of`）内追加渲染器：

```cpp
/// @brief 通用字段树渲染(插件解析帧)
/// @details 与内置构建器同款节点约定:kRoleStart/kRoleLen 供 Hex 高亮,
///          类型字段复用 tree_apply_type_color 着色;severity 再叠加告警色。
///          越界坐标只告警不崩溃(插件可能算错,不能拖垮 UI)。
void render_dissect_fields(QTreeWidgetItem* parent,
                           const QVector<DissectField>& fields,
                           int raw_bytes_size) {
    for (const DissectField& f : fields) {
        QTreeWidgetItem* it = new QTreeWidgetItem(parent);
        it->setText(0, f.name);
        it->setText(1, f.value);
        int s = f.start, l = f.len;
        if (s >= 0 && (l <= 0 || s + l > raw_bytes_size)) {
            qWarning("dissect field out of range: %s start=%d len=%d raw=%d",
                     qPrintable(f.name), s, l, raw_bytes_size);
            s = -1; l = 0;
        }
        it->setData(0, kRoleStart, s);
        it->setData(0, kRoleLen, l);
        tree_apply_type_color(it, f.name, f.value);
        if (f.severity >= 3) {
            it->setForeground(0, QColor(0xd0, 0x30, 0x30));
            it->setForeground(1, QColor(0xd0, 0x30, 0x30));
        } else if (f.severity == 2) {
            it->setForeground(0, QColor(0xb0, 0x70, 0x00));
            it->setForeground(1, QColor(0xb0, 0x70, 0x00));
        }
        if (!f.children.isEmpty())
            render_dissect_fields(it, f.children, raw_bytes_size);
    }
}
```

> 需要 `#include <QDebug>`（`qWarning`）；`QColor` 已通过 `<QColor>`/QTreeWidget 链路可用，若报错再显式 include。

**(b)** `show_packet()` 末尾，把

```cpp
    // 协议特有字段树(MPDU Base + 各帧型 FCH + PB 块 + MSDU 挂载)由构建器构建;
    // GW_2026 下按帧内 HDC 版本分派(GW_2022 帧走 HDC 1.0 布局构建器)
    if (IProtocolTreeBuilder* b = builder_for(e))
        b->build(root, e);
```

改为

```cpp
    // 协议段字段树:插件解析帧带通用字段树 → 通用渲染;
    // 内置解析帧(dissect_fields 恒空) → 按 e.protocol 分派的协议构建器
    if (!e.dissect_fields.isEmpty()) {
        render_dissect_fields(root, e.dissect_fields, e.raw_bytes.size());
    } else if (IProtocolTreeBuilder* b = builder_for(e)) {
        b->build(root, e);
    }
```

> `builder_for()` 与 `m_builders` 缓存**完全不动**。

---

### 4.5 `src/app/mainwindow.cpp`

`on_parsed` 内 `e.raw_bytes = r.payload_for_log;` 之后加一行：

```cpp
    e.dissect_fields = r.dissect_fields;  // 插件解析器通用字段树(内置恒空)
```

---

### 4.6 ~~`src/common/packetentry_serialize.h`（换页落盘）~~ → **改为不进换页载荷**

> **2026-10 修订（实测后推翻原设计）**：原方案让 `dissect_fields` 进换页盘块。
> 实测证明这会把"自动列宽/AUTO FIT 卡顿"的成因放大到插件帧上——字段树是
> **渲染派生数据**，却成了存储，导致每次读一条 `PacketEntry` 都要重建整棵树。
> 见 §7.3.1 的实测数据。故本期**不落盘、不 bump `block_version`**。

正确做法：

| 位置 | 处理 |
|---|---|
| 驻留热区（`m_hot`，<= 1000 条） | 保留 `dissect_fields`，详情面板直接用 |
| 落盘块（`pser` 块载荷） | **不写** `dissect_fields` |
| 换页读回后打开详情 | `activate_row` 时对该条**重新调用一次插件解析**（单条 IPC，毫秒级），得到字段树 |

代价：详情面板对换页条目的首次打开有一次 IPC 往返（插件实现得好就是一次 `dissect()`）。
收益：
1. 列宽/滚动/绘制等**不需要字段树的路径**不再为它付解码代价
2. 盘块体积不膨胀（实测字段树使 1000 条从 26 KB 涨到 720 KB，**27×**）
3. `block_version()` 保持 **9**，本方案少一处版本迁移

因此**取消**本节原定的 `write_dissect_fields` / `read_dissect_fields` 与 block v10。

---

### 4.7 `src/plugin/ipc/plugin_serialization.h/.cpp` + `plugin_ipc.h`

插件返回的 `ParseResult` 要经 IPC 传回主程序，**不序列化等于白填**。

**(a)** `plugin_serialization.h` 在 `// ---- MsduFieldNode (递归) ----` 附近加声明：

```cpp
// ---- DissectField (递归) ----
QDataStream& operator<<(QDataStream& out, const DissectField& f);
QDataStream& operator>>(QDataStream& in, DissectField& f);
```

**(b)** `plugin_serialization.cpp` 实现（Qt 自带 `QVector<T>` 的流算子，元素算子声明即可）：

```cpp
// ---- DissectField (递归) ----
QDataStream& operator<<(QDataStream& out, const DissectField& f) {
    out << f.name << f.value << f.start << f.len << f.severity << f.children;
    return out;
}
QDataStream& operator>>(QDataStream& in, DissectField& f) {
    in >> f.name >> f.value >> f.start >> f.len >> f.severity >> f.children;
    return in;
}
```

**(c)** `ParseResult` 算子末尾追加（**写在最后，保持追加式布局**）：

```cpp
QDataStream& operator<<(QDataStream& out, const ParseResult& r) {
    out << ... << static_cast<quint8>(r.protocol)
        << r.dissect_fields;      // v7 新增字段
    return out;
}
QDataStream& operator>>(QDataStream& in, ParseResult& r) {
    in >> ... ;
    quint8 proto = 0;
    in >> proto;
    r.protocol = static_cast<ProtocolVariant>(proto);
    in >> r.dissect_fields;       // v7
    return in;
}
```

**(d)** `plugin_ipc.h`：`kWireFormatVersion` 6 → **7**，并在版本史注释补一行：

```
///          v7:ParseResult 增 dissect_fields(插件通用字段树)
```

> 版本不一致时握手直接拒绝连接（`plugin_manager.cpp:213`），这是设计如此：
> 新旧两端按不同布局静默错位反序列化比拒绝更危险。所以 host 与主程序必须同批构建。

---

## 5. 实施顺序（每步结束都能编译）

| 步 | 改动 | 说明 |
|---|---|---|
| 1 | `bplcframe.h`（DissectField + PacketEntry 字段） | 只加类型，无人使用，可编译 |
| 2 | `iprotocolparser.h`（ParseResult 字段） | 同上 |
| 3 | `protocol_tree_builder.*` 抽 `tree_apply_type_color` | 纯重构，先跑一遍测试确认零回归 |
| 4 | `protocoltree.cpp` 通用渲染 + 接入分派 | 此时功能即可用（插件填了就能画） |
| 5 | `mainwindow.cpp` 1 行拷贝 | 打通解析→条目 |
| 6 | `plugin_serialization.*` + `plugin_ipc.h` v7 | 打通插件→主程序 |
| 7 | `packetentry_serialize.h` + `block_version()` v10 | 打通换页 |
| 8 | 测试（§6） | |

---

## 6. 验证方案

### 6.1 必须全绿（回归）
```bash
cd samples/tree_dispatch_build && QT_QPA_PLATFORM=offscreen ./release/tree_dispatch_check.exe
```
现有 **18 条**断言（1 注册表 + 4 布局 + 6 逐帧切换 + 2 SOF 归属 + 5 通用渲染）**一条都不能变**：内置解析器 `dissect_fields` 恒空 → 走原 builder。

### 6.2 新增第 4 阶段（通用渲染）
`tree_dispatch_check.cpp` 追加：

```cpp
// 第四阶段:通用字段树渲染(插件解析帧)
{
    ProtocolTree t;
    PacketEntry e;
    e.index = 1; e.accepted = true;
    e.protocol = kProtocolGw2022;      // 借内置变体,但走通用渲染
    e.raw_bytes = QByteArray(64, '\0');

    DissectField grp(QStringLiteral("Payload"), QString(), 0, 64);
    grp.children.append(DissectField(QStringLiteral("MMe Type [8b]"),
                                     QStringLiteral("0x0C"), 4, 1));
    DissectField bad(QStringLiteral("Broken"), QStringLiteral("x"), 999, 8);  // 越界
    bad.severity = 3;
    e.dissect_fields = { grp, bad };

    t.show_packet(e);
    const QString d = dump_tree(t);
    // 断言 1:通用渲染生效(内置 GW_2022 构建器的特征 "Net ID" 不应出现)
    // 断言 2:三段字段都在
    // 断言 3:越界字段被降级为无映射(start<0)且不崩
}
```

断言要求：
- 输出**不含** `"Net ID"`（证明没走内置 builder）
- 含 `Payload` / `MMe Type [8b]` / `Broken`
- 进程不崩、退出码 0

### 6.3 端到端（可选，需插件）
用 `nw2021_parser` 参考插件改造一版：在 `parse()` 里填一段 `dissect_fields`，
按 `dissect_compare_test` 的思路把两侧 `ProtocolTree` 拍平比对（属于 Step 4 资产，不在本期）。

### 6.4 主程序
```bash
qmake BPLC_STA_Monitor.pro && mingw32-make -f Makefile.Release -j4
./release/BPLC_STA_Monitor.exe --replay samples/replay_test.bin    # 2457 帧, 0 崩溃
```

---

## 7. 风险与注意事项

### 7.1 坐标错位（中风险，可观测）
插件剥了前缀却没加回偏移 → Hex 高亮偏。§4.4 的越界守卫只能拦"越界"，
拦不住"整体偏移"。**约定写进头注释 + 插件文档**；必要时后续加
`dissected_bytes` 前缀自动换算（本期不做，避免引入新的不一致）。

### 7.2 插件 ABI 强制失配（预期，需通知）
`plugin_manifest.h` 的 `plugin_host_abi()` 把 `ParseResult` 的 sizeof 计入指纹 →
本次改动后**所有 native 插件需重编并 bump 版本**，否则被拦截加载。
这不是缺陷（旧插件填不了新字段），但必须同步：
- 通知 `nw2021_parser` 等示例插件重编
- 市场 `market.json` 的 `abi` / `min_app_version` 需要同步更新

### 7.3 两个版本号（都要动）

| 版本 | 位置 | 值 | 说明 |
|---|---|---|---|
| `kWireFormatVersion` | `plugin_ipc.h:29` | 6 → **7** | 传输新增 `dissect_fields`；同时删掉 3 个死字段 |
| `block_version()` | `packetentry_serialize.h:422` | 9 → **10** | 换页载荷删掉 3 个死字段（`pb_index`/`coord_option`/`ack_search_freq`）；`dissect_fields` **不进**盘块 |
| 插件包版本 | 各 `plugin.json` | 自行 bump | ABI 指纹变化见 §7.2 |

漏 `block_version` → 旧块被拒（显式）；漏 `kWireFormatVersion` → host 拒绝连接（显式）。
两者都是"改布局必须升版"，只是影响面不同。

### 7.3.1 为什么字段树不进盘块（实测依据）

`QTableView::resizeColumnsToContents()` 的代价链（本项目实测，见
`samples/autofit_cost_check.cpp` / `samples/autofit_real_check.cpp`）：

```
resizeColumnsToContents()
  → QHeaderView 逐列扫描 resizeContentsPrecision(默认 1000) 行
  → 每格 data() → locate() → BlockReader::entry_at() 解码整条 PacketEntry
  → 而 PacketEntry 内嵌 MsduInfo::tree（61~132 个节点、每节点 2 个 QString）
```

| 场景（20,000 条，14 列） | 耗时 | `data()` 回调 |
|---|---|---|
| 有字段树（100 节点），precision=1000 | **514 ms** | 112,000 |
| 无字段树，precision=1000 | 174 ms | 112,000 |
| 有树，precision=50 | **30 ms** | 5,600 |
| 有树，precision=-1（扫全部行） | **17,832 ms** | 2,240,000 |

单纯解码基准：14,000 次 `entry_at()` = 316 ms（22.6 µs/次，其中字段树占 66%）；
盘块体积 1000 条 = **720 KB（有树）vs 26 KB（无树），27×**。

结论：**字段树属于渲染派生数据，不该进落盘载荷**。插件帧的 `dissect_fields`
只会比内置树更大，若落盘就精确重演这一坑。

### 7.4 性能
`ParseResult` 每帧多一个空 `QVector`（24 B），`PacketEntry` 同理。
内置解析路径完全无堆分配；`FrameDispatcher` 的 queued 信号每帧多拷 24 B，可忽略。

---

## 8. 回滚

全部改动是**加法**，无行为删除。回滚顺序：
1. `protocoltree.cpp` 恢复为只调 `builder_for()`
2. 其余字段/算子保留也不会被使用（`dissect_fields` 恒空）

即：**只回滚 §4.4 一处即可关闭整个特性**，其余可留在代码里。

---

## 9. 与 dissector 分支的差异（有意为之）

| 他们 | 本方案 | 理由 |
|---|---|---|
| `DissectField` + `FrameContext` 一起下沉 | 只下沉 `DissectField` | `FrameContext` 属 Step 2 的 dissector 接口，现在加是死类型 |
| `ParseResult` 加 3 字段（含 column_values/color_id） | 只加 1 字段 | 另两个属列/着色能力，本期不引入 |
| `PacketEntry` 加 3 字段 | 只加 1 字段 | 同上 |
| `protocolfactory` 硬编码 `is_builtin_id()` | **不移植** | 我这边已是 provider 注册表；防影射应做成 provider 属性或两层表（Step 2） |
| `protocoltree.cpp` diff 基于旧 `set_variant` | 手工接在 `builder_for()` 之后 | 我这边已改为按 `e.protocol` 逐帧分派 |
