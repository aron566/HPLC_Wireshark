/// @file coverage_plugin.cpp
/// @brief C++ 信号覆盖插件:以 STA 为中心,用圆圈展示其邻居表覆盖范围
/// @details native 插件,同时实现 IProtocolParserPlugin(收帧建模)与
///          IGraphicsPlugin(绘制)。模型来自 BplcFrame.topo_event:
///          发现列表的 discover_src_tei + neighbor_teis 构成邻居表,
///          comm_rates(上下行成功率)代理信号质量,节点颜色随成功率变化。
///          点击任意节点可将其设为覆盖中心。
#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QFont>
#include <QFontDatabase>
#include <QPen>
#include <QBrush>
#include <QSet>
#include <QMap>
#include <QVector>
#include <QtMath>
#include <algorithm>

#include "iprotocolparserplugin.h"
#include "igraphicsplugin.h"

namespace {

/// @brief MAC 按帧内字节序格式化(低字节先输出,不反转)
QString format_mac(quint64 v) {
    QStringList parts;
    for (int i = 0; i < 6; ++i)
        parts << QString::asprintf("%02X", unsigned((v >> (8 * i)) & 0xFF));
    return parts.join(':');
}

struct CoverageNode {
    quint16 tei = 0;
    quint64 mac = 0;
    QSet<quint16> neighbors;  ///< 有向:该节点发现列表声称能听到的邻居
    int up_rate = -1;         ///< 上行成功率 %;-1=未知
    int down_rate = -1;       ///< 下行成功率 %;-1=未知
    int avg_rate() const {    ///< 平均成功率;-1=未知
        if (up_rate < 0 && down_rate < 0) return -1;
        if (up_rate < 0) return down_rate;
        if (down_rate < 0) return up_rate;
        return (up_rate + down_rate) / 2;
    }
};

/// @brief 覆盖模型:parse 线程写,render/事件读(同 worker 线程,仍加锁)
struct CoverageModel {
    QMutex mutex;
    QMap<quint16, CoverageNode> nodes;
    QMap<quint16, quint16> parent_of;  ///< child -> 父/代理 TEI(来自 routes)
    QMap<QPair<quint16, quint16>, int> discover_cnt;  ///< (src,邻居)->发现帧计数
    quint16 center_tei = 0;

