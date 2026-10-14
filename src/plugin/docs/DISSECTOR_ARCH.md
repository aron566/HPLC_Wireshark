# 插件协议解析架构（Dissector Architecture）

状态：feat/plugin-dissector-arch 分支实现中。本文是该分支的设计契约，
实现与测试均以此为准。

## 背景与问题

旧架构有两套并行加载路径同扫 `<exe>/plugins`：

- PluginManager（Phase1）：每插件拉起一个 bplc-plugin-host 子进程，
  经 IPC 做协议解析（PluginParserProxy 注册进解析器工厂）；
- LocalPluginEngine（后期）：进程内加载同一批插件驱动面板。

由此产生三个实际问题：

1. 同一 DLL 被加载两份；市场更新/卸载的卸锁回调只覆盖进程内引擎，
   host 子进程常驻映射 DLL → Windows 下删不掉旧版
   （`cannot remove old version`，2026-10-11 实测定位）。
2. 「插件作协议解析」形同虚设：协议下拉框只有国网/南网，插件协议
   只能手改 config.ini 选中；且启动时分发器创建早于插件注册，
   选了也静默回退国网。面板类插件（如 cpp-coverage，其 parser
   注释自述「只建模，不做协议解析」）一旦真被选为解码器会产生
   空心解析（无条件 accept、mpdu 全零、无 topo 事件）。
3. PluginManager 没有任何停机接口；主程序异常退出时 host 子进程
   只能靠 socket 断开自杀，缺少主动控制与 OS 级兜底。

## 新架构总览

统一为「解剖器（dissector）」模型，参考 Wireshark：

```
串口/回放 ──去封装(去 0x3C/0x3E 哨兵+反转义)──► BplcFrame.data
        │
        ▼ FrameDispatcher（工作线程，不阻塞 UI）
IProtocolParser::parse(BplcFrame, MsduState, filter)
        │  内置 GW_2022 / NW_2021，或插件协议 → PluginDissectorProxy
        ▼ IPC（DissectRequest/Response）
bplc-plugin-host ── IDissectorPlugin::dissect(payload, FrameContext)
        │  插件返回 DissectResult（结构化字段，见下）
        ▼
ParseResult ──► make_entry ──► PacketEntry（列表/过滤/topo/面板/块存储
                              全部沿用既有契约，不感知解析来源）
```

要点：

- 喂给插件的只有**去封装后的净荷字节**与一份 **FrameContext**
  （主程序告知的帧外信息：时间戳、通道、方向等）。其中时间戳与
  通道类字段为**宿主必填**：reader 切帧时即按信封布局填入
  BplcFrame.meta（NTB 在帧体 [2..5]、媒介头在 [6..9]、epoch_ms
  取帧时间——回放时间以文件基准时间+NTB 差推进得出，只有 reader
  知道），协议解析器仅以同口径复核；插件可用可不用这些值，但
  宿主不可缺省为 0。
- 插件返回**通用结构化结果** DissectResult：列表标量 + 通用字段树 +
  topo 载荷。主界面用通用渲染器把字段树画进详情视图，行为与内置
  解析器（tab 视图、Hex 高亮联动）一致。
- 只有在清单里显式声明 `"dissector": true` 的插件才注册进工厂、
  才出现在协议下拉框中——杜绝面板插件被误选为空心解码器。

## 数据类型（src/plugin/plugin_api/dissect.h）

