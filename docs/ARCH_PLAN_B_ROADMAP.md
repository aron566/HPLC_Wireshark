# 方案 B 路线图：插件 protobuf 契约 + 协议彻底解耦

状态：**⛔ 冻结中**（用户 2026-10 指示："arch 架构先等我通知再改"）
> 冻结范围：方案 B 全部阶段（B0–B6）、`MpduInfo` 裁剪（B4/B4a）、插件契约与线格式变更、
> 以及依赖它们的提交动作。**恢复施工必须由用户明确通知**，不得因"前置条件已满足"或"成本很低"自行启动。

决策来源：用户 2026-10 指示
前置条件见 §2（未满足不启动）
相关：`docs/ARCH_DISSECTOR_STEP1_PLAN.md`（Step 1 通用字段树，B 的前置）

---

## 1. 目标（一句话）

把"协议实现"从主程序里彻底外置：**插件接口是字节进 / protobuf 出**，
主程序不再认识任何具体协议的结构体，跨进程只传**公共标量 + 通用字段树**。

## 2. 触发前置条件（可核查，全部满足才启动）

| # | 条件 | 现状 | 核查方式 |
|---|---|---|---|
| 1 | 国网(HDC 2.0)、南网(SPLC) 各自帧型全集解析完成 | 进行中 | `docs/protocol_doc` 表号 ↔ parser 分支一一对应 |
| 2 | 每帧型字段树完成，且**坐标单一来源** | **未达成，且比原判断更糟：是「三写」** —— ①parser `get_bits` ②tree 重读 `raw_bytes`（NW 17–18 处）③`wireshark_support_plugins/*.lua` **10,042 行手写 ProtoField**（文件头自述"字段坐标对照 nw_2026_parser.cpp"） | `grep -c get_bits` vs tree 内 `raw_bytes` 直读 vs `.lua` 内 `ProtoField` / `read_bits` 坐标 |
| 3 | Step 1（`DissectField` + 通用渲染，不落盘）落地 | 方案就绪 | `docs/ARCH_DISSECTOR_STEP1_PLAN.md` |
| 4 | 黄金语料：每协议每帧型至少 1 条，可对拍 | 未达成：仅 `samples/replay_test.bin` 2457 帧 | 覆盖率统计脚本 |
| 5 | `MpduInfo` 序列化漂移**收口**（当前漏 18 字段） | 未达成 | 按 §3.2：确认这 18 个均属 B/C 桶后**删除**，**不补齐** |

> 条件 2 和 4 最容易糊过去。**字段树坐标双写不解决，B 之后会变成"插件和主程序各写一遍"，问题只是换了位置。**

## 3. MpduInfo 怎么改：分离 ≠ union（关键结论）

用户指示"各协议分离或使用 union"。这两条**互斥**，而且在本架构里
**方案 B 一旦成立，union 就失去存在理由**：

- `union` 的用途是"**让四个协议的私有字段都住进同一条内存记录**"。
- 方案 B 的用途恰恰是"**私有字段根本不进那条记录**"——它们在插件进程里
  的临时结构里活着，出进程前就变成 `DissectField` 树。

所以正确形态是**分离**，而且分离的落点在"记录层"：

| 层 | 内容 | 生存期 |
|---|---|---|
| 插件内（各协议目录） | 协议私有结构（`Gw2026Sof`、`Nw2022Ack`…）| 单次 `dissect()` 调用 |
| 跨进程 / 记录层 | `DissectField` 树 + **公共标量**（~47 项）+ 协议 id | 帧的生命周期 |
| UI | 字段树按 `e.protocol` 渲染 | — |

`PacketEntry` 是每秒数百帧、还要落盘的结构，**不能放指针/多态成员**，
所以"各协议分离"在记录层只有一个可行形态：公共标量 + 字段树 + 协议 id。

### 3.1 `MpduInfo` 逐字段审计（75 → 11）

**判据（两条同时满足才留）**：
① 出现在 ≥2 个协议的同一语义位置；
② 有**记录级消费者**真的读它 —— 列表列 / 着色 / 过滤(`make_search_text`) /
   导出 / Hex 坐标映射(`protocol_tree_builder`) / 已发布的插件 API。

**口径**：`src/{ui,app,common/bplcframe.h 的 make_search_text·dir_display·frame_type_name,
protocol/*.cpp 框架层,plugin/{api,host,app_integration}}` 记为消费者；
**排除** `plugin/ipc/`、`common/packetentry_serialize.h`（序列化不是语义消费，
否则 75 个字段全部"被使用"）；`plugin/host/{lua,js}_backend.cpp` 记入
"插件 API 暴露面"而非主程序消费。

#### A1 主程序消费 —— 必须进记录（11 个）

