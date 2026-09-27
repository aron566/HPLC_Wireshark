/// @file topo_window.cpp
/// @brief 拓扑独立窗口实现(层次拓扑图 + 路由变更表 + TEI→MAC 表)
#include "topo_window.h"
#include "i18n.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QLabel>

namespace {

/// @brief MAC 48-bit 帧内原始字节序 → "aa:bb:cc:dd:ee:ff"(与 Table View 一致)
QString format_mac(quint64 v) {
    QString s;
    for (int i = 0; i < 6; ++i) {
        s += QStringLiteral("%1").arg((v >> (8 * i)) & 0xFF, 2, 16, QChar('0'));
        if (i < 5) s += QLatin1Char(':');
    }
    return s;
}

/// @brief epoch ms → 本地时间文本(精确到 ms)
QString format_time(qint64 epoch_ms) {
    if (epoch_ms <= 0) return QStringLiteral("-");
    return QDateTime::fromMSecsSinceEpoch(epoch_ms)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString event_kind_name(TopoEventKind k) {
    switch (k) {
        case TopoEventKind::DiscoverList:   return trl::L("发现列表");
        case TopoEventKind::AssocReq:       return trl::L("关联请求");
        case TopoEventKind::AssocCnf:       return trl::L("关联确认");
        case TopoEventKind::AssocGatherInd: return trl::L("关联汇总指示");
        case TopoEventKind::AssocInd:       return trl::L("关联指示");
        case TopoEventKind::ChangeProxyCnf: return trl::L("代理变更");
        case TopoEventKind::LeaveInd:       return trl::L("离线指示");
        default:                            return trl::L("其他");
    }
}

/// @brief 中→英注册(文件级,仅新增条目;重复注册无害)
struct I18nRegTopoWindow {
    I18nRegTopoWindow() {
        trl::register_en("拓扑图 TOPO", "Topology TOPO");
        trl::register_en("网络:", "Network:");
        trl::register_en("搜索 TEI / MAC…", "Search TEI / MAC…");
        trl::register_en("筛选(时间/类型/说明)…", "Filter (time/type/description)…");
        trl::register_en("TEI → MAC 映射", "TEI → MAC mapping");
        trl::register_en("路由变更记录(关联确认/关联指示/关联汇总指示/代理变更/发现列表/离线指示)",
                         "Route change log (assoc conf / assoc ind / gather ind / proxy change / discover list / leave ind)");
        trl::register_en("序号", "Seq");
        trl::register_en("时间点", "Time");
        trl::register_en("类型", "Type");
        trl::register_en("变更说明", "Description");
        trl::register_en("状态", "Status");
        trl::register_en("层级", "Level");
        trl::register_en("代理 TEI", "Proxy TEI");
        trl::register_en("在线", "Online");
        trl::register_en("入网中", "Joining");
        trl::register_en("离线", "Offline");
        trl::register_en("发现列表", "Discover list");
        trl::register_en("关联请求", "Assoc request");
        trl::register_en("关联确认", "Assoc confirm");
        trl::register_en("关联汇总指示", "Assoc gather indication");
        trl::register_en("关联指示", "Assoc indication");
        trl::register_en("代理变更", "Proxy change");
        trl::register_en("离线指示", "Leave indication");
        trl::register_en("其他", "Other");
        trl::register_en("(无拓扑数据)", "(No topology data)");
        trl::register_en("入网中(关联请求)", "Joining (assoc request)");
        trl::register_en("上级", "Parent");
        trl::register_en("下行成功率", "Downlink success rate");
        trl::register_en("上行成功率", "Uplink success rate");
        trl::register_en("通讯成功率: 暂无上报", "Comm success rate: no report yet");
        trl::register_en("接入方式", "Link");
        trl::register_en("载波", "PLC");
        trl::register_en("实时", "Live");
        trl::register_en("历史回放", "History replay");
        trl::register_en("回到实时", "Back to Live");
    }
} i18n_reg_topo_window;

} // namespace

// ============================= TopoGraphWidget =============================

TopoGraphWidget::TopoGraphWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 280);
    setMouseTracking(true);   // 悬浮(hover)显示节点信息,无需点击
    m_icon_cco = new QPixmap(QStringLiteral(":/icons/cco-router.png"));
    m_icon_online = new QPixmap(QStringLiteral(":/icons/electric-meter_online.png"));
    m_icon_online_going = new QPixmap(QStringLiteral(":/icons/electric-meter _online_going.png"));
    m_icon_offline = new QPixmap(QStringLiteral(":/icons/electric-meter _offline.png"));
}

