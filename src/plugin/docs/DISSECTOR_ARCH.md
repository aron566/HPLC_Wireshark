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
  （主程序独立告知的帧外信息：时间戳、通道、方向等），插件可用可不用。
- 插件返回**通用结构化结果** DissectResult：列表标量 + 通用字段树 +
  topo 载荷。主界面用通用渲染器把字段树画进详情视图，行为与内置
  解析器（tab 视图、Hex 高亮联动）一致。
- 只有在清单里显式声明 `"dissector": true` 的插件才注册进工厂、
  才出现在协议下拉框中——杜绝面板插件被误选为空心解码器。

## 数据类型（src/plugin/plugin_api/dissect.h）

```cpp
struct FrameContext {          // 主程序 → 解析器（解析器可忽略）
    qint64  epoch_ms;          // 帧到达时刻（PC 时间）
    qint64  arrival_us;        // 单调 µs 时刻（实时串口）
    quint32 ntb;               // 帧内 NTB tick
    bool    is_rf;             // 通道：true=HRF(无线) false=HPLC(载波)
    int     channel;           // 物理通道号，未知=-1
    quint64 frame_index;       // 主程序侧帧序号（分发器计数）
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
};
```

坐标约定：DissectField 的 start/len 以 **dissected_bytes**（解剖器自行
剥除媒介/传输前缀后的字节，与内置解析器的 payload_for_log 同义，
PacketEntry 的 raw_bytes 即此缓冲）为坐标系，与内置解析器的高亮坐标
一致，HexView 联动无需换算。注意输入 payload（BplcFrame.data）含媒介
头，与坐标系相差前缀长度，二者不可混用。

精化帧外信息：媒介头里的 NTB/通道/RF 标志由解剖器在解析时得出
（reader 只能给粗值，如文件回放时通道恒 0），经 refined_ctx 回传后
宿主覆盖展示，保证 Physical 段与内置解析逐字一致。

## 插件接口（native）

```cpp
class IDissectorPlugin {       // src/plugin/plugin_api/idissectorplugin.h
public:
    virtual ~IDissectorPlugin() = default;
    virtual QString dissector_protocol_id() const = 0;
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

- PluginManager 新增 `stop_all()`：逐个 unregister_parser →
  发 Shutdown IPC → QProcess::terminate → 超时 kill 兜底，
  并停心跳定时器。
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
