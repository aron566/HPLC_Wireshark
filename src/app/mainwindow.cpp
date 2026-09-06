/// @file mainwindow.cpp
/// @brief 主窗口实现
#include "mainwindow.h"

#include "packetlistmodel.h"
#include "hexview.h"
#include "protocoltree.h"
#include "commconfigdialog.h"
#include "QSimpleUpdater.h"

#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QLineEdit>
#include <QLabel>
#include <QTableView>
#include <QHeaderView>
#include <QSplitter>
#include <QStatusBar>
#include <QMenuBar>
#include <QSettings>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMutexLocker>
#include <QDateTime>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QString>
#include <QMessageBox>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>

#include <QIcon>

namespace {
// 更新检查地址与当前版本(发布时改为正式服务器/仓库后同步更新 README)
const QString kUpdateUrl =
    QStringLiteral("https://raw.githubusercontent.com/aron566/HPLC_Wireshark/main/update.json");
const QString kAppVersion = QStringLiteral("1.0.2");
const QString kModuleName = QStringLiteral("BPLC STA Monitor");
const QString kAuthorName = QStringLiteral("aron566");
const QString kRepoUrl    = QStringLiteral("https://github.com/aron566/HPLC_Wireshark");
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_toolbar(nullptr), m_btn_start(nullptr), m_btn_stop(nullptr),
      m_btn_pause(nullptr), m_btn_clear(nullptr), m_btn_export(nullptr),
      m_btn_settings(nullptr), m_edt_filter(nullptr), m_btn_apply_filter(nullptr),
      m_splitter_main(nullptr), m_table_packets(nullptr),
      m_splitter_bottom(nullptr), m_tree_protocol(nullptr),
      m_hex_view(nullptr), m_lbl_hex_title(nullptr),
      m_status_left(nullptr), m_status_right(nullptr),
      m_reader(nullptr), m_dispatch(nullptr), m_model(nullptr),
      m_flush_timer(nullptr), m_status_timer(nullptr),
      m_paused(false), m_last_epoch_ms(0), m_index_counter(0) {
    qRegisterMetaType<BplcParser::Result>("BplcParser::Result");
    qRegisterMetaType<BplcFrame>("BplcFrame");
    qRegisterMetaType<ReaderConfig>("ReaderConfig");
    qRegisterMetaType<BplcParser::Filter>("BplcParser::Filter");
    qRegisterMetaType<PacketEntry>("PacketEntry");

    build_ui();
    wire_signals();

    m_reader   = new SerialReader(this);
    m_dispatch = new FrameDispatcher(this);
    m_dispatch->connect_source(m_reader);
    connect(m_dispatch, &FrameDispatcher::parsed,
            this,       &MainWindow::on_parsed,
            Qt::QueuedConnection);
    connect(m_reader, &SerialReader::status_message,
            this,      &MainWindow::on_status_message);
    connect(m_reader, &SerialReader::error_occurred,
            this,      &MainWindow::on_error);

    m_flush_timer = new QTimer(this);
    connect(m_flush_timer, &QTimer::timeout, this, &MainWindow::on_flush_buffer);
    m_flush_timer->start(100);

    m_status_timer = new QTimer(this);
    connect(m_status_timer, &QTimer::timeout, this, &MainWindow::refresh_status_bar);
    m_status_timer->start(500);

    m_splitter_main->setStretchFactor(0, 6);
    m_splitter_main->setStretchFactor(1, 4);
    m_splitter_bottom->setStretchFactor(0, 5);
    m_splitter_bottom->setStretchFactor(1, 4);

    QPalette pal = m_table_packets->palette();
    pal.setColor(QPalette::Highlight, QColor("#3d6f9f"));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    m_table_packets->setPalette(pal);

    // 注意:不可用裸 "QWidget" 选择器——它会命中 QMenu 等所有子类,
    // 把弹出菜单拖进 QSS 绘制路径,导致菜单项文字与快捷键列重叠。
    // 普通 QWidget 默认透明,背景透出 QMainWindow/QSplitter 即可。
    setStyleSheet(R"(
        QMainWindow { background-color: #f0f0f0; }
        QSplitter   { background-color: #f0f0f0; }
        QToolBar { background-color: #e8e8e8; border-bottom: 1px solid #c0c0c0; spacing: 2px; }
        QLineEdit { padding: 2px 4px; }
        QTableView { background-color: white; alternate-background-color: #f7f7f7; }
        QTreeWidget { background-color: white; alternate-background-color: #f7f7f7; }
    )");

    setWindowTitle(QStringLiteral("BPLC STA Monitor v%1 — Wireshark style").arg(kAppVersion));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    resize(1280, 800);
    m_status_left->setText(QStringLiteral("Ready — Ctrl+E 开始捕获,Ctrl+L 清空"));
}

MainWindow::~MainWindow() = default;

void MainWindow::build_ui() {
    m_toolbar = addToolBar(QStringLiteral("Main"));
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    m_btn_start = new QToolButton(m_toolbar); m_btn_start->setText(QStringLiteral("开始"));       m_toolbar->addWidget(m_btn_start);
    m_btn_stop  = new QToolButton(m_toolbar); m_btn_stop->setText(QStringLiteral("停止"));        m_toolbar->addWidget(m_btn_stop);
    m_btn_pause = new QToolButton(m_toolbar); m_btn_pause->setText(QStringLiteral("暂停"));
    m_btn_pause->setCheckable(true);                                                                   m_toolbar->addWidget(m_btn_pause);
    m_btn_clear = new QToolButton(m_toolbar); m_btn_clear->setText(QStringLiteral("清空"));        m_toolbar->addWidget(m_btn_clear);
    m_toolbar->addSeparator();
    m_btn_export = new QToolButton(m_toolbar);  m_btn_export->setText(QStringLiteral("导出"));     m_toolbar->addWidget(m_btn_export);
    m_btn_settings = new QToolButton(m_toolbar);m_btn_settings->setText(QStringLiteral("设置"));    m_toolbar->addWidget(m_btn_settings);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(new QLabel(QStringLiteral("  显示过滤器:"), m_toolbar));
    m_edt_filter = new QLineEdit(m_toolbar);
    m_edt_filter->setPlaceholderText(QStringLiteral("beacon | sof | hrf | plc | sta-3 | drop | 0xf0f1f2 ..."));
    m_edt_filter->setMinimumWidth(280);
    m_toolbar->addWidget(m_edt_filter);
    m_btn_apply_filter = new QToolButton(m_toolbar);
    m_btn_apply_filter->setText(QStringLiteral("应用"));
    m_toolbar->addWidget(m_btn_apply_filter);

    m_splitter_main = new QSplitter(Qt::Vertical, this);

    m_model = new PacketListModel(this);
    m_table_packets = new QTableView(m_splitter_main);
    m_table_packets->setModel(m_model);
    m_table_packets->setAlternatingRowColors(true);
    m_table_packets->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table_packets->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table_packets->setSortingEnabled(true);
    m_table_packets->setShowGrid(false);
    m_table_packets->verticalHeader()->setVisible(false);
    m_table_packets->horizontalHeader()->setStretchLastSection(true);
    m_table_packets->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QHeaderView* hh = m_table_packets->horizontalHeader();
    hh->resizeSection(PacketListModel::COL_INDEX,    60);
    hh->resizeSection(PacketListModel::COL_TIME,     120);
    hh->resizeSection(PacketListModel::COL_DELTA,    80);
    hh->resizeSection(PacketListModel::COL_SOURCE,   100);
    hh->resizeSection(PacketListModel::COL_DEST,     100);
    hh->resizeSection(PacketListModel::COL_PROTOCOL, 70);
    hh->resizeSection(PacketListModel::COL_FRAME_TYPE, 90);
    hh->resizeSection(PacketListModel::COL_MSDU_TYPE, 140);
    hh->resizeSection(PacketListModel::COL_MSDU_SEQ, 90);
    hh->resizeSection(PacketListModel::COL_LENGTH,   60);

    m_splitter_main->addWidget(m_table_packets);

    m_splitter_bottom = new QSplitter(Qt::Horizontal, m_splitter_main);

    m_tree_protocol = new ProtocolTree(m_splitter_bottom);
    m_tree_protocol->setMinimumWidth(280);

    auto* hex_pane   = new QWidget(m_splitter_bottom);
    auto* hex_layout = new QVBoxLayout(hex_pane);
    hex_layout->setContentsMargins(0, 0, 0, 0);
    m_lbl_hex_title = new QLabel(QStringLiteral("字节视图(十六进制,左偏移 + 中间 hex + 右侧 ASCII):"),
                                 hex_pane);
    m_hex_view = new HexView(hex_pane);
    hex_layout->addWidget(m_lbl_hex_title);
    hex_layout->addWidget(m_hex_view, 1);

    m_splitter_bottom->addWidget(m_tree_protocol);
    m_splitter_bottom->addWidget(hex_pane);
    m_splitter_main->addWidget(m_splitter_bottom);

    setCentralWidget(m_splitter_main);

    m_status_left = new QLabel(QStringLiteral("Ready"), this);
    m_status_right = new QLabel(this);
    m_status_right->setAlignment(Qt::AlignRight);
    statusBar()->addWidget(m_status_left, 1);
    statusBar()->addPermanentWidget(m_status_right, 2);

    auto* menu_capture = menuBar()->addMenu(QStringLiteral("捕获(&C)"));
    auto* act_start = menu_capture->addAction(QStringLiteral("开始"));
    act_start->setShortcut(QKeySequence("Ctrl+E"));
    connect(act_start, &QAction::triggered, this, &MainWindow::on_start);
    auto* act_stop = menu_capture->addAction(QStringLiteral("停止"));
    act_stop->setShortcut(QKeySequence("Ctrl+."));
    connect(act_stop, &QAction::triggered, this, &MainWindow::on_stop);
    auto* act_pause = menu_capture->addAction(QStringLiteral("暂停"));
    act_pause->setShortcut(QKeySequence("Ctrl+P"));
    act_pause->setCheckable(true);
    connect(act_pause, &QAction::toggled, this, &MainWindow::on_pause);
    menu_capture->addSeparator();
    auto* act_clear = menu_capture->addAction(QStringLiteral("清空"));
    act_clear->setShortcut(QKeySequence("Ctrl+L"));
    connect(act_clear, &QAction::triggered, this, &MainWindow::on_clear);
    menu_capture->addSeparator();
    auto* act_export = menu_capture->addAction(QStringLiteral("导出..."));
    connect(act_export, &QAction::triggered, this, &MainWindow::on_export);

    auto* menu_analyze = menuBar()->addMenu(QStringLiteral("分析(&A)"));
    auto* act_filt = menu_analyze->addAction(QStringLiteral("应用显示过滤器"));
    act_filt->setShortcut(QKeySequence("Ctrl+F"));
    connect(act_filt, &QAction::triggered, this, &MainWindow::on_apply_filter);

    // ---- 帮助菜单:检查更新 / 关于 ----
    auto* menu_help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto* act_check = menu_help->addAction(QStringLiteral("检查更新(&U)..."));
    connect(act_check, &QAction::triggered, this, [this]() {
        auto* su = QSimpleUpdater::getInstance();
        su->setModuleVersion(kUpdateUrl, kAppVersion);
        su->setModuleName(kUpdateUrl, kModuleName);
        su->setNotifyOnUpdate(kUpdateUrl, true);   // 发现新版本 → 弹窗询问下载
        su->setNotifyOnFinish(kUpdateUrl, true);   // 无新版本/清单正常 → 弹窗告知
        m_status_left->setText(QStringLiteral("正在检查更新…"));
        su->checkForUpdates(kUpdateUrl);
    });
    // 检查更新结束(无论结果)在状态栏留痕;失败(网络/清单)时以保守文案提示。
    // 注意:不用 Qt::UniqueConnection + lambda(Qt6 断言要求成员函数指针)。
    connect(QSimpleUpdater::getInstance(), &QSimpleUpdater::checkingFinished,
            this, &MainWindow::on_check_finished);
    auto* act_about = menu_help->addAction(QStringLiteral("关于(&A)"));
    connect(act_about, &QAction::triggered, this, [this]() {
        // 作者/仓库信息 + “打开仓库”按钮(富文本链接在 QMessageBox 内不可点,
        // 用 ActionRole 按钮打开默认浏览器)
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(QStringLiteral("关于 BPLC STA Monitor"));
        box.setTextFormat(Qt::RichText);
        box.setText(
            QStringLiteral("<h3>%1 %2</h3>"
                           "<p>BPLC/HRF 协议 STA 报文监控上位机"
                           "(串口捕获 + 离线回放)。</p>"
                           "<table>"
                           "<tr><td><b>作者</b></td><td>%3</td></tr>"
                           "<tr><td><b>版本</b></td><td>%2</td></tr>"
                           "<tr><td><b>仓库</b></td><td>%4</td></tr>"
                           "</table>")
                .arg(kModuleName, kAppVersion, kAuthorName, kRepoUrl));
        auto* btn_repo = box.addButton(QStringLiteral("打开仓库(&R)"),
                                       QMessageBox::ActionRole);
        QObject::connect(btn_repo, &QPushButton::clicked, [this]() {
            QDesktopServices::openUrl(QUrl(kRepoUrl));
        });
        box.addButton(QMessageBox::Close);
        box.exec();
    });
}

void MainWindow::wire_signals() {
    connect(m_btn_start,       &QToolButton::clicked, this, &MainWindow::on_start);
    connect(m_btn_stop,        &QToolButton::clicked, this, &MainWindow::on_stop);
    connect(m_btn_pause,       &QToolButton::toggled, this, &MainWindow::on_pause);
    connect(m_btn_clear,       &QToolButton::clicked, this, &MainWindow::on_clear);
    connect(m_btn_export,      &QToolButton::clicked, this, &MainWindow::on_export);
    connect(m_btn_apply_filter, &QToolButton::clicked, this, &MainWindow::on_apply_filter);
    connect(m_edt_filter,      &QLineEdit::returnPressed, this, &MainWindow::on_apply_filter);

    connect(m_table_packets, &QTableView::doubleClicked, this,
            [this](const QModelIndex& idx) {
                if (m_model) m_model->activate_row(idx.row());
            });
    connect(m_table_packets->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& cur, const QModelIndex&) {
                if (cur.isValid() && m_model) m_model->activate_row(cur.row());
            });

    connect(m_model, &PacketListModel::packet_activated, this, &MainWindow::on_row_activated);
    connect(m_tree_protocol, &ProtocolTree::range_selected,
            this,           &MainWindow::on_range_selected);
}

static ReaderConfig load_config_from_settings() {
    QSettings s("ZbMonitor", "BPLC_STA_Monitor");
    ReaderConfig c;
    int mode = s.value("mode", int(ReaderMode::SerialPort)).toInt();
    c.mode = ReaderMode(mode);
    c.serial_name = s.value("comName", "COM3").toString();
    c.baud_rate   = s.value("baud", 460800).toInt();
    c.file_path   = s.value("filePath").toString();
    c.has_time_tag = s.value("timeTag", false).toBool();
    return c;
}

static void save_config_to_settings(const ReaderConfig& c) {
    QSettings s("ZbMonitor", "BPLC_STA_Monitor");
    s.setValue("mode",      int(c.mode));
    s.setValue("comName",   c.serial_name);
    s.setValue("baud",      c.baud_rate);
    s.setValue("filePath",  c.file_path);
    s.setValue("timeTag",   c.has_time_tag);
}

void MainWindow::on_start() {
    if (!m_reader) return;
    ReaderConfig init = load_config_from_settings();
    CommConfigDialog dlg(this, init);
    if (dlg.exec() != QDialog::Accepted) return;

    ReaderConfig cfg = dlg.config();
    save_config_to_settings(cfg);
    m_reader->start(cfg);
    m_status_left->setText(QString("Running: %1")
        .arg(cfg.mode == ReaderMode::SerialPort
                ? QString("%1 @ %2,%3,%4,%5,%6")
                    .arg(cfg.serial_name)
                    .arg(cfg.baud_rate)
                    .arg(int(cfg.data_bits))
                    .arg(int(cfg.stop_bits))
                    .arg(int(cfg.parity))
                : cfg.file_path));
}

void MainWindow::on_stop() {
    if (m_reader) m_reader->stop();
    m_status_left->setText(QStringLiteral("Stopped"));
}

void MainWindow::on_pause() {
    m_paused = m_btn_pause->isChecked();
    m_btn_pause->setText(m_paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    if (m_paused) m_status_left->setText(QStringLiteral("Paused"));
}

void MainWindow::on_clear() {
    m_model->clear_all();
    m_index_counter = 0;
    m_last_epoch_ms = 0;
    {
        QMutexLocker lock(&m_pending_mutex);
        m_pending.clear();
    }
    if (m_dispatch) m_dispatch->statistics()->reset();
    m_tree_protocol->clear();
    m_hex_view->clear();
    m_status_left->setText(QStringLiteral("Cleared"));
}

void MainWindow::on_export() {
    QString f = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出"),
        "BPLC_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"),
        QStringLiteral("Text (*.txt);;所有 (*.*)"));
    if (f.isEmpty()) return;
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly)) return;
    QTextStream ts(&out);
    ts << "# BPLC Monitor export\n";
    ts << "# Format: <#>\t<time>\t<delta>\t<src>\t<dst>\t<protocol>\t<len>\t<info>\t<hex>\n";
    out.close();
    m_status_left->setText(QStringLiteral("Exported to %1").arg(f));
}

void MainWindow::on_settings() {
    ReaderConfig init = load_config_from_settings();
    CommConfigDialog dlg(this, init);
    if (dlg.exec() != QDialog::Accepted) return;
    save_config_to_settings(dlg.config());
    m_status_left->setText(QStringLiteral("配置已保存(Ctrl+E 开始捕获)"));
}

void MainWindow::on_apply_filter() {
    m_model->set_display_filter(m_edt_filter->text().trimmed());
}

PacketEntry MainWindow::make_entry(const BplcParser::Result& r, qint64 now) {
    PacketEntry e;
    e.index     = ++m_index_counter;
    e.epoch_ms  = now;
    e.delta_ms  = (m_last_epoch_ms == 0) ? 0 : (now - m_last_epoch_ms);
    m_last_epoch_ms = now;
    e.accepted  = r.accept;
    e.reason    = r.reject_reason;
    e.meta      = r.meta;
    e.mpdu      = r.mpdu;
    e.msdu_body = r.msdu_body;
    e.msdu      = r.msdu;    // MSDU/MAC 层字段树(SOF 重组完成时非空)
    e.beacon    = r.beacon;  // BEACON 载荷区字段树(BEACON 帧时非空)
    e.msdu_raw_base = r.msdu_raw_base;
    e.raw_bytes = r.payload_for_log;
    return e;
}

void MainWindow::enqueue_entry(PacketEntry&& e) {
    QMutexLocker lock(&m_pending_mutex);
    m_pending.append(std::move(e));
}

void MainWindow::on_parsed(const BplcParser::Result& r) {
    if (m_paused) return;
    enqueue_entry(make_entry(r, QDateTime::currentMSecsSinceEpoch()));
}

void MainWindow::on_flush_buffer() {
    QList<PacketEntry> snapshot;
    {
        QMutexLocker lock(&m_pending_mutex);
        if (m_pending.isEmpty()) return;
        snapshot.swap(m_pending);
    }
    QVector<PacketEntry> entries;
    entries.reserve(snapshot.size());
    for (auto& e : snapshot) entries.append(std::move(e));
    m_model->append_packets(entries);

    if (m_table_packets->model()->rowCount() > 0 && !m_paused) {
        m_table_packets->scrollToBottom();
    }
}

void MainWindow::on_row_activated(const PacketEntry& e) {
    m_tree_protocol->show_packet(e);
    m_hex_view->set_data(e.raw_bytes);
    m_hex_view->highlight_range(-1, 0);
}

void MainWindow::on_range_selected(int start, int len) {
    m_hex_view->highlight_range(start, len);
}

void MainWindow::on_check_finished(const QString& url) {
    if (url != kUpdateUrl) return;
    bool avail = QSimpleUpdater::getInstance()->getUpdateAvailable(url);
    m_status_left->setText(
        avail ? QStringLiteral("发现新版本,请按提示下载更新")
              : QStringLiteral("检查更新完成:暂无可更新版本(若网络不可达请检查连接)"));
}

void MainWindow::on_status_message(const QString& s) {
    m_status_left->setText(QStringLiteral("[状态] ") + s);
}

void MainWindow::on_error(const QString& e) {
    m_status_left->setText(QStringLiteral("[错误] ") + e);
}

void MainWindow::refresh_status_bar() {
    if (!m_dispatch) return;
    auto* stats = m_dispatch->statistics();
    auto snap = stats->snapshot();
    using Key = FrameStatistics::Key;
    auto v = [&](Key k) { return snap.value(int(k), 0); };
    int total = 0;
    for (auto it = snap.begin(); it != snap.end(); ++it) total += it.value();
    m_status_right->setText(QString(
        "Total: %1  |  BEACON: %2  SOF: %3  ACK: %4  COORD: %5  |  Drop: %6  |  MSDU: %7")
        .arg(total)
        .arg(v(Key::KEY_BEACON_HPLC) + v(Key::KEY_BEACON_HRF))
        .arg(v(Key::KEY_SOF_HPLC)    + v(Key::KEY_SOF_HRF))
        .arg(v(Key::KEY_ACK_HPLC)    + v(Key::KEY_ACK_HRF))
        .arg(v(Key::KEY_COORD_HPLC)  + v(Key::KEY_COORD_HRF))
        .arg(v(Key::KEY_DROPPED))
        .arg(v(Key::KEY_MSDU_COMPLETE)));
}
