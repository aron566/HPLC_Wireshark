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
#include <cmath>

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

/// @brief MDS 布局还原结果:相对单位坐标 + 每节点覆盖圈半径
/// @details 由发现列表 + 链路质量(成功率/发现帧计数)估算成对距离,
///          经典 MDS + 加权 stress 迭代还原全网二维布局(参考 hplc-coverage.html)。
struct CoverageLayout {
    QMap<quint16, QPointF> pos;     ///< 节点坐标(未缩放的相对单位)
    QMap<quint16, double> radius;   ///< 节点覆盖圈半径(最远邻居距离)
};

/// @brief 覆盖模型:parse 线程写,render/事件读(同 worker 线程,仍加锁)
struct CoverageModel {
    QMutex mutex;
    QMap<quint16, CoverageNode> nodes;
    QMap<quint16, quint16> parent_of;  ///< child -> 父/代理 TEI(来自 routes)
    QMap<QPair<quint16, quint16>, int> discover_cnt;  ///< (src,邻居)->发现帧计数
    quint16 center_tei = 0;
    mutable CoverageLayout m_layout_cache;  ///< 布局缓存(数据变化时重算)
    mutable bool m_layout_dirty = true;     ///< 布局脏标记

    /// @brief 布局计算快照:持锁拷贝,锁外做 O(n³) MDS 计算,不阻塞 parse 线程
    struct LayoutSnapshot {
        QMap<quint16, CoverageNode> nodes;
        QMap<quint16, quint16> parent_of;
        QMap<QPair<quint16, quint16>, int> discover_cnt;
    };
    LayoutSnapshot snapshot_for_layout() const {
        LayoutSnapshot s;
        s.nodes = nodes;
        s.parent_of = parent_of;
        s.discover_cnt = discover_cnt;
        return s;
    }

    static int link_rate_from(const LayoutSnapshot& s, quint16 x, quint16 y) {
        int r = -1;
        if (s.parent_of.value(y, 0) == x)
            r = s.nodes.value(y).down_rate;
        else if (s.parent_of.value(x, 0) == y || y == 1)
            r = s.nodes.value(x).up_rate;
        else if (x == 1)
            r = s.nodes.value(y).down_rate;
        if (r < 0) r = s.nodes.value(y).avg_rate();
        return r;
    }

    static double est_distance_from(const LayoutSnapshot& s, quint16 x, quint16 y) {
        const int rate = link_rate_from(s, x, y);
        const int cnt = s.discover_cnt.value(qMakePair(x, y), 0);
        const double D0 = 10.0, K = 2.3;
        if (rate >= 0)
            return D0 * std::exp(K * (1.0 - rate / 100.0));
        if (cnt > 0) {
            const double f = double(qMin(cnt, 8)) / 8.0;
            return D0 * std::exp(K * (1.0 - 0.6 * f));
        }
        return 120.0;
    }

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
        m_layout_dirty = true;  // 数据变化 → 下次 render 重算 MDS 布局
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

    /// @brief 双向:sel 的发现列表里有 y,且 y 的发现列表里也有 sel
    bool is_bidirectional(quint16 sel, quint16 y) const {
        auto it = nodes.find(y);
        if (it == nodes.end()) return false;
        return it->neighbors.contains(sel);
    }

    /// @brief 单向节点:能听到 sel 但 sel 没听到它的节点(网页右侧 inbound 说明)
    QVector<quint16> inbound_neighbors(quint16 sel) const {
        QVector<quint16> only;
        auto sit = nodes.find(sel);
        if (sit == nodes.end()) return only;
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
            const quint16 i = it.key();
            if (i == sel) continue;
            if (it->neighbors.contains(sel) && !sit->neighbors.contains(i))
                only.append(i);
        }
        std::sort(only.begin(), only.end());
        return only;
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

    /// @brief 节点对距离估计(相对单位):质量越好越近
    /// @details 成功率(rate)优先,指数映射放大区分度(100%→近,0%→远);
    ///          未知回退发现帧计数(越多越近);再未知取中间距离。
    ///          参考 hplc-coverage.html 用 RSSI/报文数估距离的思想,
    ///          此处以成功率/发现帧计数作为信号质量的代理。
    double est_distance(quint16 x, quint16 y) const {
        const int rate = link_rate(x, y);
        const int cnt = discover_cnt.value(qMakePair(x, y), 0);
        const double D0 = 10.0, K = 2.3;
        if (rate >= 0)
            return D0 * std::exp(K * (1.0 - rate / 100.0));  // 100%→10, 0%→~100
        if (cnt > 0) {
            const double f = double(qMin(cnt, 8)) / 8.0;  // 发现帧越多越近
            return D0 * std::exp(K * (1.0 - 0.6 * f));
        }
        // 未知:比单帧目击(cnt=1 → 约83.8)更远,取 120
        return 120.0;
    }