| # | 字段 | 类型 | 消费者（代表行） |
|---|---|---|---|
| 1 | `frame_type` | quint8 | `packetlistmodel.cpp` 列/着色 + `bplcframe.h` `dir_display()`/`make_search_text()` |
| 2 | `net_id` | quint32 | `packetlistmodel.cpp:480`、`make_search_text()`、`mainwindow` 过滤 |
| 3 | `src_tei` | quint16 | `data(COL_SOURCE)`、`make_search_text()` |
| 4 | `dst_tei` | quint16 | `data(COL_DEST)`、`make_search_text()` |
| 5 | `tmi` | quint8 | `packetlistmodel.cpp:199`、`mainwindow.cpp:662`（INFO 列） |
| 6 | `pb_num` | quint8 | `packetlistmodel.cpp:199` + `protocol_tree_builder.cpp:10,14,15`（坐标映射） |
| 7 | `pb_size` | quint16 | `protocol_tree_builder.cpp:15,37`（PB 内偏移 → Hex 高亮） |
| 8 | `ack_rx_res` | quint8 | `packetlistmodel.cpp:237`（ACK 校验失败红色） |
| 9 | `ack_rx_status` | quint8 | `packetlistmodel.cpp:236`（GW_2026 判失败） |
| 10 | `ack_rx_pb_num` | quint8 | `packetlistmodel.cpp:236` |
| 11 | `beacon_cco_mac` | quint64 | `packetlistmodel.cpp:480-481`（TEI→MAC 表建表） |

#### A2 插件 API 暴露面 —— 进契约、不进记录（3 个）

`net_type`、`fch_crc_ok`、`pb_crc_ok` —— 四协议都解析、都通过
`lua_backend.cpp:575-579` / `js_backend.cpp:294-296` 摊给脚本。
主程序零消费，但删了会**破坏已发布脚本插件的 API**，故进 proto 契约。

#### B 多协议但仅树/解析 —— 删除（43 个）

`version`、`link_id`、`frame_len`、`symbol_num`、`mcs`、`tmi_ext`、`pb_num_field`、
`pblen`、`bc_flag`、`re_send_flag`、`encryp_flag`、`pb_head`、`pb_heads`、`pb_crc_oks`、
`beacon_timestamp`、`beacon_line`、`beacon_rsv`、`beacon_type`、`beacon_netsn`、
`beacon_period_cnt`、`beacon_rf_channel`、`beacon_rf_option`、`beacon_item_num`、
`coord_duration`、`coord_shift`、`coord_neighbour_nid`、`coord_rf_channel`、
`coord_rsv0`、`coord_rsv1`、`coord_rsv2`、`coord_rsv3`、`coord_band_end_flag`、
`coord_band_end_offset`、`coord_band_start_offset`、`ack_ext_type`、`ack_rsv0`、
`ack_channel_quality`、`ack_sta_load`、`ack_rsv1`、`ack_dst_addr`、`ack_search_tei`、
`ack_sync_timestamp`、`ack_sync_tei`

其中 `version` 是**解析期分派**用（`nw_2026_parser.cpp:149 if (r.mpdu.version != 2)`），
`frame_len`/`pb_heads`/`pb_crc_oks` 等只在 `*_tree.cpp` 里渲染用 → 全部归插件内部。

#### C 单协议专属 —— 删除（15 个）

`sof_data_type`、`sof_tf_symbol_pos`、`sof_tf_symbol_num`、`sof_fch_sym_num`、
`sof_mod_mode`、`sof_mod_param`、`sof_band`、`sof_gain`、`sof_load_table_id`、
`beacon_fch_sym`、`beacon_mech_state`、`beacon_discover_en`、
`ack_bru_snr_flag`、`ack_sat_id_flag`、`ack_remain_sta_num`

#### D 死字段 —— 立即删（3 个）

`pb_index`、`coord_option`、`ack_search_freq` —— 除序列化算子外**零引用**
（`pb_index`/`coord_option`/`ack_search_freq` 在 `src/` 下各只有 write/read 两处）。

### 3.2 那批"漏序列化 18 字段"：分两类，不能一刀切

初版结论是"18 个都不用补"。**code-review-bot 复核推翻了一部分**：

| 类别 | 字段 | 处理 |
|---|---|---|
| **树在用、必须补** | `coord_rsv1`、`coord_rsv2`、`coord_rsv3` | **已补**（`write_mpdu`/`read_mpdu`，计入 block v10）。依据：`nw_2021_tree.cpp:428/430/440/442/446` 渲染这三个字段，而原先只有 `coord_rsv0` 落盘 → **换页读回后 RSV1/RSV2/RF Option 恒显示 0，静默丢数据** |
| 单协议专属、将删除 | 15 个（`sof_*`、`beacon_fch_sym`、`beacon_mech_state`、`beacon_discover_en`、`ack_bru_snr_flag`、`ack_sat_id_flag`、`ack_remain_sta_num`） | **不补**，B4 时随 C 桶整体删除 |

