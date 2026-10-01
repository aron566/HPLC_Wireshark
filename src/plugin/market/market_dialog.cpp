/// @file market_dialog.cpp
/// @brief 插件市场对话框实现(VS Code 扩展视图式布局)
#include "market_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "../../common/i18n.h"

namespace {
// 中→英注册(文件级)
struct I18nRegMarket {
    I18nRegMarket() {
        trl::register_en("插件市场", "Plugin Marketplace");
        trl::register_en("搜索插件", "Search plugins");
        trl::register_en("%1 个插件", "%1 plugins");
        trl::register_en("全部", "All");
        trl::register_en("已安装", "Installed");
        trl::register_en("未安装", "Not installed");
        trl::register_en("已禁用", "Disabled");
        trl::register_en("已安装分组", "INSTALLED");
        trl::register_en("市场分组", "MARKETPLACE");
        trl::register_en("安装", "Install");
        trl::register_en("更新", "Update");
        trl::register_en("卸载", "Uninstall");
        trl::register_en("启用", "Enabled");
        trl::register_en("从文件安装...", "Install from file...");
        trl::register_en("刷新", "Refresh");
        trl::register_en("关闭", "Close");
        trl::register_en("版本", "Version");
        trl::register_en("作者", "Author");
        trl::register_en("分类", "Category");
        trl::register_en("可选版本", "Available versions");
        trl::register_en("已安装版本", "Installed version");
        trl::register_en("可更新到", "Update available");
        trl::register_en("诊断", "Diagnosis");
        trl::register_en("报表", "Report");
        trl::register_en("图形", "Graphics");
        trl::register_en("协议解析", "Protocol");
        trl::register_en("正在加载市场...", "Loading marketplace...");
        trl::register_en("市场加载失败", "Marketplace load failed");
        trl::register_en("请选择插件包 (*.zip)", "Select plugin package (*.zip)");
        trl::register_en("插件包", "Plugin packages");
        trl::register_en("安装成功", "Installed successfully");
        trl::register_en("安装失败", "Install failed");
        trl::register_en("确定卸载该插件吗?", "Uninstall this plugin?");
        trl::register_en("需要重启插件加载才能生效(重新选择插件目录或重启程序)。",
                         "Takes effect after plugins are reloaded (reselect the plugin dir or restart).");
        trl::register_en("未选择插件", "No plugin selected");
    }
};
static I18nRegMarket g_i18n_reg;

/// @brief 按名字哈希生成分类色块图标(首字母)
QPixmap letter_icon(const QString& name, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    uint h = qHash(name);
    const QColor bg = QColor::fromHsv(int(h % 360), 90, 200);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, size, size, 6, 6);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(size * 2 / 5);
    p.setFont(f);
    const QString ch = name.isEmpty() ? QStringLiteral("?")
                                      : name.left(1).toUpper();
    p.drawText(pm.rect(), Qt::AlignCenter, ch);
    return pm;
}

QString elide_one_line(const QString& s, int max_chars = 60) {
    const QString t = s.simplified();
    if (t.length() <= max_chars) return t;
    return t.left(max_chars - 1) + QChar(0x2026);
}

}  // namespace

PluginMarketDialog::PluginMarketDialog(QWidget* parent)
    : QDialog(parent) {
    setup_ui();
    m_market = new PluginMarket(this);
    connect(m_market, &PluginMarket::feed_ready, this,
            &PluginMarketDialog::on_feed_ready);
    connect(m_market, &PluginMarket::feed_error, this,
            &PluginMarketDialog::on_feed_error);
    connect(m_market, &PluginMarket::install_progress, this,
            &PluginMarketDialog::on_install_progress);
    connect(m_market, &PluginMarket::install_finished, this,
            &PluginMarketDialog::on_install_finished);
    refresh_installed();
    on_refresh();
    rebuild_list();
}

PluginMarketDialog::~PluginMarketDialog() = default;