void TopoGraphWidget::set_state(const TopoState* state) {
    m_state = state;
    relayout();
    update();
}

void TopoGraphWidget::relayout() {
    m_layout.clear();
    m_pos.clear();
    if (!m_state) return;

    const QHash<quint16, int> levels = m_state->compute_levels();
    // 按层级分组(仅记录有层级的节点)
    QHash<int, QVector<quint16>> by_level;
    int max_level = 0;
    for (auto it = levels.begin(); it != levels.end(); ++it) {
        by_level[it.value()].append(it.key());
        if (it.value() > max_level) max_level = it.value();
    }
    // 正在入网(关联请求)节点无 TEI,归入层级 1(CCO 直属下)
    const int pending_count = m_state->pending.size();
    if (pending_count > 0 && max_level < 1) max_level = 1;

    const qreal node_w = 140.0;   // 节点框宽(图标 + 标签)
    const qreal icon_h = 64.0;    // 图标高
    const qreal label_h = 54.0;   // 标签区高(TEI + MAC 两行)
    const qreal node_h = icon_h + label_h;
    const qreal hgap = 28.0;      // 同层水平间距
    const qreal vgap = 140.0;     // 层间垂直间距
    const qreal margin = 40.0;

    // 画布宽 = 最大层宽(层级 1 含 pending),取整保证足够
    int max_tier = 1;
    for (auto it = by_level.begin(); it != by_level.end(); ++it)
        max_tier = qMax(max_tier, it.value().size());
    if (max_level >= 1) max_tier += pending_count;
    m_canvas_w = qMax<qreal>(400.0, max_tier * (node_w + hgap) + 2 * margin);

    // 逐层布局:每层节点在画布上水平居中(层级 1 末尾追加 pending 节点)
    for (int lv = 0; lv <= max_level; ++lv) {
        const QVector<quint16> tier = by_level.value(lv);
        const int extra = (lv == 1) ? pending_count : 0;
        const int total_nodes = tier.size() + extra;
        if (total_nodes == 0) continue;
        const qreal total_w = total_nodes * (node_w + hgap) - hgap;
        const qreal x0 = (m_canvas_w - total_w) / 2;
        int idx = 0;
        for (quint16 tei : tier) {
            const qreal x = x0 + idx * (node_w + hgap);
            const qreal y = margin + lv * vgap;
            m_layout.append({tei, 0, lv, QPointF(x, y)});
            m_pos[tei] = QPointF(x + node_w / 2, y + node_h / 2); // 中心
            ++idx;
        }
        if (extra > 0) {
            for (auto it = m_state->pending.begin(); it != m_state->pending.end(); ++it) {
                const qreal x = x0 + idx * (node_w + hgap);
                const qreal y = margin + lv * vgap;
                m_layout.append({0, it.key(), lv, QPointF(x, y)}); // tei=0 标记 pending
                ++idx;
            }
        }
    }
    m_canvas_h = margin * 2 + (max_level + 1) * vgap;
}

void TopoGraphWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (!m_state || m_layout.isEmpty()) {
        p.setPen(palette().placeholderText().color());
        p.drawText(rect(), Qt::AlignCenter, trl::L("(无拓扑数据)"));
        return;
    }

    p.setRenderHint(QPainter::Antialiasing);
    // 视图变换:世界坐标 → widget 坐标(平移 + 缩放)
    p.translate(m_offset);
    p.scale(m_scale, m_scale);
    const qreal node_w = 140.0, icon_h = 64.0, label_h = 54.0;
    const qreal node_h = icon_h + label_h;

    // 1) 连线:正式节点父→子;正在入网节点连到 CCO
    for (const NodePos& np : m_layout) {
        QPointF parent_center;
        if (np.tei == 0) {
            // 正在入网(关联请求)节点 → 连 CCO(TEI=1)
            auto cit = m_pos.constFind(1);
            if (cit == m_pos.constEnd()) continue;
            parent_center = cit.value();
        } else {
            auto it = m_state->nodes.constFind(np.tei);
            if (it == m_state->nodes.constEnd()) continue;
            const quint16 parent = it.value().parent_tei;
            if (parent == 0xFFFF || parent == np.tei) continue;
            auto pit = m_pos.constFind(parent);
            if (pit == m_pos.constEnd()) continue;
            parent_center = pit.value();
        }
        const QPointF child_top(np.pos.x() + node_w / 2, np.pos.y());
        const QPointF parent_bot(parent_center.x(), parent_center.y() + node_h / 2);
        QColor edge = palette().text().color();
        edge.setAlpha(150);
        p.setPen(QPen(edge, 1.2));
        p.drawLine(parent_bot, child_top);
    }

    // 2) 节点(CCO 路由器 / STA 电表三态 图标 + TEI/MAC 标签)
    for (const NodePos& np : m_layout) {
        const TopoNode* node = nullptr;
        if (np.tei == 0) {
            // 正在入网(关联请求)节点,按 MAC 查 pending
            auto pit = m_state->pending.constFind(np.mac);
            if (pit == m_state->pending.constEnd()) continue;
            node = &pit.value();
        } else {
            auto it = m_state->nodes.constFind(np.tei);
            if (it == m_state->nodes.constEnd()) continue;
            node = &it.value();
        }
        const bool is_cco = (np.tei == 1);
        const bool is_pending = (np.tei == 0);

        // 图标:CCO 路由器 / STA 电表(按入网状态三态)
        QPixmap* icon = nullptr;
        if (is_cco) {
            icon = m_icon_cco;
        } else switch (node->status) {
            case NodeStatus::OnlineGoing: icon = m_icon_online_going; break;
            case NodeStatus::Online:      icon = m_icon_online;       break;
            case NodeStatus::Offline:     icon = m_icon_offline;      break;
        }
        if (icon && !icon->isNull()) {
            const qreal ix = np.pos.x() + (node_w - icon_h) / 2;
            p.drawPixmap(QRectF(ix, np.pos.y(), icon_h, icon_h), *icon,
                         QRectF(0, 0, icon_h, icon_h));
        }

        // 标签区:第一行标题,第二行 MAC,第三行接入方式(RF/载波)
        const QRectF label_rect(np.pos.x(), np.pos.y() + icon_h, node_w, label_h);
        const QColor tc = node->online()
            ? (is_cco ? QColor(86, 156, 214) : palette().text().color())
            : palette().placeholderText().color();
        p.setPen(tc);
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        const QString title = is_cco    ? QStringLiteral("CCO")
                            : is_pending ? trl::L("入网中")
                            : QStringLiteral("STA-%1").arg(np.tei);
        const qreal row_h = label_h / 3.0;
        p.drawText(label_rect.adjusted(0, 0, 0, -2 * row_h), Qt::AlignCenter, title);
        f.setBold(false);
        f.setPointSizeF(f.pointSizeF() - 1.0);
        p.setFont(f);
        p.drawText(label_rect.adjusted(0, row_h, 0, -row_h), Qt::AlignCenter,
                   node->mac ? format_mac(node->mac) : QStringLiteral("MAC ?"));
        // 接入方式(载波/RF)
        p.drawText(label_rect.adjusted(0, 2 * row_h, 0, 0), Qt::AlignCenter,
                   node->is_rf ? QStringLiteral("RF") : trl::L("载波"));
    }
}

QPointF TopoGraphWidget::to_world(const QPointF& w) const {
    return QPointF((w.x() - m_offset.x()) / m_scale,
                   (w.y() - m_offset.y()) / m_scale);
}

bool TopoGraphWidget::hit_node(const QPointF& world, quint16& tei, quint64& mac, QRectF& box) const {
    const qreal node_w = 140.0, icon_h = 64.0, label_h = 54.0;
    const qreal node_h = icon_h + label_h;
    for (const NodePos& np : m_layout) {
        const QRectF r(np.pos.x(), np.pos.y(), node_w, node_h);
        if (r.contains(world)) {
            tei = np.tei;
            mac = np.mac;
            box = r;
            return true;
        }
    }
    return false;
}