> 教训：判断"某字段该不该进记录"不能只看它属哪个协议桶，还要看**当前是否有消费者**。
> `coord_rsv1/2/3` 属于 B 桶（未来会删），但**在删除之前它是活的静默 bug**——"终局不需要"不等于"现在不用管"。

### 3.3 量化结果

| 指标 | 现在 | B4 之后 |
|---|---|---|
| 字段数 | 75 | **11**（+3 契约字段） |
| `sizeof(MpduInfo)` | **192 B**（实测） | **≈24 B**（11 个标量；含契约 3 字段 ≈32 B） |
| 堆容器 | `pb_heads`(QVector<quint8>)、`pb_crc_oks`(QVector<bool>) | **0**（每条目少 2 次堆分配） |
| `write_mpdu`/`read_mpdu` 字段数 | 各 75 | 各 11 |
| `plugin_serialization` mpdu 段 | 75 字段 | 11 字段 |

### 3.4 `extras[8]` 不需要了（撤回前一版设想）

上一版为"单协议但被 UI 用到"预留了 `extras[8]`。实测这类字段**一个都不存在**：
原以为要用它的 `sof_band`/`sof_gain`/`ack_rx_status`，前两个只在 GW_2026 树里渲染（C 桶），
`ack_rx_status` 则四协议都有同义字段 → 已提升为 A1 公共字段。

**记录层不需要任何"按协议分区"的扩展槽。**

### 3.5 施工顺序（删之前必须先做，否则树空白）

现在 `*_tree.cpp` 是**直接读** `e.mpdu.pb_heads` / `e.mpdu.sof_band` 等字段的
（GW 3 处、NW 17-18 处同时还有 `get_bits` 重读 `raw_bytes`）。删除字段前必须先把
这些树改为**只从 `raw_bytes` 重建**，否则：

1. 字段删了 → 树读不到 → 显示空白/0
2. 换页读回的条目更糟（字段本就没落盘）

即：**§2 前置条件 #2（坐标单一来源）是 §3 的前置**，不是并列项。

### 3.6 什么情况下才允许 union

只有一条：**成员全是 trivially destructible 类型**（POD）。
本项目不满足——`MpduInfo` 里有 `QString`/`QVector`，裸 union 就是 UB（构造/析构/活跃成员判断全无保障）。
若要"带标签的合体"，正确工具是 `std::variant`，它的代价是：插件 ABI 与主程序结构体永久绑死，
这正是方案 B 要消除的东西。**所以推荐：不用 union，也不引入 `std::variant`。**

## 4. 目标架构（采用 `origin/feat/plugin-dissector-arch`）

取其 6 层能力，全部经过"去掉硬编码"的改造：

| 层 | 内容 | 移植注意 |
|---|---|---|
| 1 通用结果类型 | `DissectResult{ fields, columns, color_id, refined_ctx }` | `DissectField` 已在 Step 1 落地，此处只加 columns/color |
| 2 通用渲染 | 字段树 → QTreeWidgetItem | Step 1 已接在 `builder_for(e)` 之后，直接复用 |
| 3 列契约 | `DissectColumn{id,title,width,align,visible}` + `column_values` | **侵入 `packetlistmodel` + `mainwindow`**，冲突最大，放最后 |
| 4 着色契约 | `DissectColorRule` / `color_id` | 依赖列契约 |
| 5 生命周期 | 插件加载/卸载/崩溃回退 | 分支比本分支完整（native DLL 卸载修复） |
| 6 插件总线 | `IPluginBus` 多插件协作 | 可延后 |

**明确不移植**：其 `protocolfactory.*`（`is_builtin_id()` 硬编码 `"GW_2022"||"NW_2021"`、
`protocol_from_key` 前缀兜底）。本分支已是 `IProtocolProvider` 自注册，
防影射应做成 provider 属性或两层表。

## 5. 编码层（protobuf）绑定位置

```
app ──protobuf──> bplc-plugin-host ──C++/Lua/JS──> 插件
      (L2 线格式)                  (L1 插件契约: 若 native 仍用 C++ 结构体,
                                    则插件仍需随主程序重编,B 的价值归零)
```

**只有 L1 也改成 protobuf，方案 B 才有意义**（否则只是换了线格式，插件照样重编）。
`.proto` 草案与三个设计要点见对话记录：
1. `raw_wire` 现在 request/response 各传一份，改后响应不回传
2. `MpduInfo` 不能整包进 proto，只放"跨进程契约子集"（正好由 §3.1 的裁剪决定）
3. 只追加字段、不复用已删号 → 靠 proto3 unknown fields 兼容，不再需要 `kWireFormatVersion` 整版拒绝