    void on_topo_event(const TopoEvent& ev) {
        QMutexLocker lk(&mutex);
        for (const TeiMacPair& p : ev.nodes) {
            CoverageNode& n = nodes[p.tei];
            n.tei = p.tei;
            if (p.mac) n.mac = p.mac;
        }
        if (ev.kind == TopoEventKind::DiscoverList && ev.discover_src_tei) {
            CoverageNode& src = nodes[ev.discover_src_tei];
            src.tei = ev.discover_src_tei;
            // 邻居表累积(并集):多次发现帧共同决定覆盖全集,
            // 邻居在圈中的角度位置按发现帧计数排序(越常被发现越靠上)
            src.neighbors.unite(
                QSet<quint16>(ev.neighbor_teis.begin(), ev.neighbor_teis.end()));
            for (quint16 t : ev.neighbor_teis) {
                nodes[t].tei = t;
                if (t != ev.discover_src_tei)
                    discover_cnt[qMakePair(ev.discover_src_tei, t)]++;
            }
        }
        for (const CommRateInfo& cr : ev.comm_rates) {
            CoverageNode& n = nodes[cr.tei];
            n.tei = cr.tei;
            n.up_rate = cr.up;
            n.down_rate = cr.down;
        }
        for (const auto& rp : ev.routes) {
            if (rp.first && rp.second) {
                parent_of[rp.first] = rp.second;
                nodes[rp.first].tei = rp.first;
            }
        }
        QSet<quint16> gone;
        for (quint64 mac : ev.leaves) {
            for (auto it = nodes.begin(); it != nodes.end();) {
                if (it->mac && it->mac == mac) {
                    gone.insert(it.key());
                    it = nodes.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (quint16 t : gone) {
            parent_of.remove(t);
            for (auto it = parent_of.begin(); it != parent_of.end();) {
                if (it.value() == t)
                    it = parent_of.erase(it);
                else
                    ++it;
            }
            for (auto it = discover_cnt.begin(); it != discover_cnt.end();) {
                if (it.key().first == t || it.key().second == t)
                    it = discover_cnt.erase(it);
                else
                    ++it;
            }
        }
        if (!nodes.contains(center_tei))
            center_tei = nodes.contains(1) ? quint16(1)
                         : nodes.isEmpty() ? quint16(0) : nodes.firstKey();
    }

    /// @brief 中心节点的展示邻居:严格取该节点发现列表声称能听到的邻居(有向,
    ///        TEI1 能听到 TEI2 不代表 TEI2 能听到 TEI1);按发现帧个数降序(越常
    ///        被发现越靠上),TEI tie-break
    QVector<quint16> display_neighbors() const {
        QVector<quint16> v;
        auto it = nodes.find(center_tei);
        if (it != nodes.end())
            v = QVector<quint16>(it->neighbors.begin(), it->neighbors.end());
        const quint16 c = center_tei;
        std::sort(v.begin(), v.end(), [&](quint16 a, quint16 b) {
            const int ca = discover_cnt.value(qMakePair(c, a), 0);
            const int cb = discover_cnt.value(qMakePair(c, b), 0);
            return ca != cb ? ca > cb : a < b;
        });
        return v;
    }

    /// @brief 中心 X→邻居 Y 的传输质量(0-100;-1=未知):决定 Y 在 X 圈中的距离
    /// @details X 是 Y 的父节点(或 X 为 CCO)时用 Y 的下行成功率;
    ///          Y 是 X 的父节点(含 Y=CCO)时用 X 的上行成功率;
    ///          未知则回退 Y 自身平均成功率。
    int link_rate(quint16 x, quint16 y) const {
        int r = -1;
        if (parent_of.value(y, 0) == x)
            r = nodes.value(y).down_rate;       // X→Y 下行
        else if (parent_of.value(x, 0) == y || y == 1)
            r = nodes.value(x).up_rate;         // X→Y 经上级,用 X 上行
        else if (x == 1)
            r = nodes.value(y).down_rate;       // CCO 覆盖 Y:用 Y 下行
        if (r < 0) r = nodes.value(y).avg_rate();
        return r;
    }

    /// @brief 节点自身覆盖圈的质量:该节点的上行成功率(回退自身平均);-1=未知
    int circle_rate(quint16 t) const {
        int r = nodes.value(t).up_rate;
        if (r < 0) r = nodes.value(t).avg_rate();
        return r;
    }

    /// @brief 邻居到中心的距离因子(0~1,0=最近圆心,1=最远圈边):
    ///        信号强度(成功率)优先;未知则用发现帧个数(越多越近);再未知取 0.8
    double neighbor_distance_factor(quint16 x, quint16 y) const {
        const int rate = link_rate(x, y);
        if (rate >= 0)
            return 0.55 + 0.30 * (1.0 - rate / 100.0);    // 0.55(强) ~ 0.85(弱)
        const int cnt = discover_cnt.value(qMakePair(x, y), 0);
        if (cnt > 0)
            return 0.85 - 0.15 * double(qMin(cnt, 4)) / 4.0;  // 0.70(多) ~ 0.85(少)
        return 0.80;
    }

    /// @brief 是否父子关系(严格按 routes,不含 CCO 推定)
    bool is_parent_child(quint16 a, quint16 b) const {
        return parent_of.value(a, 0) == b || parent_of.value(b, 0) == a;
    }
};

/// @brief 取中西文都可靠的字体(容器 fontconfig 对 "Sans" 的 CJK 回退不稳定)
QFont cov_font(int pixel_size, bool bold = false) {
    static const QString family = [] {
        const QStringList cands = {
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("WenQuanYi Micro Hei"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Sans"),
        };
        const QStringList avail =
            QFontDatabase::families(QFontDatabase::Any);
        for (const QString& c : cands)
            if (avail.contains(c, Qt::CaseInsensitive)) return c;
        return QStringLiteral("Sans");
    }();
    QFont f(family);
    f.setPixelSize(pixel_size);
    f.setBold(bold);
    return f;
}

/// @brief 成功率配色:优/中/差/未知
QColor rate_color(int rate) {
    if (rate < 0) return QColor(0x8a, 0x8f, 0x98);
    if (rate >= 90) return QColor(0x43, 0xd1, 0x7c);
    if (rate >= 70) return QColor(0xe8, 0xc5, 0x47);
    return QColor(0xe5, 0x53, 0x4b);
}

/// @brief 帧解析器:只建模,不做协议解析
class CoverageParser : public IProtocolParser {
public:
    explicit CoverageParser(CoverageModel* m) : m_model(m) {}
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }
    ParseResult parse(const BplcFrame& in, MsduState&,
                      const ParseFilter&) override {
        if (m_model) m_model->on_topo_event(in.topo_event);
        ParseResult r;
        r.meta = in.meta;
        r.raw_wire = in.raw_wire;
        r.accept = true;
        r.msdu.present = false;
        r.msdu.summary = QStringLiteral("COVERAGE");
        return r;
    }
private:
    CoverageModel* m_model = nullptr;
};

constexpr double kPi = 3.141592653589793;

/// @brief 覆盖插件:解析 + 图形
class CoveragePlugin : public QObject,
                       public IProtocolParserPlugin,
                       public IGraphicsPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID BPLC_PARSER_PLUGIN_IID)
    Q_INTERFACES(IPlugin IProtocolParserPlugin IGraphicsPlugin)
public:
    // ---- IPlugin ----
    QString id() const override { return QStringLiteral("cpp-coverage"); }
    QString display_name() const override { return QStringLiteral("C++ 信号覆盖"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QString description() const override {
        return QStringLiteral("以 STA 为中心,用圆圈展示其邻居表覆盖范围,节点颜色表示通信成功率");
    }
    QString author() const override { return QStringLiteral("BPLC Team"); }
    bool initialize() override { return true; }
    void shutdown() override {}

    // ---- IProtocolParserPlugin ----
    QString protocol_id() const override { return QStringLiteral("COVERAGE"); }
    IProtocolParser* create_parser() override {
        return new CoverageParser(&m_model);
    }

    // ---- IGraphicsPlugin ----
    bool has_graphics() const override { return true; }
    QSize preferred_size() const override { return QSize(760, 600); }
    void set_dark(bool dark) override { m_dark = dark; }

    void render(QPainter* p, int w, int h) override {
        QMutexLocker lk(&m_model.mutex);
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::TextAntialiasing, true);
        p->fillRect(0, 0, w, h, c_bg());
        QFont f = cov_font(13, true);
        p->setFont(f);

        const quint16 center = m_model.center_tei;
        const QVector<quint16> nbrs =
            center ? m_model.display_neighbors() : QVector<quint16>();
        const CoverageNode* cnode =
            center ? &m_model.nodes[center] : nullptr;

        // 标题
        p->setPen(c_text());
        p->drawText(14, 26,
                    QStringLiteral("信号覆盖 Coverage · 共 %1 个节点 Nodes")
                        .arg(m_model.nodes.size()));
        f.setPixelSize(10);
        f.setBold(false);
        p->setFont(f);
        p->setPen(c_dim());
        int avg = -1, known = 0, sum = 0;
        for (quint16 t : nbrs) {
            const int r = m_model.circle_rate(t);
            if (r >= 0) { sum += r; ++known; }
        }
        if (known) avg = sum / known;
        p->drawText(14, 44,
                    QStringLiteral("选中 Selected TEI %1 · 邻居 Neighbors: %2 · "
                                   "平均成功率 Avg rate: %3")
                        .arg(center)
                        .arg(nbrs.size())
                        .arg(avg >= 0 ? QStringLiteral("%1%").arg(avg)
                                      : QStringLiteral("?")));

        if (!center || !cnode) {
            p->setPen(c_dim());
            f.setPixelSize(12);
            p->setFont(f);
            p->drawText(QRect(0, 0, w, h), Qt::AlignCenter,
                        QStringLiteral("等待发现列表…\nWaiting for discover list…"));
            return;
        }

        // 布局:选中节点居中,一跳邻居在内圈,其余节点在外圈,每节点自带覆盖圈
        const QPointF C(w / 2.0 + m_pan_x, h * 0.52 + m_pan_y);
        const double R = 0.30 * qMin(w, h) * m_zoom;  // 中心覆盖圈半径
        m_node_pos.clear();
        m_node_pos[center] = C;

        const int n1 = nbrs.size();
        const QSet<quint16> in_ring1(nbrs.begin(), nbrs.end());
        QVector<quint16> rest;
        for (auto it = m_model.nodes.constBegin();
             it != m_model.nodes.constEnd(); ++it) {
            if (it.key() != center && !in_ring1.contains(it.key()))
                rest.append(it.key());
        }
        std::sort(rest.begin(), rest.end());

        // 各节点覆盖圈半径(按环上间距钳制,避免互相吞没)
        double r1 = 0.38 * R;
        if (n1 > 1) r1 = qMin(r1, 0.42 * 2 * R * qSin(kPi / n1));
        // 嵌套子节点圈半径(放得进父圈 r1 内)
        const double rn = qMin(18.0 * m_zoom, r1 * 0.32);
        const double nest_d = r1 * 0.55;  // 子节点距父节点圆心距离

        for (int i = 0; i < n1; ++i) {
            // 内圈半径按信号强度(成功率)优先、发现帧数兜底:越好离中心越近
            const double rr = R * m_model.neighbor_distance_factor(center, nbrs[i]);
            const double ang = -kPi / 2.0 + i * 2.0 * kPi / n1;
            m_node_pos[nbrs[i]] = C + QPointF(rr * qCos(ang), rr * qSin(ang));
        }
        // 子节点归位:父节点在中心/内圈的其余节点,放进父节点的圈内
        // (兄弟间以朝向中心方向为基准 spread;更深层级走外圈椭圆)
        QVector<quint16> nested_order;
        QSet<quint16> nested;
        {
            QMap<quint16, QVector<quint16>> kids;  // parent -> children
            QVector<quint16> outer;
            for (quint16 t : rest) {
                const quint16 p = m_model.parent_of.value(t, 0);
                if (p && p != t && (p == center || in_ring1.contains(p)))
                    kids[p].append(t);
                else
                    outer.append(t);
            }
            for (auto it = kids.constBegin(); it != kids.constEnd(); ++it) {
                const quint16 p = it.key();
                const QVector<quint16>& ch = it.value();
                // 嵌套方向:与父→中心连线垂直,既避开径向父子箭头,
                // 又避开父节点圆点下方的百分比标签
                const QPointF inward = C - m_node_pos[p];
                const double base =
                    std::atan2(inward.y(), inward.x()) - kPi / 2.0;
                for (int i = 0; i < ch.size(); ++i) {
                    // 兄弟节点在整圆上均匀分布(单节点时取 base 方向)
                    const double ang =
                        base + i * 2.0 * kPi / ch.size();
                    m_node_pos[ch[i]] =
                        m_node_pos[p] +
                        QPointF(nest_d * qCos(ang), nest_d * qSin(ang));
                    nested.insert(ch[i]);
                    nested_order.append(ch[i]);
                }
            }
            rest.swap(outer);
        }
        // 外圈用椭圆布局,吃满横向空间(纵向给标题/提示留边)
        const double R2x = w / 2.0 - r1 - 24 * m_zoom;
        const double R2y = h * 0.5 - r1 - 40 * m_zoom;
        double r2 = 0.38 * R;
        const QVector<quint16>& outer = rest;
        if (!outer.isEmpty()) {
            const double spacing = qMin(2 * R2x * qSin(kPi / outer.size()),
                                       2 * R2y * qSin(kPi / outer.size()));
            r2 = qMax(16.0 * m_zoom, qMin(r2, 0.42 * spacing));
        }
        // 外圈整体旋转:与内圈节点角距离最大化,避免径向重叠
        double outer_rot = 0;
        if (n1 > 0 && !outer.isEmpty()) {
            QVector<double> a1;
            for (int i = 0; i < n1; ++i)
                a1.append(-kPi / 2.0 + i * 2.0 * kPi / n1);
            double best = -1;
            for (int k = 0; k < 36; ++k) {
                const double rot = k * 2.0 * kPi / 36;
                double score = 1e9;
                for (int i = 0; i < outer.size(); ++i) {
                    const double a = -kPi / 2.0 + rot +
                                     (i + 0.5) * 2.0 * kPi / outer.size();
                    for (double b : a1) {
                        double d = qAbs(a - b);
                        d = qMin(d, 2 * kPi - d);
                        score = qMin(score, d);
                    }
                }
                if (score > best) { best = score; outer_rot = rot; }
            }
        }
        for (int i = 0; i < outer.size(); ++i) {
            const double ang = -kPi / 2.0 + outer_rot +
                               (i + 0.5) * 2.0 * kPi / outer.size();
            m_node_pos[outer[i]] =
                C + QPointF(R2x * qCos(ang), R2y * qSin(ang));
        }

        // 父子连线:全网父子关系箭头(子→父),中心连线稍后高亮重画
        p->setBrush(Qt::NoBrush);
        auto dot_r = [&](quint16 t) -> double {
            if (t == center) return 13.0 * m_zoom;
            if (nbrs.contains(t)) return 10.0 * m_zoom;
            if (nested.contains(t)) return 6.0 * m_zoom;
            return 7.0 * m_zoom;
        };
        const QColor faint(0x9a, 0xa3, 0xb5, 200);
        for (auto it = m_model.parent_of.constBegin();
             it != m_model.parent_of.constEnd(); ++it) {
            const quint16 ch = it.key(), pa = it.value();
            if (ch == pa) continue;
            if (!m_node_pos.contains(ch) || !m_node_pos.contains(pa)) continue;
            // 嵌套子节点用小箭头(距离短)
            const double hl = nested.contains(ch) ? 8.0 : 12.0;
            draw_arrow(p, m_node_pos[ch], m_node_pos[pa], dot_r(ch), dot_r(pa),
                       faint, 2.0, hl);
        }

        // 各自的覆盖圈:外圈细实线 / 嵌套子节点细圈 / 内圈按上行质量着色 /
        // 中心虚线大圈
        for (quint16 t : outer) {
            p->setPen(QPen(c_border2(), 1));
            p->drawEllipse(m_node_pos[t], r2, r2);
        }
        for (quint16 t : nested_order) {
            p->setPen(QPen(c_border2(), 1));
            p->drawEllipse(m_node_pos[t], rn, rn);
        }
        for (quint16 t : nbrs) {   // 内圈:按节点自身上行质量着色的圈
            QColor c = rate_color(m_model.circle_rate(t));
            c.setAlpha(120);
            p->setPen(QPen(c, 1.2));
            p->drawEllipse(m_node_pos[t], r1, r1);
        }
        p->setPen(QPen(c_border(), 1.5, Qt::DashLine));
        p->drawEllipse(C, R, R);
        p->setPen(c_dim());
        f.setPixelSize(10);
        p->setFont(f);
        // 标签放在圆圈左下方外侧,避开内圈节点
        p->drawText(QPointF(C.x() - R * 0.78 - 110, C.y() + R * 0.78),
                    QStringLiteral("覆盖范围 Coverage"));

        // 中心↔内圈父子连线(高亮箭头,子→父,颜色=中心→邻居传输质量)
        for (quint16 t : nbrs) {
            if (!m_model.is_parent_child(center, t)) continue;
            const int r = m_model.link_rate(center, t);
            QColor c = rate_color(r);
            c.setAlpha(255);
            const bool center_is_parent =
                m_model.parent_of.value(t, 0) == center;
            if (center_is_parent)
                draw_arrow(p, m_node_pos[t], C, 10.0 * m_zoom, 13.0 * m_zoom,
                           c, 3.0, 16.0 * m_zoom);
            else
                draw_arrow(p, C, m_node_pos[t], 13.0 * m_zoom, 10.0 * m_zoom,
                           c, 3.0, 16.0 * m_zoom);
        }

        // 节点:外圈小点 → 嵌套子节点 → 中心 → 内圈
        f.setPixelSize(10);
        p->setFont(f);
        for (quint16 t : outer) {
            QColor fc = rate_color(m_model.nodes[t].avg_rate());
            fc.setAlpha(170);
            draw_node(p, t, 7 * m_zoom, fc, t == m_hover_tei, false, true);
        }
        for (quint16 t : nested_order) {
            // 嵌套子节点:小圆点 + TEI 标签放在远离父节点的一侧,避开父节点标签
            const QPointF pos = m_node_pos[t];
            QColor fc = rate_color(m_model.nodes[t].avg_rate());
            fc.setAlpha(190);
            if (t == m_hover_tei) {
                p->setPen(QPen(QColor(0xff, 0xff, 0xff, 220), 1.5));
                p->setBrush(Qt::NoBrush);
                p->drawEllipse(pos, 9 * m_zoom, 9 * m_zoom);
            }
            p->setPen(Qt::NoPen);
            p->setBrush(fc);
            p->drawEllipse(pos, 6 * m_zoom, 6 * m_zoom);
            QPointF away(0, -1);
            const quint16 par = m_model.parent_of.value(t, 0);
            if (par && par != t && m_node_pos.contains(par)) {
                const QPointF d = pos - m_node_pos[par];
                const double dl = std::hypot(d.x(), d.y());
                if (dl > 1.0) away = d / dl;
            }
            p->setPen(c_text());
            p->setFont(cov_font(10, true));
            const QPointF lp = pos + away * 15.0;
            p->drawText(QRectF(lp.x() - 20, lp.y() - 8, 40, 16),
                        Qt::AlignCenter, QString::number(t));
            p->setFont(f);
        }
        draw_node(p, center, 13 * m_zoom, QColor(0x42, 0xa5, 0xf5), true, true);
        for (quint16 t : nbrs) {
            const int cr = m_model.circle_rate(t);
            draw_node(p, t, 10 * m_zoom, rate_color(cr), t == m_hover_tei,
                      false, false, cr);
        }

        // 图例(右上角,避免与底部提示重叠)
        const struct { QColor c; const char* zh; const char* en; } legend[] = {
            { rate_color(95), "≥90 优", "good" },
            { rate_color(80), "70–89 中", "fair" },
            { rate_color(50), "<70 差", "poor" },
            { rate_color(-1), "未知", "unknown" },
        };
        f.setPixelSize(10);
        p->setFont(f);
        const QFontMetrics lfm(f);
        int total = 0;
        QStringList ltexts;
        for (const auto& e : legend) {
            const QString s =
                QString::fromUtf8(e.zh) + QStringLiteral(" ") +
                QString::fromUtf8(e.en);
            ltexts << s;
            total += 14 + lfm.horizontalAdvance(s) + 18;
        }
        int lx = w - total - 14, ly = 26;
        for (int i = 0; i < 4; ++i) {
            p->setBrush(legend[i].c);
            p->setPen(Qt::NoPen);
            p->drawEllipse(QPoint(lx + 5, ly - 4), 5, 5);
            p->setPen(c_dim());
            p->drawText(lx + 14, ly, ltexts[i]);
            lx += 14 + lfm.horizontalAdvance(ltexts[i]) + 18;
        }

        // 悬停信息框
        if (m_hover_tei && m_model.nodes.contains(m_hover_tei))
            draw_hover_box(p, w, h);

        // 底部提示
        p->setPen(c_dim());
        f.setPixelSize(10);
        p->setFont(f);
        p->drawText(QRect(0, h - 28, w, 20), Qt::AlignCenter,
                    QStringLiteral("离中心越近信号越好 Closer = better · "
                                   "连线为父子关系 Lines = parent-child · "
                                   "点击节点切换 Click to recenter · "
                                   "示意布局 Schematic"));
    }

    bool handle_event(const GraphicsEvent& e) override {
        QMutexLocker lk(&m_model.mutex);
        if (e.type == GraphicsEventType::Wheel) {
            // 滚轮缩放(以鼠标位置为中心,对齐 js-topo)
            const double f = e.delta_y > 0 ? 1.15 : (1.0 / 1.15);
            const double nz = qBound(0.3, m_zoom * f, 4.0);
            const double k = nz / m_zoom;
            m_pan_x = e.x - (e.x - m_pan_x) * k;
            m_pan_y = e.y - (e.y - m_pan_y) * k;
            m_zoom = nz;
            return true;
        }
        if (e.type == GraphicsEventType::MouseMove) {
            if (m_dragging) {
                m_pan_x = m_drag_pan_x + (e.x - m_drag_start.x());
                m_pan_y = m_drag_pan_y + (e.y - m_drag_start.y());
                return true;
            }
            const quint16 hit = hit_test(QPointF(e.x, e.y));
            if (hit != m_hover_tei) {
                m_hover_tei = hit;
                m_hover_pos = QPointF(e.x, e.y);
                return true;
            }
            if (hit) { m_hover_pos = QPointF(e.x, e.y); return true; }
            return false;
        }
        if (e.type == GraphicsEventType::MousePress && e.button == 1) {
            const quint16 hit = hit_test(QPointF(e.x, e.y));
            if (hit && hit != m_model.center_tei) {
                m_model.center_tei = hit;
                return true;
            }
            if (!hit) {
                // 空白处按下:开始拖拽平移
                m_dragging = true;
                m_drag_start = QPointF(e.x, e.y);
                m_drag_pan_x = m_pan_x;
                m_drag_pan_y = m_pan_y;
            }
            return false;
        }
        if (e.type == GraphicsEventType::MouseRelease) {
            if (m_dragging) {
                m_dragging = false;
                return true;
            }
        }
        return false;
    }

private:
    // 主题颜色(跟随主界面深/浅)
    QColor c_bg() const { return m_dark ? QColor(0x17,0x19,0x1e) : QColor(0xf0,0xf0,0xf0); }
    QColor c_text() const { return m_dark ? QColor(0xe8,0xea,0xed) : QColor(0x20,0x20,0x20); }
    QColor c_dim() const { return m_dark ? QColor(0x9a,0xa0,0xa8) : QColor(0x60,0x60,0x60); }
    QColor c_border() const { return m_dark ? QColor(0x3a,0x41,0x50) : QColor(0xc0,0xc0,0xc0); }
    QColor c_border2() const { return m_dark ? QColor(0x2e,0x33,0x3d) : QColor(0xd8,0xd8,0xd8); }
    QColor c_hover_bg() const { return m_dark ? QColor(0x22,0x25,0x2c) : QColor(0xff,0xff,0xff); }

    void draw_node(QPainter* p, quint16 tei, double radius, const QColor& fill,
                   bool highlight, bool is_center = false,
                   bool dim = false, int label_rate = -2) {
        const QPointF pos = m_node_pos.value(tei);
        if (highlight) {
            p->setPen(QPen(Qt::white, 2));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(pos, radius + 3, radius + 3);
        }
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawEllipse(pos, radius, radius);
        // TEI 编号画在点上方(浅色,深背景可读)
        p->setPen(c_text());
        QFont f = cov_font(10, true);
        p->setFont(f);
        p->drawText(QRectF(pos.x() - 20, pos.y() - radius - 20, 40, 16),
                    Qt::AlignCenter, QString::number(tei));
        if (!is_center && !dim) {
            // label_rate=-2:用节点自身平均;调用方可传入链路质量
            const int rate = (label_rate <= -2)
                                 ? m_model.nodes[tei].avg_rate()
                                 : label_rate;
            f.setBold(false);
            p->setFont(f);
            p->drawText(QRectF(pos.x() - 20, pos.y() + radius + 4, 40, 16),
                        Qt::AlignCenter,
                        rate >= 0 ? QStringLiteral("%1%").arg(rate)
                                  : QStringLiteral("?"));
        }
    }

    /// @brief 画子→父方向箭头:线段起止避开两端圆点,箭尖指向父节点边缘
    static void draw_arrow(QPainter* p, const QPointF& from, const QPointF& to,
                           double from_r, double to_r, const QColor& color,
                           double width, double head_len = 7.0) {
        const QPointF d = to - from;
        const double len = std::hypot(d.x(), d.y());
        if (len < from_r + to_r + head_len + 4.0) return;  // 太近不画
        const QPointF u(d.x() / len, d.y() / len);
        const QPointF n(-u.y(), u.x());
        const QPointF a = from + u * (from_r + 2.0);
        const QPointF tip = to - u * (to_r + 1.5);
        const QPointF bc = tip - u * head_len;  // 箭头底边中心
        const double hw = head_len * 0.5;       // 半宽:宽箭头更醒目
        p->setPen(QPen(color, width));
        p->drawLine(a, bc + u);
        QPolygonF poly;
        poly << tip << (bc + n * hw) << (bc - n * hw);
        const QBrush old_brush = p->brush();
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawPolygon(poly);
        p->setBrush(old_brush);
    }

    void draw_hover_box(QPainter* p, int w, int h) {
        const CoverageNode& nd = m_model.nodes[m_hover_tei];
        QStringList lines;
        lines << QStringLiteral("TEI %1%2").arg(nd.tei)
                      .arg(nd.tei == 1 ? QStringLiteral(" (CCO)") : QString());
        lines << QStringLiteral("MAC %1").arg(
            nd.mac ? format_mac(nd.mac) : QStringLiteral("-"));
        lines << QStringLiteral("邻居 Neighbors: %1").arg(nd.neighbors.size());
        lines << QStringLiteral("上行 Up: %1  下行 Down: %2")
                     .arg(nd.up_rate >= 0 ? QStringLiteral("%1%").arg(nd.up_rate)
                                          : QStringLiteral("?"))
                     .arg(nd.down_rate >= 0
                              ? QStringLiteral("%1%").arg(nd.down_rate)
                              : QStringLiteral("?"));
        QFont f = cov_font(10);
        p->setFont(f);
        const QFontMetrics fm(f);
        int bw = 0;
        for (const QString& s : lines)
            bw = qMax(bw, fm.horizontalAdvance(s));
        bw += 20;
        const int bh = lines.size() * 18 + 14;
        qreal bx = m_hover_pos.x() + 16, by = m_hover_pos.y() + 16;
        if (bx + bw > w) bx = m_hover_pos.x() - bw - 12;
        if (by + bh > h) by = m_hover_pos.y() - bh - 12;
        p->setPen(c_border());
        p->setBrush(c_hover_bg());
        p->drawRoundedRect(QRectF(bx, by, bw, bh), 6, 6);
        p->setPen(c_text());
        for (int i = 0; i < lines.size(); ++i)
            p->drawText(QPointF(bx + 10, by + 20 + i * 18), lines[i]);
    }

    quint16 hit_test(const QPointF& pt) const {
        const double r = 18.0 * m_zoom;
        for (auto it = m_node_pos.constBegin();
             it != m_node_pos.constEnd(); ++it) {
            const double dx = pt.x() - it->x();
            const double dy = pt.y() - it->y();
            if (dx * dx + dy * dy <= r * r) return it.key();
        }
        return 0;
    }

    CoverageModel m_model;
    QMap<quint16, QPointF> m_node_pos;  ///< 本次 render 的节点屏幕坐标(命中测试用)
    quint16 m_hover_tei = 0;
    QPointF m_hover_pos;

    // 视图变换(缩放/平移):对齐 js-topo 的交互能力
    double m_zoom = 1.0;      ///< 用户缩放因子(滚轮)
    double m_pan_x = 0.0;     ///< 平移偏移(拖拽)
    double m_pan_y = 0.0;
    QPointF m_drag_start;     ///< 拖拽起点(屏幕坐标)
    double m_drag_pan_x = 0.0, m_drag_pan_y = 0.0;  ///< 拖拽开始时的平移
    bool m_dragging = false;
    bool m_dark = true;       ///< 界面主题(深色?跟随主界面)
};

}  // namespace

#include "coverage_plugin.moc"