void TopoGraphWidget::show_node_tip(const QPoint& global_pos, quint16 tei, quint64 mac) {
    QString title;
    QStringList lines;
    const TopoNode* node = nullptr;
    if (tei == 0) {
        // 正在入网节点(按 MAC 查 pending)
        auto it = m_state->pending.constFind(mac);
        if (it == m_state->pending.constEnd()) return;
        node = &it.value();
        title = trl::L("入网中(关联请求)");
    } else {
        auto it = m_state->nodes.constFind(tei);
        if (it == m_state->nodes.constEnd()) return;
        node = &it.value();
        title = (tei == 1) ? QStringLiteral("CCO") : QStringLiteral("STA-%1").arg(tei);
    }
    lines << (node->mac ? format_mac(node->mac) : QStringLiteral("MAC ?"));
    lines << QStringLiteral("%1: %2").arg(trl::L("接入方式"),
        node->is_rf ? QStringLiteral("RF") : trl::L("载波"));

    if (tei != 1) {
        // 上级(代理)
        const quint16 parent = node->parent_tei;
        if (parent != 0xFFFF && parent != tei) {
            const QString pname = (parent == 1) ? QStringLiteral("CCO") : QStringLiteral("STA-%1").arg(parent);
            lines << QStringLiteral("%1: %2").arg(trl::L("上级"), pname);
        }
        // 通讯成功率(成功率上报,与上级的上下行)
        auto cit = m_state->comm_rates.constFind(tei);
        if (cit != m_state->comm_rates.constEnd()) {
            lines << QStringLiteral("%1: %2%").arg(trl::L("下行成功率")).arg(cit.value().down);
            lines << QStringLiteral("%1: %2%").arg(trl::L("上行成功率")).arg(cit.value().up);
        } else {
            lines << trl::L("通讯成功率: 暂无上报");
        }
    }
    QToolTip::showText(global_pos,
        QStringLiteral("<b>%1</b><br>%2").arg(title, lines.join(QStringLiteral("<br>"))));
}

void TopoGraphWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_drag_start = e->position();
        m_press_widget = e->position();
        m_dragging = false;
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(e);
}

void TopoGraphWidget::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        const QPointF delta = e->position() - m_drag_start;
        if (!m_dragging && delta.manhattanLength() > 4.0)
            m_dragging = true;
        if (m_dragging) {
            m_offset += e->position() - m_drag_start;
            m_drag_start = e->position();
            update();
        }
    } else {
        // 悬浮(hover):命中节点则显示信息,否则隐藏 tooltip
        const QPointF world = to_world(e->position());
        quint16 tei = 0; quint64 mac = 0; QRectF box;
        if (hit_node(world, tei, mac, box))
            show_node_tip(e->globalPosition().toPoint(), tei, mac);
        else
            QToolTip::hideText();
    }
    QWidget::mouseMoveEvent(e);
}

void TopoGraphWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        setCursor(Qt::ArrowCursor);
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(e);
}

