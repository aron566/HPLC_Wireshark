/// @file topo_window.h
/// @brief 拓扑(Topo)独立窗口:多 NID 下拉切换 + 拓扑图 + 路由变更表 + TEI→MAC 表
/// @details 与 app/io/ui 解耦,只依赖 topo_state(数据模型) + Qt widgets。
///          MainWindow 持有 QHash<quint32, TopoState> 并传入本窗口指针引用,
///          窗口按需读取当前 NID 的状态绘制/列表,不改动状态。
#ifndef TOPO_WINDOW_H
#define TOPO_WINDOW_H

#include "topo_state.h"
#include <QHash>
#include <QList>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QTableView;
class QStandardItemModel;
class QPixmap;
class QTimer;

/// @brief 自绘层次拓扑图(CCO 顶层,STA 按层级向下展开,父子连线)
class TopoGraphWidget : public QWidget {
    Q_OBJECT
public:
    explicit TopoGraphWidget(QWidget* parent = nullptr);
    void set_state(const TopoState* state);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    /// @brief 计算节点布局坐标(世界坐标,自上而下层次树)
    void relayout();
    /// @brief widget 坐标 → 世界坐标(逆缩放/平移)
    QPointF to_world(const QPointF& w) const;
    /// @brief 命中世界坐标处的节点(返回 tei + mac;无命中返回 false)
    bool hit_node(const QPointF& world, quint16& tei, quint64& mac, QRectF& box) const;
    /// @brief 显示节点悬浮信息(通讯成功率等)
    void show_node_tip(const QPoint& widget_pos, quint16 tei, quint64 mac);

    const TopoState* m_state = nullptr;
    struct NodePos {
        quint16 tei;
        quint64 mac = 0; ///< 正在入网(关联请求)节点的 MAC;tei==0 时有效
        int     level;
        QPointF pos;
    };
    QVector<NodePos> m_layout;
    QHash<quint16, QPointF> m_pos; ///< tei → 坐标(连线用)
    QPixmap* m_icon_cco = nullptr;          ///< CCO 节点图标(cco-router.png)
    QPixmap* m_icon_online = nullptr;       ///< STA 已入网(electric-meter_online.png)
    QPixmap* m_icon_online_going = nullptr; ///< STA 正在入网(electric-meter _online_going.png)
    QPixmap* m_icon_offline = nullptr;      ///< STA 离线(electric-meter _offline.png)

    // 视图变换:世界坐标 × scale + offset = widget 坐标
    QPointF m_offset;         ///< 平移偏移
    qreal   m_scale = 1.0;    ///< 缩放比例
    qreal   m_canvas_w = 400.0;
    qreal   m_canvas_h = 300.0;
    QPointF m_drag_start;     ///< 拖拽起点(widget 坐标)
    QPointF m_press_widget;   ///< 按下位置(判断点击 vs 拖拽)
    bool    m_dragging = false;
};

/// @brief 拓扑独立窗口
class TopoWindow : public QWidget {
    Q_OBJECT
public:
    explicit TopoWindow(QWidget* parent = nullptr);

    /// @brief 刷新 NID 下拉列表(从 states map 收集)
    void refresh_nids();
    /// @brief 设置状态表引用(不拷贝;由 MainWindow 持有)
    void set_state_map(const QHash<quint32, TopoState>* map);
    /// @brief 切到指定 NID(存在则切换并刷新)
    void set_current_nid(quint32 nid);
    /// @brief 当前 NID(0=无)
    quint32 current_nid() const { return m_current_nid; }
    /// @brief 实时刷新:重新收集 NID 并刷新当前 NID 的图与表(新帧到达时由 MainWindow 调用)
    void refresh_current();
    /// @brief 标记有新拓扑数据(轻量,只置脏标志;定时器批量刷新,避免高频全量重建卡顿)
    void mark_dirty();

protected:
    /// @brief 关闭 = 隐藏(保留状态,下次打开复用)
    void closeEvent(QCloseEvent* e) override;

private slots:
    void on_nid_changed(int idx);
    void on_routes_filter(const QString& text);
    void on_teimac_search(const QString& text);

private:
    void rebuild_all();
    void rebuild_routes_table();
    void rebuild_teimac_table();

    QComboBox*          m_nid_combo;
    TopoGraphWidget*    m_graph;
    QTableView*         m_routes_table;
    QStandardItemModel* m_routes_model;
    QLineEdit*          m_routes_filter;
    QTableView*         m_teimac_table;
    QStandardItemModel* m_teimac_model;
    QLineEdit*          m_teimac_search;

    const QHash<quint32, TopoState>* m_states = nullptr;
    quint32 m_current_nid = 0;
    bool    m_dirty = false;         ///< 有新拓扑数据待刷新
    QTimer* m_refresh_timer = nullptr; ///< 节流定时器(批量刷新,防高频全量重建卡顿)

    // 路由变更表滚动逻辑(与主界面 Table View 一致):
    // 用户滚到底部(最新)才自动跟随;滚离底部则保持当前位置,不强制跳回
    bool m_routes_follow_bottom = true; ///< 是否在底部(跟随最新)
    bool m_rebuilding_routes = false;   ///< 重建期间屏蔽 scrollbar 信号干扰
};

#endif // TOPO_WINDOW_H