```cpp
struct FrameContext {          // 主程序 → 解析器（宿主必填，解析器可忽略）
    qint64  epoch_ms;          // 帧时刻（PC 时间；回放=文件基准+NTB 推进）
    qint64  arrival_us;        // 单调 µs 时刻（实时串口）
    quint32 ntb;               // 帧内 NTB tick（reader 自帧体 [2..5] 填）
    bool    is_rf;             // 通道：true=HRF(无线) false=HPLC(载波)
    int     channel;           // 物理通道号（reader 自媒介头填）
    quint64 frame_index;       // 解析线程单调计数（非条目序号：暂停丢弃/
                               // 分发器重建会与条目序号漂移，勿对齐 jumpToFrame）
    bool    seg_start;         // 是否分段起点（回放分段）
};

struct DissectField {          // Wireshark 风格字段节点（递归）
    QString name;              // 字段名（可带 [Nbit] 标注，与旧约定一致）
    QString value;             // 格式化值文本
    int     start;             // 在去封装净荷中的字节偏移，-1=无映射
    int     len;               // 覆盖字节数
    quint8  severity;          // 0=无 1=提示 2=警告 3=错误（expert info）
    QVector<DissectField> children;
};

struct DissectResult {
    bool    accept;
    QString reject_reason;
    MpduInfo mpdu;             // 列表/统计/过滤所需的公共头标量
    bool    msdu_present;
    QString msdu_summary;      // 概要文本（进 search_text）
    QByteArray msdu_body;      // 重组后的 MSDU（若有）
    int     msdu_raw_base;     // msdu_body[0] 在 dissected_bytes 中的偏移，-1=跨帧
    TopoEvent topo_event;      // topo 载荷（kind=Other 表示无）
    QVector<TeiMacPair> tei_mac_pairs;
    QVector<DissectField> fields;  // 协议段字段树（Frame/Physical 公共段除外）
    QByteArray dissected_bytes;    // 解剖器实际解剖的字节（自行剥除媒介/传输
                                   // 前缀后）；空则宿主回退用输入 payload
    bool has_refined_ctx;          // 是否回传精化帧外信息
    FrameContext refined_ctx;      // 精化值（ntb/channel/is_rf 覆盖宿主粗值）
    QMap<QString, QString> column_values;  // 本帧列文本值（键=列能力 id）
};

struct DissectColumn {         // 解剖器声明的一列展示/导出能力
    QString id;                // 稳定列 id（column_values 的键，唯一）
    QString title;             // 列标题（默认语言）
    QString title_en;          // 英文标题（英文界面/CSV 表头优先）
    int     width;             // 建议初始列宽
    quint32 alignment;         // Qt::Alignment 位掩码
    bool    default_visible;   // 是否默认显示（表头右键可调）
};
```

列能力契约（2026-10-13）：主界面不假定插件协议沿用内置列集。解剖器
经 `IDissectorPlugin::dissector_columns()` 声明自己能提供的列，握手
（Hello）时随能力位一并上报，管理器缓存、主界面在协议激活时读入
PacketListModel：`# / Time / Delta` 三列为宿主固定列（时间轴不属于
任何协议），其余列按声明顺序显示，用户可在表头右键菜单增减可显示
列。每帧文本由插件在 DissectResult::column_values 中按列 id 返回，
随 PacketEntry 落盘（块格式 v5）。CSV 导出复用导出快照中的同一组
可见列与同一批值——插件依据自己的字段响应导出，宿主不再用内置
MSDU 字段替插件拼列。参考解剖器声明与内置列一一对应的 11 列并在
插件内复刻渲染（含自持 TEI→MAC 学习表），故其列表/CSV 与内置模式
逐格一致（export_consistency_test 逐列校验）。

着色契约（2026-10-13）：着色配置与列能力同构。解剖器经
`IDissectorPlugin::dissector_color_rules()` 列出每一种报文的颜色
设置项（`DissectColorRule{id, name, name_en, foreground,
background}`，颜色为 ARGB、0=不指定），握手时随列能力一并上报，
管理器缓存、主界面在协议激活时读入 PacketListModel。每帧命中
的规则由插件在 DissectResult::color_id 中按规则 id 返回，随
PacketEntry 落盘（块格式 v6）；主界面按 id 查表着色整行（前景
优先、背景仅插件可声明），未命中/插件未声明时回退内置着色
逻辑。参考解剖器的默认清单逐项复刻主界面当前着色（MMe 按类型
逐一列项、暖色系哈希公式；BID 按值域逐一列项、冷色系公式，
确认亮绿/否认红；帧型基色 7 项），逐帧选择优先级与内置同序
（MMe > BID > 帧类型），故插件模式整行颜色与内置模式逐帧一致
（export_consistency_test 第 4 节逐帧比对前景色）。南网 MMe 为
16 位值域：低字节全段（0x0000–0x00FF，含实测出现的未命名
0x0081）逐一列项，高字节未定义取值回退内置逻辑。

