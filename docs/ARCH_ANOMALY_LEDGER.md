# 现存异常台账（复核闭环）

范围：`feat/next-gen-protocol` 工作区当前状态 —— Step 1（插件通用字段树接入 + 3 个死字段删除）
实施后暴露/确认的全部异常，以及 **code-review-bot 两轮复核** 的处置结论。

建立时间：2026-10-22
复核轮次：2 轮（7a–7e 清单 + 7e 补充证据）

---

## 1. 已修（本轮）

| ID | 异常 | 位置 | 等级 | 处置 | 验证 |
|---|---|---|---|---|---|
| **A1** | `coord_rsv1/2/3` 字段树在用，但 `write_mpdu` 只写 `coord_rsv0` → **换页读回后 RSV1/RSV2/RF Option 恒显示 0**（静默丢数据） | 树用：`nw_2021_tree.cpp:428/430/440/442/446`；缺写：`packetentry_serialize.h` | P1 | `write_mpdu`/`read_mpdu` 补写补读，计入 block **v10**（v10 本轮新定义、未发布，故不额外升版） | 块往返 120 条跨 3 chunk，逐字段比对含这三个字段 → PASS |
| **A2** | SPLC **无线 SOF** 的 SNID 高位未合并进 `net_id` → SNID≥16 的帧过滤/列显示错 | `nw_2026_parser.cpp:164-166` | P1 | `case 1` 改为无条件 `snid_high = fch[12] & 0x01`（删 `if (!is_rf)`） | `[SNID merge]` 解析层断言：bit0=1 → `net_id=0x000010`；bit0=0 → `0x000000` → PASS |
| **A3** | 无线 SOF 字段树把 `(12,0,4)` 整段标 `RSV`，SNID 高位被吞进保留位（同文件信标/载波分支都是 `SNID High(12,0,1)`，三处不一致） | `nw_2026_tree.cpp:213` | P1 | 弃用 `tmi_ext`，改读 raw 拆 `SNID High(12,0,1)` + `RSV(12,1,3)` | `[RF SOF SNID]` 树断言 → PASS |
| **A4** | 通用渲染越界守卫 `s + l` 为 int 加法，插件传 `INT_MAX` 溢出变负绕过守卫 → 高亮越界 | `protocoltree.cpp:78` | P1 | 改 `qint64(s) + qint64(l) > qint64(raw_bytes_size)` | 越界用例仍降级为 `-1`，其余 20 条断言全过 |

**A2/A3 根因**：`nw_2026_parser.cpp` 由 `nw_2021_parser.cpp` 复制而来，只把载波坐标从 BPLC 表20 的 `(5,0,1)` 改成 SPLC 表23 的 `(12,0,1)`，**把"无线无高位"这句原样留下了** —— 而 BPLC 表131 无线确实没有高位，SPLC 表45 有。`nw_2021_parser.cpp:151` 至今仍是 `if (!is_rf) ... fch[5]`，可作交叉佐证。

## 2. 关闭（判断为不需要改）

| ID | 异常 | 位置 | 结论 | 依据 |
|---|---|---|---|---|
| **B1** | `locate()` 写在 role 判断之前，`SizeHintRole` 白付一次整条解码 | `packetlistmodel.cpp:86` | **P2 代码整洁，不是性能修复** | 实测：`SizeHint` 26~32 µs/次就是该行唯一一次真解码；移进 `DisplayRole` 只是把同一笔挪位置，**14,000 次解码总数不变**。其余 7 个 role 各 0.2~1.8 µs（单槽缓存命中）。复核方已接受此反驳 |
| **B3** | `pb_heads` 只有国网写、南网不写 | — | **不改**（不对称但无功能缺失） | 南网 `nw_2021_tree.cpp:281-282` / `nw_2026_tree.cpp:410-411` 的 PB 块渲染直接从 raw 读块头与 CRC，不依赖 `pb_heads`。注：`pb_crc_oks` 南网**是**写的（`nw_2021_parser.cpp:231`、`nw_2026_parser.cpp:285`） |
| **B6** | 15 个单协议专属字段 `write_mpdu` 未序列化 | `packetentry_serialize.h` | **不补** | 均属 C 桶（`sof_*` / `beacon_fch_sym` / `beacon_mech_state` / `beacon_discover_en` / `ack_bru_snr_flag` / `ack_sat_id_flag` / `ack_remain_sta_num`），B4 随裁剪整体删除。**注意与 A1 的区别**：A1 那 3 个字段树当前在用，属"活的静默 bug"，必须补 |

## 3. 挂起（排到后续阶段）

