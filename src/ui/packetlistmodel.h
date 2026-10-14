/// @file packetlistmodel.h
/// @brief Wireshark 风格 PacketList 的 QAbstractTableModel(磁盘换页版)
/// @note 线程约束:本模型所有访问必须发生在 GUI 线程
///       (append 来自主窗口 flush 定时器,rowCount/data 来自视图重绘)。
///       严禁在模型内加互斥锁——beginInsertRows/endInsertRows 会同步触发
///       视图回调 rowCount()/data(),若持锁将导致同一线程重入死锁(界面卡死)。
/// @note 内存策略:驻留内存条目(热区 m_hot)最多 kBlockSize 条;超出后每满
///       kBlockSize 条 flush 成一块落盘临时文件(不丢弃)。滚动/访问旧条目时
///       按块从盘打开 BlockReader 进 LRU 缓存(m_block_cache):打开只解析
///       头/字符串池/轻量索引,条目按需逐条解码(v3 块格式),以控制常驻
///       内存、支持任意深度回溯。过滤只扫索引不解码条目;导出按需流式
///       遍历全部条目。
#ifndef PACKETLISTMODEL_H
#define PACKETLISTMODEL_H

#include "bplcframe.h"
#include "dissect.h"                 // DissectColumn(插件列能力)
#include "packetentry_serialize.h"   // pser::BlockReader(盘块随机访问)
#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <functional>

/// @brief 过滤条件词(表达式预处理产物,定义见 packetlistmodel.cpp)
struct PacketFilterCond {
    QString term;          ///< 子串条件(已 trim+小写;tei<0 且 type_byte==-1 时用)
    int     type_byte = -1;///< >=0 帧型关键词的帧型字节;-1 子串;-2 永假(空词/TEI 溢出)
    int     tei = -1;      ///< >=0:sta-<数字> 的 TEI 等值谓词(比 src/dst TEI,不走子串)
};

/// @brief 预处理后的过滤表达式:'|' 分组、组内 '&' 合取(与 m_filter 同步重建)
struct PacketPreparedFilter {
    QVector<QVector<PacketFilterCond>> groups;
    bool empty = true;     ///< 表达式为空 = 全部通过
};

class PacketListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        COL_INDEX,
        COL_TIME,
        COL_DELTA,
        COL_ORIG_SRC,     ///< 原始发起 TEI(MSDU 头 SourceTEI)
        COL_SOURCE,
        COL_DEST,
        COL_ORIG_DST,     ///< 原始终点 TEI(MSDU 头 DestinationTEI)
        COL_DIR,          ///< 报文方向:↑=上行(终点=CCO) ↓=下行(发起=CCO) *=其它
        COL_PROTOCOL,
        COL_FRAME_TYPE,   ///< 帧类型列(BEACON/SOF/ACK/COORD),位于 Protocol 之后
        COL_MSDU_TYPE,    ///< MSDU 类型列(SOF 重组完成时显示 MMe/APP 类型)
        COL_MSDU_SEQ,     ///< MSDU 序号列(MSDU_BASE 的 MSDUIndex)
        COL_LENGTH,
        COL_INFO,
        COL_COUNT
    };

    /// @brief 驻留内存的条目上限(超出后 flush 落盘)
    static constexpr int kBlockSize = 1000;
    /// @brief 盘块 LRU 缓存上限(滚动预取窗口,可调;块数 × kBlockSize = 预取内存)
    static constexpr int kMaxCacheBlocks = 4;

    explicit PacketListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient, int role = Qt::DisplayRole) const override;
    QVariant data_color(const PacketEntry& e) const;

    /// @brief 总条目数(含已落盘历史)
    qint64 total_count() const { return m_total; }

    /// @brief 是否正在异步过滤(测试等待用)
    bool filtering() const { return m_filtering; }

    /// @brief 滚动预取:按可见行定位全局行号,预加载其所在盘块及前后相邻块
    void ensure_loaded(int visible_row);

    /// @brief 按全局顺序流式遍历全部条目(导出用;不一次性载入内存)
    void for_each_entry(const std::function<void(const PacketEntry&)>& fn);

    // ---- 插件列能力 ----
    /// @brief 切换到插件声明的列集:#/Time/Delta 固定列之后按声明顺序
    ///        显示 default_visible 的列;每帧值取 PacketEntry::column_values
    void set_plugin_columns(const QVector<DissectColumn>& columns);
    /// @brief 回到内置固定列集
    void use_builtin_columns();
    bool plugin_columns_active() const { return m_plugin_columns_active; }
    /// @brief 插件声明的全部列(含当前隐藏列)
    QVector<DissectColumn> plugin_columns() const { return m_plugin_columns; }
    /// @brief 当前实际显示的插件列(声明顺序)
    QVector<DissectColumn> visible_plugin_columns() const;
    /// @brief 显示/隐藏一列插件列;返回 false=未找到该列或试图隐藏最后一列
    bool set_plugin_column_visible(const QString& id, bool visible);

    // ---- 插件着色配置 ----
    /// @brief 载入插件声明的着色规则(按 id 索引;空=清除,回退内置着色)
    /// @details 每帧按 PacketEntry::color_id 查本表:命中且规则指定了
    ///          前景/背景色则用之;未命中或插件未声明时沿用内置着色
    ///          逻辑(MMe/BID/帧型)。规则只影响呈现,不改变列布局,
    ///          故不触发模型重置,仅刷新当前视图。
    void set_plugin_color_rules(const QVector<DissectColorRule>& rules);
    bool plugin_colors_active() const { return m_plugin_colors_active; }

    /// @brief 导出快照:热区拷贝 + 已落盘块文件路径(升序),供工作线程流式导出
    /// @note 在 GUI 线程调用;快照独立于模型(热区深拷贝、盘块路径为快照值),
    ///       之后模型继续 append/滚动均不影响该快照,可安全传给工作线程。
    struct ExportSnapshot {
        QVector<PacketEntry> hot;         ///< 热区条目拷贝(≤ kBlockSize)
        QStringList          block_paths; ///< 盘块文件路径(升序,对应块 0..N-1)
        /// TEI→MAC 映射表快照(CSV 导出 Source/Dest 列显示 MAC 用;深拷贝)
        QHash<quint32, QHash<quint16, quint64>> tei_mac;
        /// CSV 导出列集快照:插件列模式时为 true,plugin_columns 是导出
        /// 时的可见插件列(顺序与界面一致);内置模式下 CSV 走固定列。
        bool                   plugin_columns_active = false;
        QVector<DissectColumn> plugin_columns;
    };
    ExportSnapshot make_export_snapshot() const;

    /// @brief 按可见行号取条目拷贝(供 UI 跨行时间差计算等;内部触发盘块按需加载)
    /// @return 行号越界返回 false,否则拷贝到 out 并返回 true
    bool entry_at(int visible_row, PacketEntry& out) const;