    /// @brief MDS 布局还原(参考 hplc-coverage.html layoutNet):
    ///        发现列表估算成对距离 → 最短路补全 → 经典 MDS 初值 → 加权 stress 迭代。
    static CoverageLayout compute_layout_from(const LayoutSnapshot& s) {
        CoverageLayout out;
        QVector<quint16> ids;
        for (auto it = s.nodes.begin(); it != s.nodes.end(); ++it)
            ids.append(it.key());
        const int n = ids.size();
        if (n == 0) return out;

        // 1. 收集成对距离(双向 log 平均)
        QMap<QPair<int, int>, double> logsum;
        QMap<QPair<int, int>, int> pcnt;
        for (int i = 0; i < n; ++i) {
            auto nit = s.nodes.find(ids[i]);
            if (nit == s.nodes.end()) continue;
            for (quint16 b : nit->neighbors) {
                const int j = ids.indexOf(b);
                if (j < 0 || i == j) continue;
                const auto key = qMakePair(qMin(i, j), qMax(i, j));
                logsum[key] += std::log(qMax(1.0, est_distance_from(s, ids[i], b)));
                pcnt[key]++;
            }
        }
        if (logsum.isEmpty()) {
            // 无邻居信息:退化为按 TEI 排布的网格
            for (int i = 0; i < n; ++i)
                out.pos[ids[i]] = QPointF((i % 8) * 30.0, (i / 8) * 30.0);
            return out;
        }

        // 2. Floyd-Warshall 最短路补全距离矩阵
        const double INF = 1e9;
        QVector<QVector<double>> D(n, QVector<double>(n, INF));
        for (int i = 0; i < n; ++i) D[i][i] = 0.0;
        for (auto it = logsum.begin(); it != logsum.end(); ++it) {
            const double t = std::exp(it.value() / pcnt[it.key()]);
            D[it.key().first][it.key().second] = t;
            D[it.key().second][it.key().first] = t;
        }
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n; ++i) {
                const double dik = D[i][k];
                if (dik >= INF) continue;
                for (int j = 0; j < n; ++j) {
                    const double v = dik + D[k][j];
                    if (v < D[i][j]) D[i][j] = v;
                }
            }
        double mx = 1.0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (D[i][j] < INF && D[i][j] > mx) mx = D[i][j];
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (D[i][j] >= INF) D[i][j] = mx * 1.3;

        // 3. double centering → B 矩阵
        QVector<double> rm(n, 0.0);
        double tm = 0.0;
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int j = 0; j < n; ++j) s += D[i][j] * D[i][j];
            rm[i] = s / n;
            tm += s / n;
        }
        tm /= n;
        QVector<QVector<double>> B(n, QVector<double>(n));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                B[i][j] = -0.5 * (D[i][j] * D[i][j] - rm[i] - rm[j] + tm);

        // 4. 幂迭代求前两个特征向量
        auto eig = [&](QVector<QVector<double>>& M) {
            QVector<double> v(n);
            for (int i = 0; i < n; ++i) v[i] = std::sin(i * 1.7 + 1.0) + 0.1;
            for (int it = 0; it < 500; ++it) {
                QVector<double> w(n, 0.0);
                for (int i = 0; i < n; ++i) {
                    double s = 0.0;
                    for (int j = 0; j < n; ++j) s += M[i][j] * v[j];
                    w[i] = s;
                }
                double nm = 0.0;
                for (int i = 0; i < n; ++i) nm += w[i] * w[i];
                nm = std::sqrt(nm);
                if (nm < 1e-12) break;
                for (int i = 0; i < n; ++i) v[i] = w[i] / nm;
            }
            double lam = 0.0;
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < n; ++j) lam += v[i] * M[i][j] * v[j];
            return qMakePair(v, lam);
        };
        auto e1 = eig(B);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                B[i][j] -= e1.second * e1.first[i] * e1.first[j];
        auto e2 = eig(B);
        const double s1 = std::sqrt(std::max(e1.second, 0.0));
        const double s2 = std::sqrt(std::max(e2.second, 0.0));
        QVector<double> X(n), Y(n);
        for (int i = 0; i < n; ++i) {
            X[i] = e1.first[i] * s1;
            Y[i] = e2.first[i] * s2;
        }

        // 5. 加权 stress 迭代
        QVector<QPair<int, int>> known;
        QVector<double> kt, kw;
        for (auto it = logsum.begin(); it != logsum.end(); ++it) {
            known.append(it.key());
            kt.append(std::exp(it.value() / pcnt[it.key()]));
            kw.append(1.0 / (kt.last() * kt.last()));
        }
        for (int it = 0; it < 260; ++it) {
            QVector<double> dx(n, 0.0), dy(n, 0.0), ws(n, 0.0);
            for (int k = 0; k < known.size(); ++k) {
                const int i = known[k].first, j = known[k].second;
                const double ex = X[i] - X[j], ey = Y[i] - Y[j];
                const double d = std::max(std::hypot(ex, ey), 1e-6);
                const double f = kw[k] * (kt[k] - d) / d;
                dx[i] += f * ex; dy[i] += f * ey;
                dx[j] -= f * ex; dy[j] -= f * ey;
                ws[i] += kw[k]; ws[j] += kw[k];
            }
            for (int i = 0; i < n; ++i)
                if (ws[i] > 0) {
                    X[i] += 0.6 * dx[i] / ws[i];
                    Y[i] += 0.6 * dy[i] / ws[i];
                }
        }

        // 6. 输出坐标 + 圈半径(最远邻居距离)
        for (int i = 0; i < n; ++i)
            out.pos[ids[i]] = QPointF(X[i], Y[i]);
        for (int i = 0; i < n; ++i) {
            double r = 0.0;
            auto nit = s.nodes.find(ids[i]);
            if (nit != s.nodes.end()) {
                for (quint16 b : nit->neighbors) {
                    auto pit = out.pos.find(b);
                    if (pit == out.pos.end()) continue;
                    const double d = std::hypot(out.pos[ids[i]].x() - pit->x(),
                                                out.pos[ids[i]].y() - pit->y());
                    if (d > r) r = d;
                }
            }
            out.radius[ids[i]] = r > 0 ? r * 1.04 + 3.0 : 0.0;
        }
        return out;
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

