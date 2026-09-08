/// @file packetlistmodel.h
/// @brief Wireshark 风格 PacketList 的 QAbstractTableModel
/// @note 线程约束:本模型所有访问必须发生在 GUI 线程
///       (append 来自主窗口 flush 定时器,rowCount/data 来自视图重绘)。
///       严禁在模型内加互斥锁——beginInsertRows/endInsertRows 会同步触发
///       视图回调 rowCount()/data(),若持锁将导致同一线程重入死锁(界面卡死)。
#ifndef PACKETLISTMODEL_H
#define PACKETLISTMODEL_H

#include "bplcframe.h"
#include <QAbstractTableModel>
#include <QVector>

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

    explicit PacketListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient, int role = Qt::DisplayRole) const override;
    QVariant data_color(const PacketEntry& e) const;

    /// @brief 全部捕获/回放帧(GUI 线程只读,用于导出回放文件)
    const QVector<PacketEntry>& all_entries() const { return m_all; }

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
    QVector<PacketEntry>  m_all;
    QVector<int>          m_visible;
    QString               m_filter;
};

#endif // PACKETLISTMODEL_H
