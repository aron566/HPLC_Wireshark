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

/// 把相对重组 buffer 的 [rel_start,rel_start+rel_len) 映射为 raw 里的高亮片段列表;
/// 跨块时返回多个片段(每个块内连续);空列表=无法映射。
QList<QPair<int, int>> tree_msdu_raw_ranges(const MsduRawMap& m,
                                            int rel_start, int rel_len) {
    QList<QPair<int, int>> out;
    if (rel_start < 0 || rel_len <= 0) return out;
    if (m.base >= 0) {
        out.append(qMakePair(m.base + rel_start, rel_len));
        return out;
    }
    if (m.pb_num <= 0 || m.pb_size <= 4 || m.body <= 0) return out;
    const int end = rel_start + rel_len;
    const int b0  = rel_start / m.body;
    const int b1  = (end - 1) / m.body;
    if (b0 < 0 || b1 >= m.pb_num) return out;   // 越界 → 不可映射
    for (int b = b0; b <= b1; ++b) {
        const int seg_start = qMax(rel_start, b * m.body);
        const int seg_end   = qMin(end, (b + 1) * m.body);
        if (seg_end <= seg_start) continue;
        const int raw_off = m.fch_size + b * m.pb_size + m.header_len
                          + (seg_start - b * m.body);
        out.append(qMakePair(raw_off, seg_end - seg_start));
    }
    return out;
}

QTreeWidgetItem* tree_add_item(QTreeWidgetItem* parent, const QString& field,
                               const QString& value, int byte_start, int byte_len) {
    QTreeWidgetItem* it = new QTreeWidgetItem(parent);
    it->setText(0, field);
    it->setText(1, value);
    it->setData(0, kRoleStart, byte_start);
    it->setData(0, kRoleLen, byte_len);
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

void tree_apply_msdu_range(QTreeWidgetItem* it, const MsduRawMap& m,
                           int rel_start, int rel_len, const QByteArray& msdu_body) {
    if (!it) return;
    const auto ranges = tree_msdu_raw_ranges(m, rel_start, rel_len);
    if (ranges.isEmpty()) return;
    it->setData(0, kRoleStart, ranges.first().first);
    it->setData(0, kRoleLen,   ranges.first().second);
    QVariantList vl;
    for (const auto& r : ranges) {
        QVariantList seg;
        seg << r.first << r.second;
        vl.append(QVariant::fromValue(seg));
    }
    it->setData(0, kRoleRanges, vl);
    if (rel_start >= 0 && rel_len > 0) {
        if (rel_start + rel_len > msdu_body.size()) {
            qCritical("tree_apply_msdu_range OUT OF RANGE: rel_start=%d rel_len=%d msdu_body.size=%lld",
                      rel_start, rel_len, (long long)msdu_body.size());
        } else {
            it->setData(0, kRoleBytes, msdu_body.mid(rel_start, rel_len));
        }
    }
}

void tree_render_msdu(QTreeWidgetItem* parent, const QVector<MsduFieldNode>& nodes,
                      const MsduRawMap& m, const QByteArray& msdu_body) {
    for (const auto& n : nodes) {
        QTreeWidgetItem* it = new QTreeWidgetItem(parent);
        it->setText(0, n.name);
        it->setText(1, n.value);
        // 字段带相对 msdu_body 的字节区间,映射为 raw 高亮片段(单块 1 段 / 跨块多段)
        tree_apply_msdu_range(it, m, n.rel_start, n.rel_len, msdu_body);
        if (!n.children.isEmpty()) {
            tree_render_msdu(it, n.children, m, msdu_body);
            it->setExpanded(true);
        }
    }
}
