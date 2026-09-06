/// @file protocoltree.h
/// @brief 协议树控件(分层显示 MPDU 字段)
#ifndef PROTOCOLTREE_H
#define PROTOCOLTREE_H

#include "bplcframe.h"
#include <QTreeWidget>
#include <QPair>

class ProtocolTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit ProtocolTree(QWidget* parent = nullptr);

    void show_packet(const PacketEntry& entry);
    QPair<int, int> selected_byte_range() const { return m_selected_range; }

signals:
    void range_selected(int start, int len);

private:
    QPair<int, int> m_selected_range;

    QTreeWidgetItem* add_item(QTreeWidgetItem* parent,
                              const QString& field,
                              const QString& value,
                              int byte_start = -1,
                              int byte_len = 0);

    QTreeWidgetItem* add_bit_field(QTreeWidgetItem* parent,
                                   const QString& field,
                                   const QString& value,
                                   int byte_idx, int bit_off, int bit_len);
};

#endif // PROTOCOLTREE_H