void PluginMarketDialog::setup_ui() {
    setWindowTitle(trl::L("插件市场"));
    resize(1020, 640);

    auto* root = new QVBoxLayout(this);

    // 顶栏:搜索 + 过滤
    auto* top = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(trl::L("搜索插件"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this,
            &PluginMarketDialog::on_search_changed);
    m_filter = new QComboBox(this);
    m_filter->addItem(trl::L("全部"), 0);
    m_filter->addItem(trl::L("已安装"), 1);
    m_filter->addItem(trl::L("未安装"), 2);
    connect(m_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PluginMarketDialog::on_filter_changed);
    top->addWidget(m_search, 1);
    top->addWidget(m_filter);
    root->addLayout(top);

    // 主体:左列表 + 右详情
    auto* split = new QSplitter(Qt::Horizontal, this);
    m_list = new QListWidget(split);
    m_list->setUniformItemSizes(false);
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) {
                on_selection_changed();
            });
    split->addWidget(m_list);

    auto* detail = new QWidget(split);
    auto* dl = new QVBoxLayout(detail);
    auto* head = new QHBoxLayout();
    m_d_icon = new QLabel(detail);
    m_d_icon->setFixedSize(56, 56);
    m_d_name = new QLabel(detail);
    {
        QFont f = m_d_name->font();
        f.setPointSize(f.pointSize() + 4);
        f.setBold(true);
        m_d_name->setFont(f);
    }
    m_d_name->setWordWrap(true);
    head->addWidget(m_d_icon);
    head->addWidget(m_d_name, 1);
    dl->addLayout(head);
    m_d_meta = new QLabel(detail);
    m_d_meta->setWordWrap(true);
    dl->addWidget(m_d_meta);
    auto* btns = new QHBoxLayout();
    m_btn_install = new QPushButton(trl::L("安装"), detail);
    m_btn_uninstall = new QPushButton(trl::L("卸载"), detail);
    m_chk_enabled = new QCheckBox(trl::L("启用"), detail);
    connect(m_btn_install, &QPushButton::clicked, this,
            &PluginMarketDialog::on_install_clicked);
    connect(m_btn_uninstall, &QPushButton::clicked, this,
            &PluginMarketDialog::on_uninstall_clicked);
    connect(m_chk_enabled, &QCheckBox::toggled, this,
            &PluginMarketDialog::on_enabled_toggled);
    btns->addWidget(m_btn_install);
    btns->addWidget(m_btn_uninstall);
    btns->addWidget(m_chk_enabled);
    btns->addStretch(1);
    dl->addLayout(btns);
    m_d_desc = new QLabel(detail);
    m_d_desc->setWordWrap(true);
    m_d_desc->setAlignment(Qt::AlignTop);
    dl->addWidget(m_d_desc, 1);
    m_d_versions = new QLabel(detail);
    m_d_versions->setWordWrap(true);
    dl->addWidget(m_d_versions);
    split->addWidget(detail);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 6);
    root->addWidget(split, 1);

    m_status = new QLabel(this);
    root->addWidget(m_status);

    auto* bottom = new QHBoxLayout();
    auto* btn_file = new QPushButton(trl::L("从文件安装..."), this);
    auto* btn_refresh = new QPushButton(trl::L("刷新"), this);
    auto* btn_close = new QPushButton(trl::L("关闭"), this);
    connect(btn_file, &QPushButton::clicked, this,
            &PluginMarketDialog::on_install_from_file);
    connect(btn_refresh, &QPushButton::clicked, this,
            &PluginMarketDialog::on_refresh);
    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(btn_file);
    bottom->addWidget(btn_refresh);
    bottom->addStretch(1);
    bottom->addWidget(btn_close);
    root->addLayout(bottom);
}

void PluginMarketDialog::refresh_installed() {
    m_installed = m_market->installed_plugins();
}

QString PluginMarketDialog::category_name(const QString& cat) const {
    if (cat == QStringLiteral("diagnosis")) return trl::L("诊断");
    if (cat == QStringLiteral("report")) return trl::L("报表");
    if (cat == QStringLiteral("graphics")) return trl::L("图形");
    if (cat == QStringLiteral("protocol")) return trl::L("协议解析");
    return cat;
}

void PluginMarketDialog::on_feed_ready(const QList<MarketPlugin>& plugins) {
    m_feed = plugins;
    m_status->setText(trl::L("%1 个插件").arg(plugins.size()));
    rebuild_list();
}

void PluginMarketDialog::on_feed_error(const QString& error) {
    m_status->setText(trl::L("市场加载失败") + QStringLiteral(": ") + error);
}

void PluginMarketDialog::on_install_progress(const QString& text) {
    m_status->setText(text);
}

void PluginMarketDialog::on_install_finished(bool ok, const QString& error,
                                             const QString& name) {
    Q_UNUSED(name);
    if (ok) {
        m_status->setText(trl::L("安装成功"));
        refresh_installed();
        rebuild_list();
    } else {
        m_status->setText(trl::L("安装失败") + QStringLiteral(": ") + error);
        QMessageBox::warning(this, trl::L("安装失败"), error);
    }
}

bool PluginMarketDialog::row_matches(const RowEntry& e) const {
    const int f = m_filter->currentData().toInt();
    if (f == 1 && !e.is_installed_group) return false;
    if (f == 2 && e.is_installed_group) return false;
    const QString q = m_search->text().trimmed().toLower();
    if (q.isEmpty()) return true;
    if (e.is_installed_group) {
        const InstalledPlugin& ip = m_installed[e.installed_index];
        return ip.manifest.name.toLower().contains(q) ||
               ip.manifest.display_name.toLower().contains(q) ||
               ip.manifest.display_name_en.toLower().contains(q) ||
               ip.manifest.author.toLower().contains(q);
    }
    const MarketPlugin& p = m_feed[e.feed_index];
    return p.name.toLower().contains(q) ||
           p.display_name.toLower().contains(q) ||
           p.display_name_en.toLower().contains(q) ||
           p.author.toLower().contains(q);
}