void TopoGraphWidget::wheelEvent(QWheelEvent* e) {
    const qreal factor = (e->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    m_scale = qBound(0.2, m_scale * factor, 8.0);
    // 以鼠标位置为锚点缩放(保持鼠标下内容不动)
    const QPointF world = to_world(e->position());
    m_offset = e->position() - world * m_scale;
    update();
    e->accept();
}

// =============================== TopoWindow ===============================

TopoWindow::TopoWindow(QWidget* parent) : QWidget(parent) {
    // 独立顶层窗口:自带标题栏 + 最小化/最大化/关闭按钮,可自由移动
    setWindowFlags(Qt::Window);
    setWindowTitle(trl::L("拓扑图 TOPO"));
    resize(1000, 700);

    // 顶部:NID 下拉
    m_nid_combo = new QComboBox(this);
    connect(m_nid_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TopoWindow::on_nid_changed);

    // 左:拓扑图
    m_graph = new TopoGraphWidget(this);

    // 右:TEI→MAC 表(搜索)
    m_teimac_search = new QLineEdit(this);
    m_teimac_search->setPlaceholderText(trl::L("搜索 TEI / MAC…"));
    connect(m_teimac_search, &QLineEdit::textChanged, this, &TopoWindow::on_teimac_search);
    m_teimac_model = new QStandardItemModel(this);
    m_teimac_table = new QTableView(this);
    m_teimac_table->setModel(m_teimac_model);
    m_teimac_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_teimac_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_teimac_table->horizontalHeader()->setStretchLastSection(true);
    m_teimac_table->verticalHeader()->setVisible(false);

    // 底部:路由变更表(筛选)
    m_routes_filter = new QLineEdit(this);
    m_routes_filter->setPlaceholderText(trl::L("筛选(时间/类型/说明)…"));
    connect(m_routes_filter, &QLineEdit::textChanged, this, &TopoWindow::on_routes_filter);
    m_routes_model = new QStandardItemModel(this);
    m_routes_table = new QTableView(this);
    m_routes_table->setModel(m_routes_model);
    m_routes_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_routes_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_routes_table->horizontalHeader()->setStretchLastSection(true);
    m_routes_table->verticalHeader()->setVisible(false);
    // 双击路由变更表某行 → 追溯到该行对应的帧(序号列对应主界面帧序号)
    connect(m_routes_table, &QTableView::doubleClicked,
            this, &TopoWindow::on_routes_double_clicked);
    // 滚动逻辑与主界面一致:滚到底部才跟随最新,滚离底部暂停跟随
    connect(m_routes_table->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
                if (m_rebuilding_routes) return;  // 重建期间忽略,防 clear 干扰
                m_routes_follow_bottom =
                    (value >= m_routes_table->verticalScrollBar()->maximum());
            });

    // 右侧面板(TEI→MAC)
    auto* teimac_panel = new QWidget(this);
    auto* teimac_lay = new QVBoxLayout(teimac_panel);
    teimac_lay->setContentsMargins(0, 0, 0, 0);
    teimac_lay->addWidget(new QLabel(trl::L("TEI → MAC 映射"), teimac_panel));
    teimac_lay->addWidget(m_teimac_search);
    teimac_lay->addWidget(m_teimac_table, 1);

    // 中央:图 + 右侧表
    auto* mid_split = new QSplitter(Qt::Horizontal, this);
    mid_split->addWidget(m_graph);
    mid_split->addWidget(teimac_panel);
    mid_split->setStretchFactor(0, 3);
    mid_split->setStretchFactor(1, 2);

    // 底部面板(路由变更)
    auto* routes_panel = new QWidget(this);
    auto* routes_lay = new QVBoxLayout(routes_panel);
    routes_lay->setContentsMargins(0, 0, 0, 0);
    routes_lay->addWidget(new QLabel(trl::L("路由变更记录(关联确认/关联指示/关联汇总指示/代理变更/发现列表/离线指示)"), routes_panel));
    routes_lay->addWidget(m_routes_filter);
    routes_lay->addWidget(m_routes_table, 1);

    // 总布局:顶部下拉 + 中央分割 + 底部表
    auto* top_lay = new QHBoxLayout();
    top_lay->addWidget(new QLabel(trl::L("网络:"), this));
    top_lay->addWidget(m_nid_combo, 1);
    // 历史回放调试:模式标签 + 回到实时按钮
    m_mode_label = new QLabel(trl::L("实时"), this);
    m_mode_label->setStyleSheet(QStringLiteral("QLabel { color: palette(highlight); font-weight: bold; }"));
    top_lay->addWidget(m_mode_label);
    m_btn_live = new QPushButton(trl::L("回到实时"), this);
    m_btn_live->setVisible(false);
    connect(m_btn_live, &QPushButton::clicked, this, &TopoWindow::request_live);
    top_lay->addWidget(m_btn_live);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->addLayout(top_lay);
    root->addWidget(mid_split, 1);
    root->addWidget(routes_panel, 1);

    // 节流刷新:高频拓扑事件只置脏标志,定时器批量刷新(防回放/采集卡顿)
    m_refresh_timer = new QTimer(this);
    m_refresh_timer->setInterval(200);
    connect(m_refresh_timer, &QTimer::timeout, this, [this] {
        if (m_dirty && isVisible() && !m_hist_mode) {
            m_dirty = false;
            refresh_nids();
            rebuild_all();
        }
    });
    m_refresh_timer->start();
}

