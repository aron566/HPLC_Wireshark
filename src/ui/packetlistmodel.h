/// @file packetlistmodel.h
/// @brief Wireshark 风格 PacketList 的 QAbstractTableModel(磁盘换页版)
/// @note 线程约束:本模型所有访问必须发生在 GUI 线程
///       (append 来自主窗口 flush 定时器,rowCount/data 来自视图重绘)。
///       严禁在模型内加互斥锁——beginInsertRows/endInsertRows 会同步触发
///       视图回调 rowCount()/data(),若持锁将导致同一线程重入死锁(界面卡死)。
/// @note 内存策略:驻留内存条目(热区 m_hot)最多 kBlockSize 条;超出后每满
///       kBlockSize 条 flush 成一块落盘临时文件(不丢弃)。滚动/访问旧条目时
///       按块从盘加载进 LRU 缓存(m_block_cache),以控制常驻内存、支持
///       任意深度回溯。导出/过滤按需流式遍历全部条目。
#ifndef PACKETLISTMODEL_H
#define PACKETLISTMODEL_H

#include "bplcframe.h"
#include <QAbstractTableModel>
#include <QVector>
#include <QHash>
#include <QTemporaryDir>
#include <functional>

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
    static constexpr int kBlockSize = 10000;
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

    /// @brief 滚动预取:按可见行定位全局行号,预加载其所在盘块及前后相邻块
    void ensure_loaded(int visible_row);

    /// @brief 按全局顺序流式遍历全部条目(导出用;不一次性载入内存)
    void for_each_entry(const std::function<void(const PacketEntry&)>& fn);

public slots:
    void append_packets(const QVector<PacketEntry>& entries);
    void append_packet(const PacketEntry& entry);
    void clear_all();
    void activate_row(int visible_row);
    void set_display_filter(const QString& expr);
    bool passes_filter(const PacketEntry& e) const;

signals:
    void packet_activated(const PacketEntry& entry);

private:
    /// @brief 全局行号 g → 条目引用(盘块或热区)
    const PacketEntry& locate(int g) const;
    /// @brief 加载盘块进缓存(若未在);返回是否成功
    bool load_block(int idx) const;
    /// @brief 把 m_hot 前 kBlockSize 条 flush 成一块落盘
    void flush_hot_block();
    QString block_path(int idx) const;
    void touch_lru(int idx) const;

    QVector<PacketEntry>  m_hot;         ///< 驻留热区(最近未 flush 条目,≤kBlockSize)
    int                    m_block_count; ///< 已落盘满块数量
    qint64                 m_total;       ///< 总条目数
    QVector<int>           m_visible;     ///< 过滤器命中的全局行号(升序)
    QString                m_filter;
    QTemporaryDir          m_paging_dir;  ///< 盘块临时目录(进程结束自动清理)

    mutable QHash<int, QVector<PacketEntry>> m_block_cache; ///< 已加载盘块 LRU
    mutable QList<int>     m_lru;          ///< 盘块最近使用顺序(前=最旧)
};

#endif // PACKETLISTMODEL_H