/// @brief 列表卡片:图标 + 名称/作者/描述 + 右侧状态徽标
QWidget* make_card(const QString& icon_key, const QString& title,
                   const QString& sub, const QString& desc,
                   const QString& badge) {
    auto* w = new QWidget();
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(6, 6, 6, 6);
    auto* icon = new QLabel(w);
    icon->setPixmap(letter_icon(icon_key, 40));
    icon->setFixedSize(40, 40);
    h->addWidget(icon);
    auto* v = new QVBoxLayout();
    auto* l1 = new QHBoxLayout();
    auto* t = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), w);
    l1->addWidget(t, 1);
    if (!badge.isEmpty()) {
        auto* b = new QLabel(
            QStringLiteral("<font color=\"#2e7d32\">%1</font>")
                .arg(badge.toHtmlEscaped()),
            w);
        l1->addWidget(b);
    }
    v->addLayout(l1);
    auto* s = new QLabel(sub.toHtmlEscaped(), w);
    s->setEnabled(false);
    auto* d = new QLabel(elide_one_line(desc).toHtmlEscaped(), w);
    v->addWidget(s);
    v->addWidget(d);
    h->addLayout(v, 1);
    return w;
}

void PluginMarketDialog::rebuild_list() {
    m_list->clear();
    m_rows.clear();
    const bool en = trl::enabled();

    auto add_header = [this](const QString& text) {
        auto* it = new QListWidgetItem(text, m_list);
        it->setFlags(Qt::NoItemFlags);
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
        it->setBackground(QColor(0, 0, 0, 18));
    };

    // 已安装分组
    QList<RowEntry> inst_rows;
    for (int i = 0; i < m_installed.size(); ++i) {
        RowEntry e;
        e.is_installed_group = true;
        e.installed_index = i;
        e.name = m_installed[i].manifest.name;
        if (row_matches(e)) inst_rows.append(e);
    }
    if (!inst_rows.isEmpty()) {
        add_header(trl::L("已安装分组"));
        for (const RowEntry& e : inst_rows) {
            const InstalledPlugin& ip = m_installed[e.installed_index];
            const MarketVersion* upd =
                PluginMarket::update_for(ip, m_feed);
            QString badge = ip.enabled ? QString() : trl::L("已禁用");
            if (upd) badge = trl::L("可更新到") + " " + upd->version;
            auto* card = make_card(
                ip.manifest.name,
                plugin_display_name(ip.manifest, en),
                trl::L("作者") + ": " + ip.manifest.author + "  " +
                    trl::L("版本") + ": " + ip.manifest.version,
                plugin_description(ip.manifest, en), badge);
            auto* it = new QListWidgetItem(m_list);
            it->setSizeHint(card->sizeHint());
            m_list->setItemWidget(it, card);
            m_rows.append(e);
            it->setData(Qt::UserRole, m_rows.size() - 1);
        }
    }

    // 市场分组(排除已安装)
    QSet<QString> installed_names;
    for (const InstalledPlugin& ip : m_installed)
        installed_names.insert(ip.manifest.name);
    QList<RowEntry> mkt_rows;
    for (int i = 0; i < m_feed.size(); ++i) {
        if (installed_names.contains(m_feed[i].name)) continue;
        RowEntry e;
        e.is_installed_group = false;
        e.feed_index = i;
        e.name = m_feed[i].name;
        if (row_matches(e)) mkt_rows.append(e);
    }
    if (!mkt_rows.isEmpty()) {
        add_header(trl::L("市场分组"));
        for (const RowEntry& e : mkt_rows) {
            const MarketPlugin& p = m_feed[e.feed_index];
            const MarketVersion* lat = p.latest();
            auto* card = make_card(
                p.name, p.localized_name(en),
                trl::L("作者") + ": " + p.author + "  " +
                    trl::L("版本") + ": " +
                    (lat ? lat->version : QStringLiteral("-")) + "  " +
                    trl::L("分类") + ": " + category_name(p.category),
                p.localized_desc(en), QString());
            auto* it = new QListWidgetItem(m_list);
            it->setSizeHint(card->sizeHint());
            m_list->setItemWidget(it, card);
            m_rows.append(e);
            it->setData(Qt::UserRole, m_rows.size() - 1);
        }
    }
    if (m_list->count() > 0) {
        // 选中第一个可选行
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->flags() & Qt::ItemIsSelectable) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
    update_detail();
}

void PluginMarketDialog::on_selection_changed() {
    update_detail();
}