坐标约定：DissectField 的 start/len 以 **dissected_bytes**（解剖器自行
剥除媒介/传输前缀后的字节，与内置解析器的 payload_for_log 同义，
PacketEntry 的 raw_bytes 即此缓冲）为坐标系，与内置解析器的高亮坐标
一致，HexView 联动无需换算。注意输入 payload（BplcFrame.data）含媒介
头，与坐标系相差前缀长度，二者不可混用。

精化帧外信息（可选纠正通道）：reader 已按信封必填 NTB/通道/RF
（见上），故 refined_ctx 不再是必需路径；仅当解剖器的信封理解与
宿主不同（如私有前缀）时才经此回传覆盖，保证 Physical 段展示
以解析方口径为准。参考插件回传的即解析器复核值，与 reader 必填
值恒等（对比测试逐帧断言）。

## 插件接口（native）

```cpp
class IDissectorPlugin {       // src/plugin/plugin_api/idissectorplugin.h
public:
    virtual ~IDissectorPlugin() = default;
    virtual QString dissector_protocol_id() const = 0;
    virtual QVector<DissectColumn> dissector_columns() const { return {}; }
    virtual DissectResult dissect(const QByteArray& payload,
                                  const FrameContext& ctx) = 0;
    virtual void dissect_reset() {}   // 新采集/重放时主程序要求清状态
};
```

- 跨帧状态（如 MSDU 重组）由插件实例自持（host 进程内），
  DissectRequest 不再往返 MsduState（与旧 ParseRequest 的区别）。
- NativeBackend 若插件同时实现 IDissectorPlugin 则提供 dissect 通路；
  host 收到 DissectRequest 时调用之。脚本（JS/Lua）解剖器为后续工作，
  本期仅 native。
- 参考实现：`src/plugin/examples/{gw_dissector,nw_dissector}/` 两打包
  共用 `dissector_common/` 一份实现（protocol_id 分别为 GW_2022_PLUGIN /
  NW_2021_PLUGIN，避免覆盖内置注册）：内部复用内置 GW_2022/NW_2021
  解析器产出 ParseResult，再由字段生成器把结果转成 DissectField 树
  （MPDU/FCH/PB 段 + MSDU 段）。一致性由
  `src/plugin/tests/dissect_compare_test.cpp` 验证：真实抓包全帧标量
  比对 + 抽样字段树经同一 ProtocolTree 渲染后逐行比对（NW 13568 帧 /
  GW 320290 帧实测全一致）；跨 PB 块字段的高亮只取首片段为主区间
  （内置构建器是多片段），"无区间"节点的 -1 与内置 0 表示等价
  （len=0 不高亮任何字节）。端到端 IPC 由 dissect_ipc_test.cpp 验证。

## 主程序接入

- PluginDissectorProxy（manager）：实现 IProtocolParser，经
  DissectRequest/Response 同步 IPC 调用 host；超时/失败 →
  accept=false 且 reject_reason 注明（不挂死分发线程）。
- 注册时机：PluginManager 仅对 `dissector:true` 的插件在握手成功后
  register_parser。MainWindow 订阅 manager 的注册信号：若当前配置
  协议是尚未注册的插件协议，分发器先回退国网，注册到达后在空闲时
  自动重建分发器切换过去（解决启动竞态）；协议被注销同理回退。