| ID | 异常 | 位置 | 等级 | 排期 |
|---|---|---|---|---|
| **B2** | `pb_head` 有写、有序列化，**全局零业务读** | `gw_2022_sof_parser.cpp:106` / `gw_2026_sof_parser.cpp:157` | P2 | B4 随裁剪删（现在仍在 block v10 载荷里） |
| **A5** | 插件帧的 `PacketEntry::protocol` 落回内置 0-3；换页后 `dissect_fields` 为空 → `builder_for()` 画**内置布局**，用户可能误读 | 映射 `mainwindow.cpp:912`；置位 `plugin_serialization.cpp:217`；判位 `protocoltree.cpp:105`（builder_for）/`:160`（show_packet 三态）；常量 `bplcframe.h:340` | P1（设计缺口） | **已修**：`kProtocolPluginBit = 0x80` 占 `protocol` 高位（不改 block 布局、不升版本）；置位在 IPC 反序列化单点；`builder_for()` 见位返回 `nullptr`；`show_packet()` 三态（通用树 / 插件位空树→提示节点 / 内置构建器）。映射收敛为 `entry_protocol_id()`（`iprotocolparser.h`）。**掩位比较共 3 处代码 + 1 处映射内掩位**：`mainwindow.cpp:931`、`bplcframe.h:412`、`packetlistmodel.cpp:235` | 26/26（含 `[plugin page-out]`、`[plugin re-dissect]`、`[proto map]` 4 条） |
| **B5** | `tmi_ext` 同名不同义：载波 = 扩展载波映射表索引（`nw_2026_parser.cpp:261-262` 用于算 `pb_size`）；无线 = SNID 高位 + 保留 | `nw_2026_parser.cpp:216` | P2 | 已在该行注释写明双关语义；B4 裁剪后两种用法都进插件内部，语义随帧型解释 |

## 4. 复核过程中双方各自纠正的错误（留档，防重犯）

| 谁 | 说法 | 更正 |
|---|---|---|
| 复核方 | 7a `locate()` 移进 role 分支"省约 85% 自动列宽耗时" | **错**。总解码次数不变 → 总耗时不变。已改为 P2 |
| 复核方 | 载波 `tmi_ext` 消费者引 `gw_2022_pb_table.h:40-45` / `gw_2022_sof_parser.cpp:16-23` | 那是**国网**例证；南网实际消费者是 `nw_2026_parser.cpp:261-262` |
| 我 | "`pb_heads`/`pb_crc_oks` 只有国网写、南网完全不写" | **错**。`pb_crc_oks` 南网也写；只有 `pb_heads` 是国网独占。原因是只扫 `=` 赋值没扫 `.append()` |
| 我 | "漏序列化的 18 字段都不用补" | **部分错**。`coord_rsv1/2/3` 树在用 → 是活的静默 bug（见 A1）。**"终局不需要"≠"现在不用管"** |
| 我 | 承诺"SNID≥16 无线 SOF 帧 `net_id` 高位非 0"断言 | 首次只做了树层（FCH CRC 不过会早退，伪造帧到不了合并块）。后经 `from_raw` 快捷路径 + `crc24_lsb` 构造合法帧补上（见 A2） |
| **我（工单缺陷）** | A5 工单 §2.5 只列了 3 个**比较点**（`mainwindow.cpp:935` / `bplcframe.h:409` / `packetlistmodel.cpp:235`），**漏了 `mainwindow.cpp:911` 那个重建 `e.protocol` 的 `switch`** | **这是 A5 缺陷的真正源头**。实施方"与工单完全吻合"属实 —— 它照做了，是我没指出重组点。教训：**写工单不能只 grep 比较点（`==`），必须追数据流**——找出所有"给目标字段赋值/重建"的位置，那里才是位丢失的地方 |
| 实施方 | 报告 A5 完成"22/22 全过" | **假绿**（不是实现错，是测试选点错）。两条断言直接构造 `PacketEntry`，绕过 `make_entry` 的映射，所以生产路径上插件位被 switch 丢弃这件事测不出来 |
| 我（流程教训） | 只按实施方给的"测试通过"就准备收尾 | **单元测试必须覆盖"数据从生产路径流到消费路径"的接缝**。直接构造消费端结构体的测试，只能证明消费端逻辑对，证明不了生产端会喂给它正确的值。A5 的 `[proto map]` 4 条就是补这个接缝 |

## 5. 验证基线（改动后）

| 项 | 结果 |
|---|---|
| `tree_dispatch_check` | **26/26 PASS** |
| 块往返（block v10） | PASS，120 条跨 3 个压缩分块 |
| 主程序构建 | exit 0，`BPLC_STA_Monitor.exe` 104,223,017 B |
| 回放 | 2457 帧 / 50 ms，崩溃关键字 0 |

> 复核方早期消息里写的"回归 19/19"是旧数；随后 20/20 是 Step 1 阶段；**当前基线 26/26**（A5 后新增 `[plugin page-out]`、`[plugin re-dissect]`、`[proto map]` ×4）。
