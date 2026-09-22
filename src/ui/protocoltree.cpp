/// @file protocoltree.cpp
/// @brief 协议树控件实现(顶层公共节点 + 委托协议字段树构建器)
#include "protocoltree.h"
#include "protocol_tree_builder.h"
#include "protocolfactory.h"
#include <QHeaderView>

ProtocolTree::ProtocolTree(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(2);
    setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    setUniformRowHeights(true);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setAlternatingRowColors(true);
    // 默认国网;切协议时由 MainWindow 调 set_variant 重建
    m_builder = make_tree_builder(ProtocolVariant::GW_2022);

    // 鼠标点击必触发(即便重复点击已选中行 itemSelectionChanged 不触发,
    // 改用 itemClicked 保证每点都刷新高亮);键盘方向键仍走 selectionChanged。
    auto apply_selection = [this](QTreeWidgetItem* it) {
        if (!it) return;
        int start = it->data(0, Qt::UserRole).toInt();
        int len   = it->data(1, Qt::UserRole).toInt();
        m_selected_range = {start, len};
        emit range_selected(start, len);   // 无字节映射的行发 (-1,0) → 清除高亮
    };
    connect(this, &QTreeWidget::itemClicked, this,
            [apply_selection](QTreeWidgetItem* it, int) { apply_selection(it); });
    connect(this, &QTreeWidget::itemSelectionChanged, this, [this, apply_selection]() {
        // 鼠标点击时当前项即选中项;键盘方向键移动也同步。
        QTreeWidgetItem* it = currentItem();
        if (!it) {
            const auto sel = selectedItems();
            it = sel.isEmpty() ? nullptr : sel.first();
        }
        apply_selection(it);
    });
}

void ProtocolTree::set_variant(ProtocolVariant v) {
    m_builder = make_tree_builder(v);
}

void ProtocolTree::show_packet(const PacketEntry& e) {
    clear();
    m_selected_range = {-1, 0};

    if (!e.accepted) {
        QTreeWidgetItem* root = new QTreeWidgetItem();
        root->setText(0, QStringLiteral("Dropped Frame"));
        root->setText(1, e.reason);
        addTopLevelItem(root);
        tree_add_item(root, "Reason", e.reason);
        return;
    }

    // 顶层节点需 addTopLevelItem 显式挂载
    QTreeWidgetItem* root = new QTreeWidgetItem();
    root->setText(0, QStringLiteral("Frame"));
    root->setText(1, QStringLiteral("%1 frame (%2 B)")
                          .arg(e.meta.is_rf ? "HRF" : "HPLC")
                          .arg(e.raw_bytes.size()));
    addTopLevelItem(root);

    // Physical(协议无关,公共)
    auto* phys = tree_add_item(root, "Physical", "");
    tree_add_item(phys, "Media", e.meta.is_rf ? "HRF (Wireless)" : "HPLC (PLC)");
    tree_add_item(phys, "Timestamp", QString::number(e.meta.timestamp) + " (NTB tick, 40ns)");
    tree_add_item(phys, "Channel/Band", QString::number(e.meta.channel));
    if (e.meta.is_rf) {
        tree_add_item(phys, "PHR MCS", QString::number(e.meta.phr_mcs));
        tree_add_item(phys, "Option",  QString::number(e.meta.option));
    }
    tree_add_item(phys, "FrameTime", e.meta.frame_time.toString("yyyy-MM-dd HH:mm:ss.zzz"));

    // 协议特有字段树(MPDU Base + 各帧型 FCH + PB 块 + MSDU 挂载)由构建器构建
    if (m_builder)
        m_builder->build(root, e);

    expandAll();
}
