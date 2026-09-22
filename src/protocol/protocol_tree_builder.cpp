/// @file protocol_tree_builder.cpp
/// @brief 协议树公共渲染工具实现(加节点/位域节点/MSDU 字段树渲染)
#include "protocol_tree_builder.h"

/// 把相对重组 buffer 的 [rel_start,rel_start+rel_len) 映射到 raw 偏移;
/// 跨块且字段越块边界(或越界)返回 -1(无法高亮)。
int tree_msdu_raw_of(const MsduRawMap& m, int rel_start, int rel_len) {
    if (rel_start < 0 || rel_len <= 0) return -1;
    if (m.base >= 0) return m.base + rel_start;
    if (m.pb_num <= 0 || m.pb_size <= 4 || m.body <= 0) return -1;
    const int end = rel_start + rel_len;
    const int b0  = rel_start / m.body;
    const int b1  = (end - 1) / m.body;
    if (b0 != b1 || b0 >= m.pb_num) return -1;  // 跨块或越界 → 不可映射
    return m.fch_size + b0 * m.pb_size + m.header_len + (rel_start - b0 * m.body);
}

QTreeWidgetItem* tree_add_item(QTreeWidgetItem* parent, const QString& field,
                               const QString& value, int byte_start, int byte_len) {
    QTreeWidgetItem* it = new QTreeWidgetItem(parent);
    it->setText(0, field);
    it->setText(1, value);
    it->setData(0, Qt::UserRole, byte_start);
    it->setData(1, Qt::UserRole, byte_len);
    return it;
}

QTreeWidgetItem* tree_add_bit_field(QTreeWidgetItem* parent, const QString& field,
                                    const QString& value,
                                    int byte_idx, int bit_off, int bit_len) {
    QString name = field;
    if (bit_len > 0) name += QStringLiteral(" [%1b]").arg(bit_len);

    // 换算字节区间:起始位 -> (byte_idx*8+bit_off),结束位 -> +bit_len
    int first_bit = byte_idx * 8 + bit_off;
    int last_bit  = first_bit + bit_len - 1;
    int bs = first_bit / 8;
    int be = last_bit / 8;
    return tree_add_item(parent, name, value, bs, be - bs + 1);
}

void tree_render_msdu(QTreeWidgetItem* parent, const QVector<MsduFieldNode>& nodes,
                      const MsduRawMap& m) {
    for (const auto& n : nodes) {
        QTreeWidgetItem* it = new QTreeWidgetItem(parent);
        it->setText(0, n.name);
        it->setText(1, n.value);
        // 字段带相对 msdu_body 的字节区间,且本帧含该 MSDU 时 → 映射 raw 高亮
        int rstart = tree_msdu_raw_of(m, n.rel_start, n.rel_len);
        if (rstart >= 0) {
            it->setData(0, Qt::UserRole, rstart);
            it->setData(1, Qt::UserRole, n.rel_len);
        }
        if (!n.children.isEmpty()) {
            tree_render_msdu(it, n.children, m);
            it->setExpanded(true);
        }
    }
}
