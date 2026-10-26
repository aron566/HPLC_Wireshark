/// @file protocoltree.cpp
/// @brief 协议树控件实现(顶层公共节点 + 委托协议字段树构建器)
#include "protocoltree.h"
#include "protocol_tree_builder.h"
#include "protocolfactory.h"
#include "i18n.h"
#include <QHeaderView>
#include <QDebug>

namespace {
struct I18nRegProtocolTree {
    I18nRegProtocolTree() {
        trl::register_en("(插件字段树需重新解剖：该帧已换页，点击本行重新获取)",
                         "(Plugin Field Tree Needs Re-Dissection: Frame Paged Out, Click This Row to Re-Fetch)");
    }
};
const I18nRegProtocolTree g_i18n_reg_protocoltree;
}  // namespace

ProtocolTree::ProtocolTree(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(2);
    setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    setUniformRowHeights(true);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setAlternatingRowColors(true);
    // 构建器不预设:Builder 在 builder_for() 中按帧协议懒创建并缓存

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

namespace {

/// @brief PacketEntry::protocol(quint8 常量) → 工厂入参 ProtocolVariant
ProtocolVariant variant_of(quint8 p) {
    switch (p) {
        case kProtocolNw2021: return ProtocolVariant::NW_2021;
        case kProtocolGw2026: return ProtocolVariant::GW_2026;
        case kProtocolNw2026: return ProtocolVariant::NW_2026;
        default:              return ProtocolVariant::GW_2022;
    }
}

/// @brief 通用字段树渲染(插件解析帧)
/// @details 节点约定与内置构建器一致:kRoleStart/kRoleLen 供 Hex 高亮,
///          类型字段复用 tree_apply_type_color 着色;severity 再叠加告警色。
///          越界坐标只告警不崩溃(插件可能算错,不能拖垮 UI)。
static void render_dissect_fields(QTreeWidgetItem* parent,
                                  const QVector<DissectField>& fields,
                                  int raw_bytes_size) {
    for (const DissectField& f : fields) {
        QTreeWidgetItem* it = new QTreeWidgetItem(parent);
        it->setText(0, f.name);
        it->setText(1, f.value);
        int s = f.start, l = f.len;
        // 越界降级为"无字节映射",避免 setData 出界后高亮越界读取。
        // 用 qint64 求和:插件可能传 INT_MAX,`s + l` 会溢出变负绕过守卫。
        if (s >= 0 && (l <= 0
                       || qint64(s) + qint64(l) > qint64(raw_bytes_size))) {
            qWarning("dissect field out of range: %s start=%d len=%d raw=%d",
                     qPrintable(f.name), s, l, raw_bytes_size);
            s = -1; l = 0;
        }
        it->setData(0, kRoleStart, s);
        it->setData(0, kRoleLen, l);
        tree_apply_type_color(it, f.name, f.value);
        if (f.severity >= 3) {
            it->setForeground(0, QColor(0xd0, 0x30, 0x30));
            it->setForeground(1, QColor(0xd0, 0x30, 0x30));
        } else if (f.severity == 2) {
            it->setForeground(0, QColor(0xb0, 0x70, 0x00));
            it->setForeground(1, QColor(0xb0, 0x70, 0x00));
        }
        if (!f.children.isEmpty())
            render_dissect_fields(it, f.children, raw_bytes_size);
    }
}

}  // namespace

IProtocolTreeBuilder* ProtocolTree::builder_for(const PacketEntry& e) {
    // 插件解析帧(高位=插件位):字段树走通用渲染(dissect_fields)或换页提示,
    // 绝不按内置协议布局画树(否则用户会把插件协议帧当内置协议读且字段错位)
    if (e.protocol & kProtocolPluginBit)
        return nullptr;
    // 分派依据:这一帧实际被哪个解析器解出来(PacketEntry::protocol),与会话配置无关。
    // 解析器内部会把帧委托给旧版解析器(GW_2026→GW_2022 / NW_2026→NW_2021),
    // e.protocol 已同步标记为被委托方,字段树据此选布局——这正是"版本自适应"
    // 在展示层的对应实现,不再需要按会话配置写特例分支。
    // 逐帧判定:同一包里新旧帧交替出现也各画各的;构建器对象按协议懒创建后常驻,
    // 四个实现均无成员状态,复用无副作用。
    auto it = m_builders.find(e.protocol);
    if (it == m_builders.end()) {
        it = m_builders.emplace(e.protocol,
                                make_tree_builder(variant_of(e.protocol))).first;
    }
    return it->second.get();
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

    // 协议段字段树三态分派:
    //   ① 插件通用字段树非空 → 通用渲染(在线插件帧/已重新解剖的换页帧)
    //   ② 插件位帧且字段树为空(换页后 dissect_fields 不落盘) → 挂重新解剖提示节点,
    //      不画内置树(否则用户会把插件协议帧当内置协议读且字段全错位)
    //   ③ 内置解析帧(dissect_fields 恒空、无插件位) → 按 e.protocol 分派的协议构建器
    if (!e.dissect_fields.isEmpty()) {
        render_dissect_fields(root, e.dissect_fields, e.raw_bytes.size());
    } else if (e.protocol & kProtocolPluginBit) {
        tree_add_item(root, "Protocol",
            trl::L("(插件字段树需重新解剖：该帧已换页，点击本行重新获取)"));
    } else if (IProtocolTreeBuilder* b = builder_for(e)) {
        b->build(root, e);
    }

    expandAll();
}