- main.cpp 调整顺序：PluginManager::loadAll 提前到 MainWindow 构造
  之前，让 host 进程与 UI 构造并行启动。
- 协议下拉框（通信设置）：选项 = 内置协议 + 已注册的 dissector
  插件协议（显示名取清单 display_name）；当前配置值若不在列表中
  （插件未加载）仍显示该项并标注未加载。
- 渲染：PacketEntry 新增 `dissect_fields`（ParseResult 同步携带）。
  ProtocolTree 在 dissect_fields 非空时用通用渲染器画协议段
  （Frame/Physical 公共段仍为主程序统一生成），否则走旧 builder。
  块文件格式随之升 v4（条目尾部追加字段树；块文件仅同会话往返，
  不跨版本兼容）。
- 列集：MainWindow 在分发器重建与解剖器注册/注销时调用
  sync_packet_columns()——活动协议为插件解剖器时模型换成其列能力
  （见上「列能力契约」），否则回内置列；导出快照携带当时的可见列
  集，CSV 在工作线程按快照生成，界面与导出严格同列。块格式升 v5
  （条目尾部再追加 column_values）。

## 过滤 / topo / 其他插件

- 过滤：make_entry 与 search_text 生成完全沿用（数据源是
  ParseResult 标量），故显示过滤（含 sta-<TEI> 谓词）对插件解析
  帧与内置帧行为一致。
- topo：DissectResult.topo_event 走既有 make_entry 注入路径，
  topo 状态/历史/面板不受解析来源影响。
- 面板插件（LocalPluginEngine 喂帧）：喂的是主程序解析后的帧，
  与改前一致；被选为解码器的插件，其进程内那份仍作观察者收帧。

## 插件间通讯（最小总线）

主进程做中转 hub（PluginBus 语义，复用 manager/engine 回调）：

- 任一插件经宿主 API 发送（native 经 IPluginBackend 总线回调，
  JS/Lua 经 `host.sendPluginMessage(to, topic, text)`）；主进程按
  插件名路由：目标在 host 进程 → IPC PluginMessage 转发；目标在
  进程内引擎 → 投递其后端。同名插件有多个实例时（进程内一份 +
  host 一份）全部收到（按名广播语义）。
- 接收侧回调 `on_plugin_message(from_name, topic, payload)`，
  native 经 IPluginBusAware 接口注入，JS/Lua 经同名全局函数
  `on_plugin_message(from, topic, text)`。
- 载荷为不透明 QByteArray（约定 UTF-8 文本/JSON），主进程不解析、
  不持久化；目标不存在时投递函数返回 false、消息丢弃。

## 生命周期与联动（问题 1/3 的根治）

- PluginManager 新增 `stop_all()`：两遍处理——先逐个置 disabled +
  retired 并 unregister_parser（断绝新请求进入；已排队等锁的迟到
  请求拿锁后复查 retired 立即退出），再逐个拿 `ipc_mutex` 等在途
  请求收尾后才发 Shutdown IPC → QProcess::terminate → 超时 kill
  兜底并释放进程/socket 等重对象。运行时本体（含 mutex）转入退役
  列表、留待管理器析构才 delete，保证迟到请求手中的指针永不悬空。
  与全系统"持锁贯穿请求"的纪律一致，避免采集进行中卸插件/退出时
  与分发线程并发删除对象（UAF）。
- 父死子亡，三层保障：
  1. IPC socket 断开 → host 自行退出（既有行为，保留）；
  2. host 新增 `--parent-pid` 看守：1s 轮询父进程存活，父死即退
     （跨平台；防 socket 语义异常）；
  3. Windows：主程序创建 Job Object（KILL_ON_JOB_CLOSE）并把每个
     host 进程加入，主程序被强杀时 OS 连带终止所有 host。