void TopoWindow::set_state_map(const QHash<quint32, TopoState>* map) {
    m_states = map;
    refresh_nids();
}

void TopoWindow::refresh_nids() {
    const quint32 prev = m_current_nid;
    const auto* states = view_states();
    m_nid_combo->blockSignals(true);
    m_nid_combo->clear();
    if (states) {
        // 按 NID 升序
        QList<quint32> nids = states->keys();
        std::sort(nids.begin(), nids.end());
        for (quint32 nid : nids)
            m_nid_combo->addItem(QStringLiteral("NID:0x%1 TOPO").arg(nid, 0, 16), nid);
    }
    m_nid_combo->blockSignals(false);
    if (prev && states && states->contains(prev))
        set_current_nid(prev);
    else if (m_nid_combo->count() > 0)
        set_current_nid(m_nid_combo->itemData(0).toUInt());
    else
        rebuild_all();
}

void TopoWindow::set_current_nid(quint32 nid) {
    if (nid == m_current_nid && m_nid_combo->count() > 0) return;
    m_current_nid = nid;
    // 同步下拉框选中
    for (int i = 0; i < m_nid_combo->count(); ++i) {
        if (m_nid_combo->itemData(i).toUInt() == nid) {
            m_nid_combo->blockSignals(true);
            m_nid_combo->setCurrentIndex(i);
            m_nid_combo->blockSignals(false);
            break;
        }
    }
    setWindowTitle(QStringLiteral("NID:0x%1 TOPO").arg(nid, 0, 16));
    rebuild_all();
}

void TopoWindow::on_nid_changed(int idx) {
    if (idx < 0) return;
    const quint32 nid = m_nid_combo->itemData(idx).toUInt();
    set_current_nid(nid);
}

void TopoWindow::on_routes_filter(const QString& text) {
    Q_UNUSED(text);
    rebuild_routes_table();
}

void TopoWindow::on_teimac_search(const QString& text) {
    Q_UNUSED(text);
    rebuild_teimac_table();
}

void TopoWindow::closeEvent(QCloseEvent* e) {
    // 关闭 = 隐藏,保留拓扑状态,下次工具栏"拓扑"再次打开
    hide();
    e->ignore();
}

void TopoWindow::mark_dirty() {
    if (m_hist_mode) return;  // 历史回放模式:冻结,不跟随新帧
    m_dirty = true;  // 仅置脏标志;定时器批量刷新
}

void TopoWindow::refresh_current() {
    if (!isVisible()) return;  // 未打开时不刷新(打开时由 open_topo_window 触发)
    // 实时刷新:新帧到达后重新收集 NID(可能有新网络)并刷新当前 NID 内容
    refresh_nids();
    rebuild_all();
}

void TopoWindow::rebuild_all() {
    // 图(历史回放模式读冻结快照,否则读实时表)
    const TopoState* st = nullptr;
    if (const auto* states = view_states()) {
        auto it = states->constFind(m_current_nid);
        if (it != states->constEnd()) st = &it.value();
    }
    m_graph->set_state(st);
    // 两张表
    rebuild_routes_table();
    rebuild_teimac_table();
}

void TopoWindow::show_history(const QHash<quint32, TopoState>* hist, bool active,
                              qint64 frame_index, qint64 frame_ms) {
    m_hist_states = hist;
    m_hist_mode = active;
    m_hist_frame = active ? frame_index : -1;
    if (active) {
        m_mode_label->setText(
            QStringLiteral("%1 @ #%2 %3").arg(trl::L("历史回放")).arg(frame_index)
                                         .arg(format_time(frame_ms)));
    }
    update_mode_ui();
    refresh_nids();
    rebuild_all();
    highlight_history_row();  // 标出冻结位置;路由表保留全部行(含目标帧之后的)
}

void TopoWindow::show_live() {
    m_hist_mode = false;
    m_hist_states = nullptr;
    m_hist_frame = -1;
    if (m_routes_table && m_routes_table->selectionModel())
        m_routes_table->selectionModel()->clearSelection();
    update_mode_ui();
    refresh_nids();
    rebuild_all();
}