void PluginMarketDialog::on_search_changed(const QString&) { rebuild_list(); }
void PluginMarketDialog::on_filter_changed(int) { rebuild_list(); }

void PluginMarketDialog::on_refresh() {
    m_status->setText(trl::L("正在加载市场..."));
    refresh_installed();
    m_market->fetch_feed(PluginMarket::default_feed_url());
    rebuild_list();
}

void PluginMarketDialog::on_install_from_file() {
    const QString zip = QFileDialog::getOpenFileName(
        this, trl::L("请选择插件包 (*.zip)"), QString(),
        trl::L("插件包") + QStringLiteral(" (*.zip)"));
    if (zip.isEmpty()) return;
    m_status->setText(trl::L("安装") + QStringLiteral("..."));
    m_market->install_from_file(zip);
}

void PluginMarketDialog::update_detail() {
    const bool en = trl::enabled();
    QListWidgetItem* cur = m_list->currentItem();
    const bool has_sel =
        cur && (cur->flags() & Qt::ItemIsSelectable) &&
        cur->data(Qt::UserRole).isValid();
    if (!has_sel) {
        m_current_name.clear();
        m_d_icon->clear();
        m_d_name->setText(trl::L("未选择插件"));
        m_d_meta->clear();
        m_d_desc->clear();
        m_d_versions->clear();
        m_btn_install->setEnabled(false);
        m_btn_uninstall->setEnabled(false);
        m_chk_enabled->setEnabled(false);
        return;
    }
    const RowEntry& e = m_rows[cur->data(Qt::UserRole).toInt()];
    m_current_name = e.name;
    m_d_icon->setPixmap(letter_icon(e.name, 56));

    if (e.is_installed_group) {
        const InstalledPlugin& ip = m_installed[e.installed_index];
        m_d_name->setText(plugin_display_name(ip.manifest, en));
        m_d_meta->setText(
            trl::L("作者") + ": " + ip.manifest.author + "\n" +
            trl::L("已安装版本") + ": " + ip.manifest.version);
        m_d_desc->setText(plugin_description(ip.manifest, en));
        const MarketVersion* upd = PluginMarket::update_for(ip, m_feed);
        if (upd) {
            m_d_versions->setText(trl::L("可更新到") + ": " + upd->version);
            m_btn_install->setText(trl::L("更新"));
            m_btn_install->setEnabled(true);
        } else {
            m_d_versions->clear();
            m_btn_install->setEnabled(false);
            m_btn_install->setText(trl::L("安装"));
        }
        m_btn_uninstall->setEnabled(true);
        m_chk_enabled->setEnabled(true);
        m_chk_enabled->blockSignals(true);
        m_chk_enabled->setChecked(ip.enabled);
        m_chk_enabled->blockSignals(false);
    } else {
        const MarketPlugin& p = m_feed[e.feed_index];
        const MarketVersion* lat = p.latest();
        m_d_name->setText(p.localized_name(en));
        m_d_meta->setText(
            trl::L("作者") + ": " + p.author + "\n" +
            trl::L("版本") + ": " + (lat ? lat->version : "-") + "\n" +
            trl::L("分类") + ": " + category_name(p.category));
        m_d_desc->setText(p.localized_desc(en));
        QStringList vs;
        for (const MarketVersion& v : p.versions) vs.prepend(v.version);
        m_d_versions->setText(trl::L("可选版本") + ": " + vs.join(", "));
        m_btn_install->setText(trl::L("安装"));
        m_btn_install->setEnabled(lat && PluginMarket::app_version_ok(
                                              lat->min_app_version));
        m_btn_uninstall->setEnabled(false);
        m_chk_enabled->setEnabled(false);
    }
}

void PluginMarketDialog::on_install_clicked() {
    if (m_current_name.isEmpty()) return;
    // 已安装且有更新 → 装最新版;未安装 → 装最新版
    for (const MarketPlugin& p : m_feed) {
        if (p.name != m_current_name) continue;
        const MarketVersion* lat = p.latest();
        if (!lat) return;
        m_market->install_market_plugin(p, p.versions.size() - 1);
        return;
    }
}

void PluginMarketDialog::on_uninstall_clicked() {
    if (m_current_name.isEmpty()) return;
    if (QMessageBox::question(this, trl::L("卸载"),
                              trl::L("确定卸载该插件吗?")) !=
        QMessageBox::Yes)
        return;
    if (m_market->uninstall(m_current_name)) {
        m_status->setText(trl::L("安装成功"));
        refresh_installed();
        rebuild_list();
    }
}

void PluginMarketDialog::on_enabled_toggled(bool on) {
    if (m_current_name.isEmpty()) return;
    if (m_market->set_enabled(m_current_name, on)) {
        m_status->setText(
            trl::L("需要重启插件加载才能生效(重新选择插件目录或重启程序)。"));
        refresh_installed();
        rebuild_list();
    }
}
