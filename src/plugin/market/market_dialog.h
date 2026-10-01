/// @file market_dialog.h
/// @brief 插件市场对话框:VS Code 扩展视图式布局
/// @details 左侧:搜索框 + 过滤器 + 插件卡片列表(分"已安装"/"市场"分组);
///          右侧:选中插件的详情面板(描述/版本/安装/更新/卸载/启用)。
///          全部用户可见文本经 trl::L() 做中英切换。
#ifndef BPLC_MARKET_DIALOG_H
#define BPLC_MARKET_DIALOG_H

#include <QDialog>
#include <QList>

#include "plugin_market.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

/// @brief 市场对话框
class PluginMarketDialog : public QDialog {
    Q_OBJECT
public:
    explicit PluginMarketDialog(QWidget* parent = nullptr);
    ~PluginMarketDialog() override;

private slots:
    void on_feed_ready(const QList<MarketPlugin>& plugins);
    void on_feed_error(const QString& error);
    void on_install_progress(const QString& text);
    void on_install_finished(bool ok, const QString& error,
                             const QString& name);
    void rebuild_list();
    void on_selection_changed();
    void on_search_changed(const QString& text);
    void on_filter_changed(int index);
    void on_refresh();
    void on_install_from_file();
    void on_install_clicked();
    void on_uninstall_clicked();
    void on_enabled_toggled(bool on);

private:
    struct RowEntry {
        bool        is_installed_group;  ///< true=已安装分组,false=市场分组
        QString     name;                ///< 插件 name
        int         feed_index = -1;     ///< feed 下标(市场分组)
        int         installed_index = -1;///< installed 下标(已安装分组)
    };

    void setup_ui();
    void refresh_installed();
    QString category_name(const QString& cat) const;
    bool    row_matches(const RowEntry& e) const;
    void    update_detail();

    PluginMarket* m_market = nullptr;
    QList<MarketPlugin>    m_feed;
    QList<InstalledPlugin> m_installed;
    QList<RowEntry>        m_rows;
    QString                m_current_name;

    QLineEdit*   m_search = nullptr;
    QComboBox*   m_filter = nullptr;
    QListWidget* m_list = nullptr;
    QLabel*      m_status = nullptr;
    // detail
    QLabel*      m_d_icon = nullptr;
    QLabel*      m_d_name = nullptr;
    QLabel*      m_d_meta = nullptr;
    QLabel*      m_d_desc = nullptr;
    QLabel*      m_d_versions = nullptr;
    QPushButton* m_btn_install = nullptr;
    QPushButton* m_btn_uninstall = nullptr;
    QCheckBox*   m_chk_enabled = nullptr;
};

#endif // BPLC_MARKET_DIALOG_H