/// @brief 成功率配色:优/中/差/未知(仅面板表格/图例用)
QColor rate_color(int rate) {
    if (rate < 0) return QColor(0x8a, 0x8f, 0x98);
    if (rate >= 90) return QColor(0x43, 0xd1, 0x7c);
    if (rate >= 70) return QColor(0xe8, 0xc5, 0x47);
    return QColor(0xe5, 0x53, 0x4b);
}

/// @brief 颜色线性插值(HTML mixc):near→far 距离渐变
QColor mix_color(const QColor& a, const QColor& b, double t) {
    t = qBound(0.0, t, 1.0);
    return QColor(int(a.red() + (b.red() - a.red()) * t),
                  int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
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
        const QVector<quint16> inbound =
            center ? m_model.inbound_neighbors(center) : QVector<quint16>();

        // 布局划分:右侧面板 + 底部说明 + 左侧画布
        // 面板宽度随总宽自适应,窄窗口时保证左侧画布至少 ~118px 可画
        const int panel_w = qMin(300, qMax(150, int(w * 0.34)));
        const int map_w = w - panel_w;
        const int header_h = 52;
        const int footer_h = 32;

        // 标题(顶部横跨)
        p->setPen(c_text());
        p->drawText(14, 26,
                    QStringLiteral("信号覆盖 Coverage · 共 %1 个节点 Nodes")
                        .arg(m_model.nodes.size()));
        f.setPixelSize(10);
        f.setBold(false);
        p->setFont(f);
        p->setPen(c_dim());
        int both = 0;
        for (quint16 t : nbrs)
            if (m_model.is_bidirectional(center, t)) ++both;
        // 全网统计(还原网页 stats):平均邻居数 + 全网链路双向/单向
        int total_nbr = 0, total_links = 0, bidir_links = 0;
        QSet<QPair<quint16, quint16>> seen;
        for (auto it = m_model.nodes.constBegin();
             it != m_model.nodes.constEnd(); ++it) {
            total_nbr += it->neighbors.size();
            for (quint16 nb : it->neighbors) {
                const auto key = qMakePair(qMin(it.key(), nb), qMax(it.key(), nb));
                if (seen.contains(key)) continue;
                seen.insert(key);
                ++total_links;
                if (m_model.is_bidirectional(it.key(), nb)) ++bidir_links;
            }
        }
        const double avg_nbr =
            m_model.nodes.isEmpty() ? 0.0
                                    : double(total_nbr) / m_model.nodes.size();
        p->drawText(14, 44,
                    QStringLiteral("选中 TEI %1 · 邻居 %2 · 双向 %3 · 单向 %4 · "
                                   "全网 %5 节点 · 链路 %6(%7 双向) · 平均邻居 %8")
                        .arg(center)
                        .arg(nbrs.size())
                        .arg(both)
                        .arg(inbound.size())
                        .arg(m_model.nodes.size())
                        .arg(total_links)
                        .arg(bidir_links)
                        .arg(QString::number(avg_nbr, 'f', 1)));

        if (!center) {
            p->setPen(c_dim());
            f.setPixelSize(12);
            p->setFont(f);
            p->drawText(QRect(0, 0, w, h), Qt::AlignCenter,
                        QStringLiteral("等待发现列表…\nWaiting for discover list…"));
            return;
        }

        // MDS 布局还原:脏时快照+锁外 O(n³) 计算,不阻塞 parse 线程
        CoverageLayout layout;
        if (m_model.m_layout_dirty) {
            auto snap = m_model.snapshot_for_layout();
            lk.unlock();
            layout = CoverageModel::compute_layout_from(snap);
            lk.relock();
            m_model.m_layout_cache = layout;
            m_model.m_layout_dirty = false;
        } else {
            layout = m_model.m_layout_cache;
        }
        if (layout.pos.isEmpty() || !layout.pos.contains(center)) {
            p->setPen(c_dim());
            f.setPixelSize(12);
            p->setFont(f);
            p->drawText(QRect(0, 0, w, h), Qt::AlignCenter,
                        QStringLiteral("等待发现列表…\nWaiting for discover list…"));
            return;
        }

        // fit:布局包围盒适配画布,叠加用户缩放/平移
        double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
        for (auto it = layout.pos.constBegin(); it != layout.pos.constEnd(); ++it) {
            const double r = layout.radius.value(it.key(), 0.0);
            minx = qMin(minx, it->x() - r);
            miny = qMin(miny, it->y() - r);
            maxx = qMax(maxx, it->x() + r);
            maxy = qMax(maxy, it->y() + r);
        }
        double bw = maxx - minx;
        double bh = maxy - miny;
        // 单节点(或极小包围盒):不用 1x1 盒去 fit(会铺满画布),给合理默认尺寸
        if (layout.pos.size() <= 1 || bw < 20.0 || bh < 20.0) {
            bw = qMax(bw, 200.0);
            bh = qMax(bh, 200.0);
        }
        const int map_h = h - header_h - footer_h;
        const double pad = 40.0;
        const double fit_k =
            qMin((map_w - 2 * pad) / bw, (map_h - 2 * pad) / bh) * m_zoom;
        const double lcx = (minx + maxx) / 2.0;
        const double lcy = (miny + maxy) / 2.0;
        const double map_cx = map_w / 2.0;
        const double map_cy = header_h + map_h / 2.0;

        m_node_pos.clear();
        for (auto it = layout.pos.constBegin(); it != layout.pos.constEnd(); ++it) {
            m_node_pos[it.key()] =
                QPointF((it->x() - lcx) * fit_k + map_cx + m_pan_x,
                        (it->y() - lcy) * fit_k + map_cy + m_pan_y);
        }
        auto scr_r = [&](quint16 t) {
            return layout.radius.value(t, 0.0) * fit_k;
        };

        // 0. 背景网格(对齐 HTML drawMap)
        {
            // step 自适应:保证屏距 ≥28px。
            // 注:不用 `while (step*fit_k<28.0) step*=2.0` —— 该浮点 while 会触发
            // MinGW 13.1.0 编译器的优化 bug,产物 DLL 加载失败(exit 127)。
            // 改用带次数上限的 for,语义相同(step 最多翻 16 次到 3.2M)。
            double step = 50.0;
            for (int k = 0; k < 16 && step * fit_k < 28.0; ++k)
                step *= 2.0;
            p->setPen(QPen(c_grid(), 1));
            const double off_x = map_cx + m_pan_x;
            const double off_y = map_cy + m_pan_y;
            const double xs = qFloor((-off_x / fit_k) / step) * step;
            const double xe = (map_w - off_x) / fit_k;
            for (double wx = xs; wx <= xe; wx += step) {
                const double sx = wx * fit_k + off_x;
                p->drawLine(QPointF(sx, header_h), QPointF(sx, h - footer_h));
            }
            const double ys = qFloor((-off_y / fit_k) / step) * step;
            const double ye = (h - footer_h - off_y) / fit_k;
            for (double wy = ys; wy <= ye; wy += step) {
                const double sy = wy * fit_k + off_y;
                if (sy < header_h) continue;
                p->drawLine(QPointF(0, sy), QPointF(map_w, sy));
            }
        }

        // 1. 全网父子连线(箭头,子→父)
        p->setBrush(Qt::NoBrush);
        const QColor faint = c_dim();
        for (auto it = m_model.parent_of.constBegin();
             it != m_model.parent_of.constEnd(); ++it) {
            const quint16 ch = it.key(), pa = it.value();
            if (ch == pa) continue;
            if (!m_node_pos.contains(ch) || !m_node_pos.contains(pa)) continue;
            draw_arrow(p, m_node_pos[ch], m_node_pos[pa],
                       6.0 * m_zoom, 8.0 * m_zoom, faint, 1.5, 9.0);
        }

        // 1b. 选中中心父子连线高亮(颜色=中心→邻居传输质量,盖在 faint 上)
        for (quint16 t : nbrs) {
            const quint16 pa = m_model.parent_of.value(t, 0);
            const bool t_is_child = (pa == center);
            const bool t_is_parent = (m_model.parent_of.value(center, 0) == t);
            if (!t_is_child && !t_is_parent) continue;
            if (!m_node_pos.contains(t)) continue;
            const int r = m_model.link_rate(center, t);
            QColor c = rate_color(r);
            c.setAlpha(255);
            if (t_is_child)
                draw_arrow(p, m_node_pos[t], m_node_pos[center],
                           10.0 * m_zoom, 13.0 * m_zoom, c, 3.0, 16.0);
            else
                draw_arrow(p, m_node_pos[center], m_node_pos[t],
                           13.0 * m_zoom, 10.0 * m_zoom, c, 3.0, 16.0);
        }

        // 2. 各节点覆盖圈(HTML 风格:ink 蓝填充+描边,单向略强调)
        const QSet<quint16> inb_set(inbound.begin(), inbound.end());
        for (auto it = layout.pos.constBegin(); it != layout.pos.constEnd(); ++it) {
            const quint16 t = it.key();
            if (t == center) continue;
            const double r = scr_r(t);
            if (r <= 0) continue;
            const bool h = inb_set.contains(t);
            QColor fill = c_ink();
            fill.setAlpha(h ? 12 : 7);
            QColor stroke = c_ink();
            stroke.setAlpha(h ? 150 : 55);
            p->setBrush(fill);
            p->setPen(QPen(stroke, h ? 1.3 : 1.0));
            p->drawEllipse(m_node_pos[t], r, r);
        }

        // 3. 中心节点覆盖圈(accent 橙,实线)
        {
            const double r = scr_r(center);
            QColor fill = c_accent();
            fill.setAlpha(25);
            p->setBrush(fill);
            p->setPen(QPen(c_accent(), 2.0));
            if (r > 0) p->drawEllipse(m_node_pos[center], r, r);
        }

        // 4. 节点(HTML 风格:选中橙 / 邻居 near→far 渐变 / 单向空心 / 默认 ink / CCO 方块)
        f.setPixelSize(10);
        p->setFont(f);
        const double dRef = 120.0;  // 距离归一化参考(未知距离=120)
        for (auto it = layout.pos.constBegin(); it != layout.pos.constEnd(); ++it) {
            const quint16 t = it.key();
            if (t == center) continue;
            const bool is_nbr = nbrs.contains(t);
            const bool is_inb = inb_set.contains(t);
            QColor fill, stroke = c_ink();
            double rad = 6.0;
            int label_rate = -2;
            if (is_nbr) {
                const double tt =
                    qBound(0.0, m_model.est_distance(center, t) / dRef, 1.0);
                fill = mix_color(c_near(), c_far(), tt);
                stroke = c_ink();
                rad = 7.5;
                label_rate = m_model.link_rate(center, t);
            } else if (is_inb) {
                fill = c_bg();       // 空心:背景色填充 + accent 描边
                stroke = c_accent();
                rad = 6.0;
            } else {
                fill = c_ink();
                fill.setAlpha(150);
                label_rate = m_model.nodes[t].avg_rate();
            }
            draw_node(p, t, rad * m_zoom, fill, stroke, t == m_hover_tei,
                      (t == 1), false, label_rate);
        }
        draw_node(p, center, 8.5 * m_zoom, c_accent(), c_ink(), true,
                  (center == 1), true, -2);

        // 图例(画布左上角,横排,对齐 HTML legend)
        const struct { QColor c; bool hollow; bool square; const char* zh; const char* en; } legend[] = {
            { c_accent(), false, false, "选中", "selected" },
            { c_near(),  false, false, "邻居", "neighbor" },
            { c_accent(), true,  false, "单向", "one-way" },
            { c_ink(),   false, true,  "CCO", "CCO" },
        };
        f.setPixelSize(9);
        p->setFont(f);
        const QFontMetrics lfm(f);
        QStringList ltexts;
        for (const auto& e : legend)
            ltexts << QString::fromUtf8(e.zh) + QStringLiteral(" ") +
                          QString::fromUtf8(e.en);
        {
            int lx = 14, ly = header_h + 14;
            for (int i = 0; i < 4; ++i) {
                if (legend[i].square) {
                    p->setPen(Qt::NoPen);
                    p->setBrush(legend[i].c);
                    p->drawRect(QRectF(lx + 1, ly - 7, 6, 6));
                } else if (legend[i].hollow) {
                    p->setPen(QPen(legend[i].c, 1.2));
                    p->setBrush(Qt::NoBrush);
                    p->drawEllipse(QPoint(lx + 4, ly - 3), 4, 4);
                } else {
                    p->setPen(Qt::NoPen);
                    p->setBrush(legend[i].c);
                    p->drawEllipse(QPoint(lx + 4, ly - 3), 4, 4);
                }
                p->setPen(c_dim());
                p->drawText(lx + 12, ly, ltexts[i]);
                lx += 12 + lfm.horizontalAdvance(ltexts[i]) + 14;
            }
        }

        // 悬停信息框(仅画布区域)
        if (m_hover_tei && m_model.nodes.contains(m_hover_tei))
            draw_hover_box(p, map_w, h);

        // 右侧面板:选中节点信息 + 邻居表格 + 单向说明
        draw_side_panel(p, panel_w, map_w, header_h, h - footer_h,
                        center, nbrs, inbound, layout);

        // 底部说明
        draw_footer(p, w, h, footer_h);
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
            // 0. 下拉框本体:点击切换展开/收起
            if (m_dropdown_rect.contains(QPointF(e.x, e.y))) {
                m_dropdown_open = !m_dropdown_open;
                return true;
            }
            // 0b. 下拉列表项:点击切换选中并收起
            if (m_dropdown_open) {
                for (const auto& item : m_dropdown_items) {
                    if (item.second.contains(QPointF(e.x, e.y))) {
                        if (item.first != m_model.center_tei)
                            m_model.center_tei = item.first;
                        m_dropdown_open = false;
                        return true;
                    }
                }
            }
            // 1. 先命中右侧面板的邻居表格行(点击切换选中)
            for (const auto& row : m_table_rows) {
                if (row.second.contains(QPointF(e.x, e.y))) {
                    if (row.first != m_model.center_tei) {
                        m_model.center_tei = row.first;
                        return true;
                    }
                    return false;
                }
            }
            // 再命中画布节点
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
    // 主题颜色:严格对齐主界面 theme.cpp 的 dark/light palette
    // (深 #19232D/#DFE1E2/#455364/#346792;浅 #f0f0f0/#202020/#3d6f9f)
    QColor c_bg() const { return m_dark ? QColor(0x19,0x23,0x2d) : QColor(0xf0,0xf0,0xf0); }
    QColor c_text() const { return m_dark ? QColor(0xdf,0xe1,0xe2) : QColor(0x20,0x20,0x20); }
    QColor c_dim() const { return m_dark ? QColor(0x78,0x8d,0x9c) : QColor(0x60,0x60,0x60); }
    QColor c_border() const { return m_dark ? QColor(0x45,0x53,0x64) : QColor(0xc0,0xc0,0xc0); }
    QColor c_border2() const { return m_dark ? QColor(0x37,0x41,0x4f) : QColor(0xd8,0xd8,0xd8); }
    QColor c_hover_bg() const { return m_dark ? QColor(0x22,0x30,0x3c) : QColor(0xff,0xff,0xff); }
    // 节点/圈配色严格对齐 hplc-coverage.html 的 CSS 变量(深浅两套)
    QColor c_accent() const { return m_dark ? QColor(0xff,0x8f,0x52) : QColor(0xd2,0x49,0x0c); }  // --accent 橙(选中)
    QColor c_ink() const { return m_dark ? QColor(0x78,0xb6,0xec) : QColor(0x1d,0x4e,0x7a); }     // --ink 蓝(圈/默认节点)
    QColor c_near() const { return m_dark ? QColor(0x3f,0xcd,0xbb) : QColor(0x0a,0x7c,0x73); }    // --near 绿(近)
    QColor c_far() const { return m_dark ? QColor(0x3a,0x4c,0x59) : QColor(0xbf,0xcb,0xd4); }     // --far 灰(远)
    QColor c_grid() const { return m_dark ? QColor(0x1b,0x2a,0x34) : QColor(0xdf,0xe5,0xe9); }    // --grid 网格线
    QColor c_panel() const { return m_dark ? QColor(0x15,0x21,0x2b) : QColor(0xf8,0xfa,0xfb); }
    QColor c_footer_bg() const { return m_dark ? QColor(0x11,0x1c,0x25) : QColor(0xe8,0xea,0xeb); }
    QColor c_good() const { return m_dark ? QColor(0x3f,0xcd,0xbb) : QColor(0x0a,0x7c,0x73); }
    QColor c_warn() const { return m_dark ? QColor(0xe3,0xa6,0x50) : QColor(0xa3,0x5f,0x0a); }

    void draw_node(QPainter* p, quint16 tei, double radius, const QColor& fill,
                   const QColor& stroke, bool highlight, bool cco = false,
                   bool is_center = false, int label_rate = -2) {
        const QPointF pos = m_node_pos.value(tei);
        if (highlight) {
            p->setPen(QPen(c_text(), 1.2));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(pos, radius + 3, radius + 3);
        }
        p->setPen(QPen(stroke, 1.3));
        p->setBrush(fill);
        if (cco)   // CCO 用方块(对齐 HTML rect)
            p->drawRect(QRectF(pos.x() - radius, pos.y() - radius,
                               2 * radius, 2 * radius));
        else
            p->drawEllipse(pos, radius, radius);
        // TEI 编号画在点上方
        p->setPen(c_text());
        QFont f = cov_font(10, true);
        p->setFont(f);
        p->drawText(QRectF(pos.x() - 20, pos.y() - radius - 20, 40, 16),
                    Qt::AlignCenter, QString::number(tei));
        if (!is_center) {
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

    /// @brief 右侧面板:选中节点信息 + 邻居表格 + 单向节点说明(还原网页 aside)
    void draw_side_panel(QPainter* p, int panel_w, int px, int py, int pb,
                         quint16 center, const QVector<quint16>& nbrs,
                         const QVector<quint16>& inbound,
                         const CoverageLayout& layout) {
        // 面板背景 + 左边分隔线
        p->fillRect(px, py, panel_w, pb - py, c_panel());
        p->setPen(c_border());
        p->drawLine(px, py, px, pb);

        QFont f = cov_font(11);
        p->setFont(f);
        int y = py + 14;

        // 「查看节点」下拉框(对齐 HTML 的 select)
        m_dropdown_rect = QRectF(px + 12, y, panel_w - 24, 22);
        p->setPen(c_border());
        p->setBrush(c_hover_bg());
        p->drawRoundedRect(m_dropdown_rect, 4, 4);
        p->setPen(c_text());
        p->drawText(QRectF(px + 18, y, panel_w - 60, 22),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    QStringLiteral("查看节点 View: TEI %1").arg(center));
        p->setPen(c_dim());
        p->drawText(QRectF(px + panel_w - 34, y, 22, 22), Qt::AlignCenter,
                    m_dropdown_open ? QStringLiteral("▲") : QStringLiteral("▼"));
        y += 28;

        // 展开的节点列表(覆盖在面板上部)
        if (m_dropdown_open) {
            m_dropdown_items.clear();
            QVector<quint16> all;
            for (auto it = m_model.nodes.constBegin();
                 it != m_model.nodes.constEnd(); ++it)
                all.append(it.key());
            std::sort(all.begin(), all.end());
            const int row_h = 18;
            for (quint16 t : all) {
                const QRectF row(px + 12, y, panel_w - 24, row_h);
                m_dropdown_items.append(qMakePair(t, row));
                if (t == center)
                    p->fillRect(row, c_accent());
                p->setPen(c_text());
                p->drawText(row.adjusted(6, 0, -4, 0), Qt::AlignVCenter,
                            QStringLiteral("TEI %1%2")
                                .arg(t)
                                .arg(t == 1 ? QStringLiteral(" (CCO)")
                                            : QString()));
                y += row_h;
            }
            y += 6;
        }

        // 选中节点信息
        const CoverageNode& nd = m_model.nodes[center];
        p->setPen(c_text());
        f.setBold(true);
        f.setPixelSize(14);
        p->setFont(f);
        p->drawText(px + 14, y,
                    QStringLiteral("TEI %1%2")
                        .arg(center)
                        .arg(center == 1 ? QStringLiteral(" (CCO)")
                                         : QStringLiteral(" (STA)")));
        y += 20;

        f.setBold(false);
        f.setPixelSize(10);
        p->setFont(f);
        p->setPen(c_dim());
        p->drawText(px + 14, y,
                    QStringLiteral("MAC %1").arg(nd.mac ? format_mac(nd.mac)
                                                        : QStringLiteral("-")));
        y += 18;

        // kv:邻居数 / 圈半径 / 双向
        const double radius = layout.radius.value(center, 0.0);
        int both = 0;
        for (quint16 t : nbrs)
            if (m_model.is_bidirectional(center, t)) ++both;
        p->setPen(c_text());
        p->drawText(px + 14, y,
                    QStringLiteral("邻居 %1 · 圈半径 %2 · 双向 %3/%4")
                        .arg(nbrs.size())
                        .arg(int(radius))
                        .arg(both)
                        .arg(nbrs.size()));
        y += 24;

        // 表格标题
        p->setPen(c_dim());
        f.setBold(true);
        f.setPixelSize(9);
        p->setFont(f);
        p->drawText(px + 12, y, QStringLiteral("邻居列表 Neighbors"));
        y += 4;
        p->drawLine(px + 10, y, px + panel_w - 10, y);
        y += 12;

        // 表头
        const int cx1 = px + 10;    // TEI
        const int cx2 = px + 64;    // 帧数
        const int cx3 = px + 104;   // 成功率
        const int cx4 = px + 152;   // 距离
        const int cx5 = px + 192;   // 关系
        p->drawText(cx1, y, QStringLiteral("TEI"));
        p->drawText(cx2, y, QStringLiteral("帧"));
        p->drawText(cx3, y, QStringLiteral("成功率"));
        p->drawText(cx4, y, QStringLiteral("距离"));
        p->drawText(cx5, y, QStringLiteral("关系"));
        y += 4;
        p->drawLine(px + 10, y, px + panel_w - 10, y);
        y += 11;

        // 表格行(按估算距离由近及远)
        QVector<quint16> sorted = nbrs;
        std::sort(sorted.begin(), sorted.end(), [&](quint16 a, quint16 b) {
            return m_model.est_distance(center, a) <
                   m_model.est_distance(center, b);
        });
        f.setBold(false);
        f.setPixelSize(9);
        p->setFont(f);
        m_table_rows.clear();
        const int row_h = 15;
        for (quint16 t : sorted) {
            const int cnt = m_model.discover_cnt.value(qMakePair(center, t), 0);
            const int rate = m_model.link_rate(center, t);
            const double d = m_model.est_distance(center, t);
            const bool mutual = m_model.is_bidirectional(center, t);
            p->setPen(c_text());
            p->drawText(cx1, y, QStringLiteral("#%1").arg(t));
            p->setPen(c_dim());
            p->drawText(cx2, y, QString::number(cnt));
            p->setPen(rate >= 0 ? rate_color(rate) : c_dim());
            p->drawText(cx3, y,
                        rate >= 0 ? QStringLiteral("%1%").arg(rate)
                                  : QStringLiteral("?"));
            p->drawText(cx4, y, QStringLiteral("%1m").arg(int(d)));
            p->setPen(mutual ? c_good() : c_warn());
            p->drawText(cx5, y, mutual ? QStringLiteral("↔ 双向")
                                       : QStringLiteral("→ 单向"));
            m_table_rows.append(
                qMakePair(t, QRectF(px + 8, y - 11, panel_w - 16, row_h)));
            y += row_h;
        }
        if (sorted.isEmpty()) {
            p->setPen(c_dim());
            p->drawText(px + 12, y, QStringLiteral("该节点没有发现任何邻居"));
            y += 16;
        }

        // 单向节点说明
        y += 8;
        p->setPen(c_dim());
        f.setPixelSize(9);
        p->setFont(f);
        const QString inb_text =
            inbound.isEmpty()
                ? QStringLiteral("所有听到我的节点也都在我的发现列表里")
                : QStringLiteral("听到我但我没听到的节点(单向):%1")
                      .arg([&]() {
                          QStringList s;
                          for (quint16 t : inbound)
                              s << QStringLiteral("#%1").arg(t);
                          return s.join(QStringLiteral("、"));
                      }());
        p->drawText(QRectF(px + 12, y, panel_w - 24, pb - y - 8),
                    Qt::TextWordWrap, inb_text);
    }

    /// @brief 底部说明条(还原网页底部 note)
    void draw_footer(QPainter* p, int w, int h, int footer_h) {
        const int fy = h - footer_h;
        p->fillRect(0, fy, w, footer_h, c_footer_bg());
        p->setPen(c_border());
        p->drawLine(0, fy, w, fy);
        p->setPen(c_dim());
        QFont f = cov_font(9);
        p->setFont(f);
        p->drawText(QRect(0, fy + 2, w, footer_h - 4), Qt::AlignCenter,
                    QStringLiteral("MDS 布局还原 · 圈内为该节点能听到的邻居 · "
                                   "实线=双向 / 虚线=单向 · 点击节点或右侧表格切换选中 · "
                                   "滚轮缩放 / 拖拽平移"));
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
    QVector<QPair<quint16, QRectF>> m_table_rows;  ///< 右侧邻居表格行矩形(点击切换选中)
    bool m_dropdown_open = false;                  ///< 「查看节点」下拉框是否展开
    QRectF m_dropdown_rect;                        ///< 下拉框本体矩形(点击切换展开)
    QVector<QPair<quint16, QRectF>> m_dropdown_items;  ///< 下拉列表项矩形
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
