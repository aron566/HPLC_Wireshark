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
        int start = it->data(0, kRoleStart).toInt();
        int len   = it->data(0, kRoleLen).toInt();
        m_selected_range = {start, len};
        // 多片段高亮(跨块字段) + 复制字节(字段重组内容)
        QList<QPair<int, int>> ranges;
        const QVariantList vl = it->data(0, kRoleRanges).toList();
        for (const QVariant& v : vl) {
            const QVariantList p = v.toList();
            if (p.size() >= 2)
                ranges.append({p[0].toInt(), p[1].toInt()});
        }
        // 旧节点/普通节点(仅 kRoleStart/kRoleLen)无多片段 → 用主区间补单片段
        if (ranges.isEmpty() && start >= 0 && len > 0)
            ranges.append({start, len});
        const QByteArray copy = it->data(0, kRoleBytes).toByteArray();
        emit range_selected(start, len);     // 向后兼容:无字节映射的行发 (-1,0)
        emit ranges_selected(ranges, copy);  // 多片段 + 复制字节
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

namespace {
/// @brief 通用字段树渲染(插件解剖帧):与内置构建器同款的节点数据约定
///        (kRoleStart/kRoleLen 高亮 + 类型字段着色),severity 额外着色提示。
void render_dissect_fields(QTreeWidgetItem* parent,
                           const QVector<DissectField>& fields) {
    for (const DissectField& f : fields) {
        QTreeWidgetItem* it = new QTreeWidgetItem(parent);
        it->setText(0, f.name);
        it->setText(1, f.value);
        it->setData(0, kRoleStart, f.start);
        it->setData(0, kRoleLen, f.len);
        tree_apply_type_color(it, f.name, f.value);
        if (f.severity >= 3) {
            it->setForeground(0, QColor(0xd0, 0x30, 0x30));
            it->setForeground(1, QColor(0xd0, 0x30, 0x30));
        } else if (f.severity == 2) {
            it->setForeground(0, QColor(0xb0, 0x70, 0x00));
            it->setForeground(1, QColor(0xb0, 0x70, 0x00));
        }
        if (!f.children.isEmpty())
            render_dissect_fields(it, f.children);
    }
}
}  // namespace

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

    // 协议特有字段树(MPDU Base + 各帧型 FCH + PB 块 + MSDU 挂载):
    // 插件解剖帧带通用字段树 → 通用渲染;内置解析帧 → 协议构建器构建
    if (!e.dissect_fields.isEmpty())
        render_dissect_fields(root, e.dissect_fields);
    else if (m_builder)
        m_builder->build(root, e);

    expandAll();
}