public slots:
    void append_packets(QVector<PacketEntry> entries);  // 按值:move 语义,避免 PacketEntry 深拷贝
    void append_packet(const PacketEntry& entry);
    void clear_all();
    void activate_row(int visible_row);
    void set_display_filter(const QString& expr);
    bool passes_filter(const PacketEntry& e) const;

signals:
    void packet_activated(const PacketEntry& entry);

private:
    /// @brief 全局行号 g → 条目引用(盘块或热区)
    /// @warning 盘块路径的引用来自 BlockReader 单槽缓存(见 entry_ref):
    ///          下一次 locate/data 调用即可能失效,只能瞬时消费(立即读
    ///          字段、emit 同步信号或深拷贝),严禁跨调用持有。
    const PacketEntry& locate(int g) const;
    /// @brief 加载盘块进缓存(若未在);返回是否成功
    bool load_block(int idx) const;
    /// @brief 把 m_hot 前 kBlockSize 条 flush 成一块落盘
    void flush_hot_block();
    QString block_path(int idx) const;
    void touch_lru(int idx) const;
    /// @brief 更新 TEI→MAC 映射表(按 NID 分表;取自 MSDU 头 MACAddrFlag 扩展字段)
    void update_tei_mac(const PacketEntry& e);
    /// @brief 查 TEI 对应 MAC(48-bit;无记录返回 0)
    quint64 lookup_mac(quint32 nid, quint16 tei) const;
    /// @brief 模型列 section → 插件列描述(仅插件列模式且 section≥3 时非空)
    const DissectColumn* plugin_column_for_section(int section) const;
    /// @brief 条目命中的插件着色规则(未启用/未命中返回空)
    const DissectColorRule* plugin_color_for(const PacketEntry& e) const;

    bool                   m_plugin_columns_active = false;
    QVector<DissectColumn> m_plugin_columns;        ///< 插件声明的全部列
    QVector<bool>          m_plugin_column_visible; ///< 与 m_plugin_columns 并行
    bool                   m_plugin_colors_active = false;
    QHash<QString, DissectColorRule> m_plugin_colors; ///< 插件着色规则(按 id)

    QVector<PacketEntry>  m_hot;         ///< 驻留热区(最近未 flush 条目,≤kBlockSize)
    int                    m_block_count; ///< 已落盘满块数量
    qint64                 m_total;       ///< 总条目数
    QVector<int>           m_visible;     ///< 过滤器命中的全局行号(升序)
    QString                m_filter;
    PacketPreparedFilter   m_prepared;   ///< m_filter 的预处理形式(匹配零分配)
    bool                   m_filtering = false; ///< 过滤进行中(异步;期间 append 暂存到 m_deferred)
    int                    m_filter_gen = 0; ///< 过滤代计数,丢弃过期异步结果
    QVector<PacketEntry>   m_deferred;    ///< 过滤期间暂存的 append 条目
    QHash<quint32, QHash<quint16, quint64>> m_tei_mac; ///< TEI→MAC 映射表(NID → TEI → MAC48)
    QTemporaryDir          m_paging_dir;  ///< 盘块临时目录(进程结束自动清理)

    mutable QHash<int, pser::BlockReader> m_block_cache; ///< 已打开盘块 LRU
    mutable QList<int>     m_lru;          ///< 盘块最近使用顺序(前=最旧)
};

#endif // PACKETLISTMODEL_H