void TopoWindow::update_mode_ui() {
    if (!m_hist_mode)
        m_mode_label->setText(trl::L("实时"));
    m_btn_live->setVisible(m_hist_mode);
}

void TopoWindow::append_routes_rows(const TopoState* st, const QString& filter,
                                   int from, int to) {
    for (int i = from; i < to; ++i) {  // 时间正序,最新在底部(与主界面一致)
        const TopoEvent& e = st->events[i];
        const QString time = format_time(e.epoch_ms);
        const QString kind = event_kind_name(e.kind);
        const QString nid = QStringLiteral("0x%1").arg(e.nid, 0, 16);
        const QString desc = e.desc;
        if (!filter.isEmpty()) {
            if (!time.contains(filter, Qt::CaseInsensitive) &&
                !kind.contains(filter, Qt::CaseInsensitive) &&
                !nid.contains(filter, Qt::CaseInsensitive) &&
                !desc.contains(filter, Qt::CaseInsensitive))
                continue;
        }
        QList<QStandardItem*> row;
        // 首列序号:对应主界面帧列表的帧序号;UserRole 存帧序号/时间点供双击追溯
        // (过滤可能跳过行,不能用行号反推事件下标)
        QStandardItem* seq_item = new QStandardItem(QString::number(e.frame_index));
        seq_item->setData(e.frame_index, Qt::UserRole);
        seq_item->setData(e.epoch_ms, Qt::UserRole + 1);
        row << seq_item
            << new QStandardItem(time)
            << new QStandardItem(kind)
            << new QStandardItem(nid)
            << new QStandardItem(desc);
        m_routes_model->appendRow(row);
    }
}

/// @brief 路由变更表双击 → 按该行序号列对应的帧号发射追溯请求
void TopoWindow::on_routes_double_clicked(const QModelIndex& idx) {
    if (!idx.isValid() || !m_routes_model) return;
    const QStandardItem* seq_item = m_routes_model->item(idx.row(), 0);
    if (!seq_item) return;
    const qint64 frame = seq_item->data(Qt::UserRole).toLongLong();
    const qint64 ms = seq_item->data(Qt::UserRole + 1).toLongLong();
    if (frame < 0) return;
    emit request_history(frame, ms);
}

/// @brief 历史回放模式下选中冻结帧对应的路由表行(仅高亮,不滚动/不删行)
void TopoWindow::highlight_history_row() {
    if (!m_hist_mode || m_hist_frame < 0 || !m_routes_model || !m_routes_table)
        return;
    // 首列 UserRole 存帧序号(过滤可能跳行,不能按行号反推)
    for (int r = 0; r < m_routes_model->rowCount(); ++r) {
        const QStandardItem* seq_item = m_routes_model->item(r, 0);
        if (seq_item && seq_item->data(Qt::UserRole).toLongLong() == m_hist_frame) {
            m_routes_table->selectRow(r);  // 该行双击时本就可见,不强制滚动
            return;
        }
    }
    // 过滤导致目标行不可见:清除旧选中,避免高亮停留在无关行
    m_routes_table->selectionModel()->clearSelection();
}

