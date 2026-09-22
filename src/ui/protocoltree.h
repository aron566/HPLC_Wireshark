/// @file protocoltree.h
/// @brief 协议树控件(分层显示 MPDU 字段)
/// @details 本控件只负责:顶层 "Frame"/"Physical" 公共节点 + 委托
///          IProtocolTreeBuilder 构建协议特有字段树。协议字段逻辑全部在
///          gw_2022_tree / nw_2021_tree 各自独立源文件,工厂按 variant 实例化。
#ifndef PROTOCOLTREE_H
#define PROTOCOLTREE_H

#include "bplcframe.h"
#include "protocol_tree_builder.h"
#include <QTreeWidget>
#include <QPair>
#include <memory>

class ProtocolTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit ProtocolTree(QWidget* parent = nullptr);

    void show_packet(const PacketEntry& entry);
    /// 切换协议变体:工厂重建字段树构建器
    void set_variant(ProtocolVariant v);
    QPair<int, int> selected_byte_range() const { return m_selected_range; }

signals:
    void range_selected(int start, int len);
    /// 多片段高亮(跨块字段多个 raw 片段) + 复制字节(字段重组内容)
    void ranges_selected(const QList<QPair<int, int>>& ranges, const QByteArray& copy_bytes);

private:
    QPair<int, int> m_selected_range;
    std::unique_ptr<IProtocolTreeBuilder> m_builder;  ///< 当前协议字段树构建器
};

#endif // PROTOCOLTREE_H