工程前置：`3rdparty` 无 protobuf，MinGW 13.1 需自建 + qmake 代码生成 + 三平台打包脚本。

## 6. 分阶段（每阶段可停可回滚）

| 阶段 | 内容 | 可验证结果 | 状态 |
|---|---|---|---|
| B0 | 前置条件 §2 全部达成 | 覆盖率/坐标单一来源/漂移清零 | 未启动 |
| B1 | Step 1 落地（通用字段树，不落盘） | 内置解析零回归，插件帧能画树 | ✅ **已完成**（回归 20/20） |
| B2 | `IDissectorPlugin`（字节→通用字段），编码仍用 QDataStream | 插件协议不再依赖 C++ 结构体；ABI 指纹可删 | 未启动 |
| B3 | 线格式换 protobuf（L2） | 插件无需随主程序重编 | 未启动 |
| B4 | `MpduInfo` 裁剪：删 B/C 桶 58 个字段，公共层只留 11 个（§3.1 审计） | 字段 75 → 11；`sizeof` 192 B → ~24 B；`extras[8]` **不需要** | 未启动 |
| B5 | 列契约 + 着色契约（arch 分支 Layer 3/4） | 列表列由插件定义 | 未启动 |
| B6 | 插件总线 / 生命周期加固 | 多插件、崩溃回退 | 未启动 |

顺序不能颠倒：**B2 先于 B3**（先解契约再换编码，否则插件要改两次）；
**B4 在 B3 之后**（proto 会强迫你把 `MpduInfo` 裁成契约子集，裁剪一次到位，别改两遍）。

> **B4a（树改只从 raw_bytes 重建）为什么也等 B2** —— 2026-10 冻结期决策输入：
> B2 的 `IDissectorPlugin` 对 `*_tree.cpp` 走的是**原位改造**（把 `tree_add_bit_field(...)`
> 的输出换成 `DissectField` 推送，坐标表原地保留），**不是整体重写**。
> 因此若现在做 B4a，等于把"parser 一份 + 原树一份 + 新树一份"的坐标三写**永久固化进
> DissectField 时代**；正确做法是让 `DissectField` 由"`raw_bytes` 一次性解出"的单一来源生成，
> 双写自然消失。→ **B4a 与 B4 一并冻结，等 B2 启动时一并裁决**（①推后 / ②`MpduPublic` 视图
> 仍可选；③硬裁大概率与 B2 合并成一次改造）。

### 6.1 阶段外小修（不占阶段号，可随时做）

有些修复不依赖任何阶段，不要因为"等 B2/B4"而拖着：

| 项 | 说明 | 状态 |
|---|---|---|
| 插件帧协议位（`kProtocolPluginBit = 0x80`） | 插件帧翻页后 `dissect_fields` 为空会退回画内置布局。**不需要 `IDissectorPlugin`**，是独立小修 | 已派工单 `docs/WO_B2_plugin_protocol_bit.md` |
| 3 个死字段删除 + `coord_rsv1/2/3` 补序列化 + SNID 无线高位 | 见 `docs/ARCH_ANOMALY_LEDGER.md` A1–A4 | ✅ 已完成 |

> 命名提醒：台账里曾把协议位写成"B2 阶段"，容易误读成必须先有 `IDissectorPlugin`。
> 实际上它只是"插件帧不再被当内置帧渲染"的一处补丁，与 B2 无依赖关系。

## 7. 风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 字段树坐标**三写** | parser / tree / **Wireshark Lua（10,042 行）** 各存一份位段坐标；任一方改动（如本轮 SNID 无线高位）另两方不会自动跟 | 前置条件 2 必须真解决；Lua 与 C++ 目前**无自动化一致性校验** |
| Lua 侧游离于架构之外 | 方案 B 只规划了 C++ 主程序与插件，**未把 Wireshark dissector 纳入**。若坐标改为声明式表，Lua 可从同一份表生成，否则永久手写维护 | B0 评审时把"Lua 生成"列为候选，而非事后追加 |
| `PacketEntry` 落盘格式再变一次 | B4 裁剪会改块载荷 | 字段树不落盘（Step 1 已定），标量裁剪升 `block_version` |
| 插件 ABI | B2 删 `plugin_host_abi()` 的 sizeof 指纹是**目的**，但老插件会失去保护 | 用 proto schema 版本替代 sizeof 指纹 |
| 三平台 protobuf 构建 | MinGW 无预编译包 | 单独排期，别混在 B3 里 |
| `PacketEntry` 每秒数百帧 | 禁止指针/多态成员 | §3 的记录层形态已约束 |