void TopoWindow::rebuild_routes_table() {
    // 保存滚动状态:用户是否在底部(跟随最新),以及当前滚动条位置(滚离底部时恢复)
    const bool was_follow = m_routes_follow_bottom;
    const int  saved_value = m_routes_table->verticalScrollBar()->value();
    m_rebuilding_routes = true;   // 屏蔽 clear/append 期间 scrollbar 信号干扰

    // 路由变更表恒读实时表:它是帧记录索引,双击进入历史回放时也不得
    // 截断目标帧之后的行(拓扑图/TEI-MAC 表仍读冻结快照,保持冻结语义)
    const QHash<quint32, TopoState>* states = m_states;
    const TopoState* st = nullptr;
    if (states) {
        auto it = states->constFind(m_current_nid);
        if (it != states->constEnd()) st = &it.value();
    }
    const QString filter = m_routes_filter->text().trimmed();
    const int ev_count = st ? st->events.size() : 0;
    // 视图变化(数据源/NID/过滤文本切换,或事件数收缩)→全量重建;
    // 否则只追加新增事件行,避免每 200ms 全量重建 O(E)(E 随抓包时长线性增长)
    const bool view_changed = (states != m_routes_view) || (m_current_nid != m_routes_nid)
                           || (filter != m_routes_filter_text) || (ev_count < m_routes_shown);
    if (view_changed) {
        m_routes_model->clear();
        m_routes_model->setHorizontalHeaderLabels(
            {trl::L("序号"), trl::L("时间点"), trl::L("类型"), trl::L("NID"), trl::L("变更说明")});
        if (st) append_routes_rows(st, filter, 0, ev_count);
        m_routes_view = states;
        m_routes_nid = m_current_nid;
        m_routes_filter_text = filter;
        m_routes_shown = ev_count;
    } else if (ev_count > m_routes_shown) {
        append_routes_rows(st, filter, m_routes_shown, ev_count);
        m_routes_shown = ev_count;
    }

    m_rebuilding_routes = false;
    // 滚动逻辑与主界面一致:在底部(最新)才跟随,否则恢复原位置(不强制跳回)
    if (was_follow)
        m_routes_table->scrollToBottom();
    else
        m_routes_table->verticalScrollBar()->setValue(saved_value);
}

void TopoWindow::rebuild_teimac_table() {
    m_teimac_model->clear();
    m_teimac_model->setHorizontalHeaderLabels(
        {trl::L("TEI"), trl::L("MAC"), trl::L("状态"), trl::L("层级"), trl::L("代理 TEI")});

    const TopoState* st = nullptr;
    // 历史回放模式读冻结快照,否则读实时表(此前误读 m_states,回放时表格未冻结)
    if (const auto* states = view_states()) {
        auto it = states->constFind(m_current_nid);
        if (it != states->constEnd()) st = &it.value();
    }
    if (!st) return;

    const QString search = m_teimac_search->text().trimmed();
    const QHash<quint16, int> levels = st->compute_levels();
    QList<quint16> teis = st->nodes.keys();
    std::sort(teis.begin(), teis.end());

    for (quint16 tei : teis) {
        const TopoNode& node = st->nodes.value(tei);
        const QString s_tei = tei == 1 ? QStringLiteral("CCO(1)") : QString::number(tei);
        const QString s_mac = node.mac ? format_mac(node.mac) : QStringLiteral("-");
        const QString s_online =
            node.status == NodeStatus::Offline     ? trl::L("离线") :
            node.status == NodeStatus::OnlineGoing ? trl::L("入网中") :
                                                     trl::L("在线");
        const int level = levels.value(tei, -1);
        const QString s_level = level < 0 ? QStringLiteral("-") : QString::number(level);
        const QString s_parent = node.parent_tei == 0xFFFF
            ? QStringLiteral("-") : QString::number(node.parent_tei);

        if (!search.isEmpty()) {
            if (!s_tei.contains(search, Qt::CaseInsensitive) &&
                !s_mac.contains(search, Qt::CaseInsensitive))
                continue;
        }
        QList<QStandardItem*> row;
        row << new QStandardItem(s_tei)
            << new QStandardItem(s_mac)
            << new QStandardItem(s_online)
            << new QStandardItem(s_level)
            << new QStandardItem(s_parent);
        m_teimac_model->appendRow(row);
    }

    // 正在入网(关联请求)节点:TEI 未分配,按 MAC 列出
    for (auto it = st->pending.begin(); it != st->pending.end(); ++it) {
        const TopoNode& node = it.value();
        const QString s_tei = QStringLiteral("-");
        const QString s_mac = node.mac ? format_mac(node.mac) : QStringLiteral("-");
        const QString s_online = trl::L("入网中");
        const QString s_level = QStringLiteral("1");
        const QString s_parent = QStringLiteral("1");
        if (!search.isEmpty()) {
            if (!s_tei.contains(search, Qt::CaseInsensitive) &&
                !s_mac.contains(search, Qt::CaseInsensitive))
                continue;
        }
        QList<QStandardItem*> row;
        row << new QStandardItem(s_tei)
            << new QStandardItem(s_mac)
            << new QStandardItem(s_online)
            << new QStandardItem(s_level)
            << new QStandardItem(s_parent);
        m_teimac_model->appendRow(row);
    }
}
