/// @file plugin_panels.h
/// @brief 插件功能界面:按插件 panel 类型展示的 QWidget 面板
/// @details 面板类型(由 plugin.json "panel" 声明,缺省 graphics→topo,其余→stats):
///          topo   - 图形插件:定时 request_render,展示插件绘制的图像
///          replay - 回放插件:定时拉取 get_replay_data() 文本展示
///          diag   - 诊断插件:parse 结果 Diagnosis 子节点告警列表
///          report - 报表插件:按钮触发 get_report() 展示
///          stats  - 兜底:accept/reject 统计 + 插件描述
#ifndef BPLC_PLUGIN_PANELS_H
#define BPLC_PLUGIN_PANELS_H

#include <QWidget>

#include "local_plugin_engine.h"

/// @brief 注册插件面板相关中英文翻译(幂等;需在任何 trl::L("插件"...) 之前调用)
void register_plugin_panel_i18n();

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;
class QTimer;
class QVBoxLayout;

/// @brief 插件面板基类:accept/reject 统计条 + 插件描述
class PluginPanel : public QWidget {
    Q_OBJECT
public:
    PluginPanel(LocalPluginEngine* engine, const PluginLoadedInfo& info,
                QWidget* parent = nullptr);

    QString plugin_id() const { return m_info.plugin_id; }

protected:
    /// @brief 子类把展示控件放入的布局(统计条已占首行)
    QVBoxLayout* body_layout() { return m_body; }

    LocalPluginEngine* m_engine = nullptr;
    PluginLoadedInfo   m_info;

protected slots:
    void on_plugin_stats(const QString& pid, qint64 accept, qint64 reject);

private:
    QVBoxLayout* m_body = nullptr;
    QLabel*      m_stats = nullptr;
};

/// @brief 拓扑/图形面板
class TopoPluginPanel : public PluginPanel {
    Q_OBJECT
public:
    TopoPluginPanel(LocalPluginEngine* engine, const PluginLoadedInfo& info,
                    QWidget* parent = nullptr);
    void refresh_now();  ///< 立即请求一帧渲染(自动化测试用)

private slots:
    void on_refresh_tick();
    void on_render_ready(const QString& pid, const QImage& img);

private:
    QLabel* m_view = nullptr;
    QTimer* m_timer = nullptr;
};

/// @brief 回放数据面板(文本)
class ReplayPluginPanel : public PluginPanel {
    Q_OBJECT
public:
    ReplayPluginPanel(LocalPluginEngine* engine, const PluginLoadedInfo& info,
                      QWidget* parent = nullptr);
    void refresh_now();  ///< 立即拉取一次(自动化测试用)

private slots:
    void on_refresh_tick();
    void on_text_ready(const QString& pid, const QString& func,
                       const QString& text);

private:
    QPlainTextEdit* m_text = nullptr;
    QTimer*         m_timer = nullptr;
    QString         m_func;
};

/// @brief 诊断告警面板(表格)
class DiagPluginPanel : public PluginPanel {
    Q_OBJECT
public:
    DiagPluginPanel(LocalPluginEngine* engine, const PluginLoadedInfo& info,
                    QWidget* parent = nullptr);

private slots:
    void on_diag_alarms(const QString& pid,
                        const QList<PluginDiagAlarm>& alarms);

private:
    QTableWidget* m_table = nullptr;
};

/// @brief 报表面板(按钮生成)
class ReportPluginPanel : public PluginPanel {
    Q_OBJECT
public:
    ReportPluginPanel(LocalPluginEngine* engine, const PluginLoadedInfo& info,
                      QWidget* parent = nullptr);
    void generate_now();  ///< 立即生成一次(自动化测试用)

private slots:
    void on_generate();
    void on_text_ready(const QString& pid, const QString& func,
                       const QString& text);

private:
    QTextEdit*   m_text = nullptr;
    QPushButton* m_btn = nullptr;
    QString      m_func;
};

/// @brief 兜底统计面板
class StatsPluginPanel : public PluginPanel {
    Q_OBJECT
public:
    StatsPluginPanel(LocalPluginEngine* engine, const PluginLoadedInfo& info,
                     QWidget* parent = nullptr);
};

/// @brief 按 PluginLoadedInfo.panel 创建对应面板
PluginPanel* create_plugin_panel(LocalPluginEngine* engine,
                                 const PluginLoadedInfo& info,
                                 QWidget* parent = nullptr);

#endif // BPLC_PLUGIN_PANELS_H
