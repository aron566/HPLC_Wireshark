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
            src.neighbors =
                QSet<quint16>(ev.neighbor_teis.begin(), ev.neighbor_teis.end());
            for (quint16 t : ev.neighbor_teis) nodes[t].tei = t;
        }
        for (const CommRateInfo& cr : ev.comm_rates) {
            CoverageNode& n = nodes[cr.tei];
            n.tei = cr.tei;
            n.up_rate = cr.up;
            n.down_rate = cr.down;
        }
        for (quint64 mac : ev.leaves) {
            for (auto it = nodes.begin(); it != nodes.end();) {
                if (it->mac && it->mac == mac)
                    it = nodes.erase(it);
                else
                    ++it;
            }
        }
        if (!nodes.contains(center_tei))
            center_tei = nodes.contains(1) ? quint16(1)
                         : nodes.isEmpty() ? quint16(0) : nodes.firstKey();
    }

    /// @brief 中心节点的展示邻居:优先其自身邻居表,为空则回退反向边
    QVector<quint16> display_neighbors() const {
        QVector<quint16> v;
        auto it = nodes.find(center_tei);
        if (it != nodes.end() && !it->neighbors.isEmpty()) {
            v = QVector<quint16>(it->neighbors.begin(), it->neighbors.end());
        } else {
            for (auto i = nodes.constBegin(); i != nodes.constEnd(); ++i)
                if (i.key() != center_tei && i->neighbors.contains(center_tei))
                    v.append(i.key());
        }
        std::sort(v.begin(), v.end());
        return v;
    }
};

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
        r.arrival_us = in.arrival_us;
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

    void render(QPainter* p, int w, int h) override {
        QMutexLocker lk(&m_model.mutex);
        p->fillRect(0, 0, w, h, QColor(0x17, 0x19, 0x1e));
        QFont f(QStringLiteral("Sans"));
        f.setPixelSize(13);
        f.setBold(true);
        p->setFont(f);

        const quint16 center = m_model.center_tei;
        const QVector<quint16> nbrs =
            center ? m_model.display_neighbors() : QVector<quint16>();
        const CoverageNode* cnode =
            center ? &m_model.nodes[center] : nullptr;

        // 标题
        p->setPen(QColor(0xe8, 0xea, 0xed));
        p->drawText(14, 26, QStringLiteral("信号覆盖 Coverage · 中心 TEI %1")
                                   .arg(center ? QString::number(center)
                                               : QStringLiteral("-")));
        f.setPixelSize(10);
        f.setBold(false);
        p->setFont(f);
        p->setPen(QColor(0x9a, 0xa0, 0xa8));
        int avg = -1, known = 0, sum = 0;
        for (quint16 t : nbrs) {
            const int r = m_model.nodes[t].avg_rate();
            if (r >= 0) { sum += r; ++known; }
        }
        if (known) avg = sum / known;
        p->drawText(14, 44,
                    QStringLiteral("邻居 Neighbors: %1 · 平均成功率 Avg rate: %2")
                        .arg(nbrs.size())
                        .arg(avg >= 0 ? QStringLiteral("%1%").arg(avg)
                                      : QStringLiteral("?")));

        if (!center || !cnode) {
            p->setPen(QColor(0x9a, 0xa0, 0xa8));
            f.setPixelSize(12);
            p->setFont(f);
            p->drawText(QRect(0, 0, w, h), Qt::AlignCenter,
                        QStringLiteral("等待发现列表…\nWaiting for discover list…"));
            return;
        }

        // 布局
        const QPointF C(w / 2.0, h * 0.56);
        const double R = 0.34 * qMin(w, h);
        m_node_pos.clear();
        m_node_pos[center] = C;
        const int n = nbrs.size();
        for (int i = 0; i < n; ++i) {
            const double ang = -kPi / 2.0 + i * 2.0 * kPi / qMax(n, 1);
            m_node_pos[nbrs[i]] =
                C + QPointF(R * qCos(ang), R * qSin(ang));
        }

        // 覆盖圆圈
        p->setPen(QPen(QColor(0x3a, 0x41, 0x50), 1.5, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(C, R, R);
        p->setPen(QColor(0x8b, 0x93, 0xa3));
        f.setPixelSize(10);
        p->setFont(f);
        p->drawText(QPointF(C.x() + R * 0.72, C.y() - R * 0.72),
                    QStringLiteral("覆盖范围 Coverage"));

        // 连线
        for (quint16 t : nbrs) {
            const int r = m_model.nodes[t].avg_rate();
            QColor c = rate_color(r);
            c.setAlpha(110);
            p->setPen(QPen(c, 2));
            p->drawLine(C, m_node_pos[t]);
        }

        // 节点
        f.setPixelSize(10);
        p->setFont(f);
        draw_node(p, center, 13, QColor(0x42, 0xa5, 0xf5), true, true);
        for (quint16 t : nbrs)
            draw_node(p, t, 10, rate_color(m_model.nodes[t].avg_rate()),
                      t == m_hover_tei);

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
            p->setPen(QColor(0x9a, 0xa0, 0xa8));
            p->drawText(lx + 14, ly, ltexts[i]);
            lx += 14 + lfm.horizontalAdvance(ltexts[i]) + 18;
        }

        // 悬停信息框
        if (m_hover_tei && m_model.nodes.contains(m_hover_tei))
            draw_hover_box(p, w, h);

        // 底部提示
        p->setPen(QColor(0x6b, 0x72, 0x80));
        f.setPixelSize(10);
        p->setFont(f);
        p->drawText(QRect(0, h - 28, w, 20), Qt::AlignCenter,
                    QStringLiteral("点击节点设为覆盖中心 · "
                                   "Click a node to set as coverage center"));
    }

    bool handle_event(const GraphicsEvent& e) override {
        QMutexLocker lk(&m_model.mutex);
        if (e.type == GraphicsEventType::MouseMove) {
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
        }
        return false;
    }

private:
    void draw_node(QPainter* p, quint16 tei, int radius, const QColor& fill,
                   bool highlight, bool is_center = false) {
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
        p->setPen(QColor(0xe8, 0xea, 0xed));
        QFont f(QStringLiteral("Sans"));
        f.setPixelSize(10);
        f.setBold(true);
        p->setFont(f);
        p->drawText(QRectF(pos.x() - 20, pos.y() - radius - 20, 40, 16),
                    Qt::AlignCenter, QString::number(tei));
        if (!is_center) {
            const int rate = m_model.nodes[tei].avg_rate();
            f.setBold(false);
            p->setFont(f);
            p->drawText(QRectF(pos.x() - 20, pos.y() + radius + 4, 40, 16),
                        Qt::AlignCenter,
                        rate >= 0 ? QStringLiteral("%1%").arg(rate)
                                  : QStringLiteral("?"));
        }
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
        QFont f(QStringLiteral("Sans"));
        f.setPixelSize(10);
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
        p->setPen(QColor(0x3a, 0x41, 0x50));
        p->setBrush(QColor(0x22, 0x25, 0x2c));
        p->drawRoundedRect(QRectF(bx, by, bw, bh), 6, 6);
        p->setPen(QColor(0xe8, 0xea, 0xed));
        for (int i = 0; i < lines.size(); ++i)
            p->drawText(QPointF(bx + 10, by + 20 + i * 18), lines[i]);
    }

    quint16 hit_test(const QPointF& pt) const {
        for (auto it = m_node_pos.constBegin();
             it != m_node_pos.constEnd(); ++it) {
            const double dx = pt.x() - it->x();
            const double dy = pt.y() - it->y();
            if (dx * dx + dy * dy <= 18.0 * 18.0) return it.key();
        }
        return 0;
    }

    CoverageModel m_model;
    QMap<quint16, QPointF> m_node_pos;  ///< 本次 render 的节点坐标(命中测试用)
    quint16 m_hover_tei = 0;
    QPointF m_hover_pos;
};

}  // namespace

#include "coverage_plugin.moc"