- 主程序退出：aboutToQuit → PluginManager::stop_all()，不留孤儿。
- 市场安装/更新/卸载：卸锁回调同时停两套——LocalPluginEngine
  同步卸载 + PluginManager::stop_all()；对话框关闭后两套都重新
  加载（引擎既有 load_plugin_dirs + manager 补 loadAll）。
- NativeBackend::shutdown 修正：先 delete 插件根实例再 unload，
  消除实例悬空隐患（与本次锁文件无关，但同属卸载正确性）。

## 架构评审修复（2026-10-11 自审）

- 注册安全：工厂内置条目（GW_2022/NW_2021）不可被插件注册覆盖、
  不可被注销删除（register 拒绝 + unregister 无操作）；manager 在
  握手注册前再查一遍 id 与内置冲突，冲突即禁用并给出原因。
- 注册资格：以 host 在 Hello 上报的 has_dissector 能力位为准，
  清单 dissector:true 但后端无解剖通路（如脚本后端）不注册、不进
  下拉，并发状态提示说明原因。
- 注册键归一化：解剖器工厂键/信号/下拉统一用 protocol_id 大写形；
  运行时 IPC 寻址仍用清单原文 id（代理由工厂 lambda 捕获原文）。
- 注销热回退：配置协议的解剖器被注销时（含采集中崩溃/超时禁用），
  主界面立即重建分发器回退内置协议，保住本轮采集剩余帧；代价是
  切换瞬间在途的一组 MSDU 重组丢失。注册到达仍仅在空闲时切换，
  避免无必要的采集中扰动。
- 总线路由单点化：handle_message 与 parse/dissect 等待循环统一走
  route_plugin_message（信号广播 + host→host 直投）；直投排队到
  主线程执行，避免等待循环持源 runtime 锁时同步拿目标锁自锁。
  此前 parse 等待循环会吞掉总线消息、dissect 等待循环只发信号
  不直投，两处不一致一并消除。

## IPC 单读者化与列能力（2026-10-13 实测驱动）

导出层字节级实测（export_consistency_test）上量后暴露 IPC 响应
双读竞态：socket 同时被主线程 on_host_ready_read 与请求等待循环
（worker 线程 waitForReadyRead+readAll）读取，而 handle_message
没有响应分支——响应被主线程抢读即丢失，等待方等满 5s 超时误判
插件崩溃并禁用，长回放数百帧内必触发、后续帧连锁拒绝。修复：

- 单读者：socket 字节只由 on_host_ready_read 读取；四类响应帧
  （Parse/Dissect/Render/EventAck）按 seq 投递到 PluginRuntime
  的响应邮箱（resp_mutex/resp_cond），parse/dissect/render 三处
  等待统一改走 wait_response/邮箱查询，不再直读 socket。
- 主线程防停转：响应投递依赖主线程事件循环，故主线程侧一切
  ipc_mutex 获取改 tryLock——checkHeartbeats（忙则跳过本 tick）、
  send_plugin_message（忙则报未投递）、request_render /
  send_graphics_event（忙则跳过本帧）。否则主线程阻塞在工作线程
  持有的锁上、响应永远投递不了，与双读是同一死结的两面。
- 心跳判定改按在途 Ping 计时（awaiting_pong_seq + last_ping_ms）：
  只对已发出且超时未答的 Ping 判死，不再用陈旧 last_pong 误杀
  忙碌中的插件（tryLock 跳过若干 tick 后首个成功 tick 若沿用旧
  判定会刚发 Ping 就禁用）。
- 禁用/断连/退役时 abort_response_waiters 唤醒在途等待立即退出。

同批落地列能力契约（见上「数据类型」节）：起因是实测发现
DissectResult 未携带 MsduInfo 明细，插件模式列表/CSV 的 MSDU 派生
列恒空——按「插件声明列、宿主按能力设列、插件按自己的字段导出」
改为通用列契约，不再把内置列字段硬塞回 DissectResult。
