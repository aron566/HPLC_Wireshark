/// @file mainwindow.cpp
/// @brief 主窗口实现
#include "mainwindow.h"

#include "packetlistmodel.h"
#include "hexview.h"
#include "protocoltree.h"
#include "protocolfactory.h"
#include "commconfigdialog.h"
#include "QSimpleUpdater.h"
#include "appconfig.h"
#include "playbackwriter.h"
#include "i18n.h"
#include "packetentry_serialize.h"
#include "fieldtools.h"
#include "topo_window.h"

#include <QtConcurrent>
#include <QDataStream>
#include <QFutureWatcher>
#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QLineEdit>
#include <QLabel>
#include <QTableView>
#include <QHeaderView>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QMenuBar>
#include <QSettings>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QMutexLocker>
#include <QDateTime>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QString>
#include <QMessageBox>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

#include <QIcon>

namespace {
// 当前版本与仓库信息(更新检查地址见 config.ini [general] update_url)
const QString kAppVersion = QStringLiteral("1.3.0");
const QString kModuleName = QStringLiteral("BPLC STA Monitor");
const QString kAuthorName = QStringLiteral("aron566");
const QString kAuthorEmail = QStringLiteral("aron566@163.com");
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_toolbar(nullptr), m_btn_start(nullptr), m_btn_stop(nullptr),
      m_btn_pause(nullptr), m_btn_clear(nullptr), m_btn_export(nullptr),
      m_btn_settings(nullptr), m_btn_topo(nullptr), m_edt_filter(nullptr), m_btn_apply_filter(nullptr),
      m_splitter_main(nullptr), m_table_packets(nullptr),
      m_splitter_bottom(nullptr), m_tree_protocol(nullptr),
      m_hex_view(nullptr), m_lbl_hex_title(nullptr),
      m_status_left(nullptr), m_status_mid(nullptr), m_status_right(nullptr),
      m_reader(nullptr), m_dispatch(nullptr), m_model(nullptr),
      m_flush_timer(nullptr), m_status_timer(nullptr),
      m_paused(false), m_exporting(false),
      m_follow_bottom(true),
      m_last_ntb(0),
      m_index_counter(0) {
    qRegisterMetaType<ParseResult>("ParseResult");
    qRegisterMetaType<BplcFrame>("BplcFrame");
    qRegisterMetaType<ReaderConfig>("ReaderConfig");
    qRegisterMetaType<ParseFilter>("ParseFilter");
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
    connect(m_reader, &SerialReader::progress_percent,
            this,      [this](int p) {
                m_status_left->setText(trl::L("回放进度: %1%").arg(p));
            });

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
    pal.setColor(QPalette::Highlight, QColor(0x3d, 0x6f, 0x9f));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    m_table_packets->setPalette(pal);
    // 注意:浅色/深色主题由 src/app/theme.cpp 全局应用(设置→外观 即时切换);
    // 不再在此设实例样式表,避免实例级 QSS 覆盖全局主题。

    setWindowTitle(QStringLiteral("BPLC STA Monitor v%1 — Wireshark style").arg(kAppVersion));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    resize(1280, 800);
    m_status_left->setText(trl::L("Ready — Ctrl+E 开始捕获,Ctrl+L 清空"));

    // 恢复上次保存的显示过滤器(config.ini [general] filter)
    const QString saved_filter = appcfg::filter();
    if (!saved_filter.isEmpty() && m_edt_filter && m_model) {
        m_edt_filter->setText(saved_filter);
        m_model->set_display_filter(saved_filter);
    }

    // 启动时按配置自动检查更新(静默:无新版本不弹窗,有新版本显示"立即更新"按钮)
    if (appcfg::auto_check()) {
        QTimer::singleShot(0, this, [this]() { check_for_updates(true); });
    }

    // 支持拖放文件导入(按扩展名自动判定 .bin 回放 / .txt/.hex 裸 hex)
    setAcceptDrops(true);
}

MainWindow::~MainWindow() = default;

void MainWindow::build_ui() {
    m_toolbar = addToolBar(QStringLiteral("Main"));
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    m_btn_start = new QToolButton(m_toolbar); m_btn_start->setText(trl::L("开始"));       m_toolbar->addWidget(m_btn_start);
    m_btn_stop  = new QToolButton(m_toolbar); m_btn_stop->setText(trl::L("停止"));        m_toolbar->addWidget(m_btn_stop);
    m_btn_pause = new QToolButton(m_toolbar); m_btn_pause->setText(trl::L("暂停"));
    m_btn_pause->setCheckable(true);                                                                   m_toolbar->addWidget(m_btn_pause);
    m_btn_clear = new QToolButton(m_toolbar); m_btn_clear->setText(trl::L("清空"));        m_toolbar->addWidget(m_btn_clear);
    m_toolbar->addSeparator();
    m_btn_export = new QToolButton(m_toolbar);  m_btn_export->setText(trl::L("导出"));     m_toolbar->addWidget(m_btn_export);
    m_btn_settings = new QToolButton(m_toolbar);m_btn_settings->setText(trl::L("设置"));    m_toolbar->addWidget(m_btn_settings);
    m_btn_topo = new QToolButton(m_toolbar);    m_btn_topo->setText(trl::L("拓扑"));        m_toolbar->addWidget(m_btn_topo);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(new QLabel(trl::L("  显示过滤器:"), m_toolbar));
    m_edt_filter = new QLineEdit(m_toolbar);
    m_edt_filter->setPlaceholderText(QStringLiteral("beacon | sof | hrf | plc | sta-3 | drop | 0xf0f1f2 ..."));
    m_edt_filter->setMinimumWidth(180);
    m_toolbar->addWidget(m_edt_filter);
    m_btn_apply_filter = new QToolButton(m_toolbar);
    m_btn_apply_filter->setText(trl::L("应用"));
    m_toolbar->addWidget(m_btn_apply_filter);

    m_splitter_main = new QSplitter(Qt::Vertical, this);

    m_model = new PacketListModel(this);
    m_table_packets = new QTableView(m_splitter_main);
    m_table_packets->setModel(m_model);
    m_table_packets->setAlternatingRowColors(true);
    m_table_packets->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table_packets->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table_packets->setSortingEnabled(true);
    m_table_packets->setShowGrid(false);
    m_table_packets->verticalHeader()->setVisible(false);
    m_table_packets->horizontalHeader()->setStretchLastSection(true);
    m_table_packets->setEditTriggers(QAbstractItemView::NoEditTriggers);

    QHeaderView* hh = m_table_packets->horizontalHeader();
    hh->resizeSection(PacketListModel::COL_INDEX,    60);
    hh->resizeSection(PacketListModel::COL_TIME,     120);
    hh->resizeSection(PacketListModel::COL_DELTA,    80);
    hh->resizeSection(PacketListModel::COL_ORIG_SRC, 70);
    hh->resizeSection(PacketListModel::COL_SOURCE,   100);
    hh->resizeSection(PacketListModel::COL_DEST,     100);
    hh->resizeSection(PacketListModel::COL_ORIG_DST, 70);
    hh->resizeSection(PacketListModel::COL_DIR,      44);
    hh->resizeSection(PacketListModel::COL_PROTOCOL, 70);
    hh->resizeSection(PacketListModel::COL_FRAME_TYPE, 90);
    hh->resizeSection(PacketListModel::COL_MSDU_TYPE, 140);
    hh->resizeSection(PacketListModel::COL_MSDU_SEQ, 90);
    hh->resizeSection(PacketListModel::COL_LENGTH,   60);

    m_splitter_main->addWidget(m_table_packets);

    m_splitter_bottom = new QSplitter(Qt::Horizontal, m_splitter_main);

    m_tree_protocol = new ProtocolTree(m_splitter_bottom);
    m_tree_protocol->setMinimumWidth(220);
    m_tree_protocol->set_variant(protocol_from_key(appcfg::protocol()));

    auto* hex_pane   = new QWidget(m_splitter_bottom);
    auto* hex_layout = new QVBoxLayout(hex_pane);
    hex_layout->setContentsMargins(0, 0, 0, 0);
    m_lbl_hex_title = new QLabel(trl::L("字节视图(十六进制,左偏移 + 中间 hex + 右侧 ASCII + RAW DATA):"),
                                 hex_pane);

    // 原始报文列:常显(无 checkbox/复制按钮),复制经右键菜单(0x 前缀可选)
    auto* hex_toolbar = new QWidget(hex_pane);
    auto* tl = new QHBoxLayout(hex_toolbar);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addStretch(1);

    m_split_hex = new QSplitter(Qt::Horizontal, hex_pane);
    m_hex_view  = new HexView(hex_pane);
    m_raw_view  = new QPlainTextEdit(m_split_hex);
    m_raw_view->setReadOnly(true);
    m_raw_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_raw_view->setFont(mono);
    m_split_hex->addWidget(m_hex_view);
    m_split_hex->addWidget(m_raw_view);
    m_split_hex->setSizes({640, 480});

    hex_layout->addWidget(m_lbl_hex_title);
    hex_layout->addWidget(hex_toolbar);
    hex_layout->addWidget(m_split_hex, 1);

    // 右键复制:加 0x 前缀 / 纯 hex 两种方式
    m_raw_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_raw_view, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(m_raw_view);
        QAction* a0 = menu.addAction(trl::L("复制(含 0x 前缀)"));
        QAction* a1 = menu.addAction(trl::L("复制(纯 hex)"));
        QAction* a = menu.exec(m_raw_view->mapToGlobal(pos));
        if (!a || m_raw_bytes.isEmpty()) return;
        QString out;
        if (a == a0) {
            // 含 0x 前缀的 hex:基于 toHex 生成 "xx xx" 后统一加前缀,
            // 不做逐字节 char/unsigned 转换、也无循环内字符串分配
            QByteArray hex = m_raw_bytes.toHex(' ');   // "3c 1c 02"
            hex.replace(' ', " 0x");                    // "3c 0x1c 0x02"
            out = QStringLiteral("0x") + QString::fromLatin1(hex);
        } else if (a == a1) {
            out = QString::fromLatin1(m_raw_bytes.toHex(' '));
        }
        QApplication::clipboard()->setText(out);
    });

    m_splitter_bottom->addWidget(m_tree_protocol);
    m_splitter_bottom->addWidget(hex_pane);
    m_splitter_main->addWidget(m_splitter_bottom);

    setCentralWidget(m_splitter_main);

    m_status_left = new QLabel(QStringLiteral("Ready"), this);
    m_status_mid = new QLabel(this);   // 状态栏中间:选中两行时间差(不干涉左侧串口状态)
    m_status_mid->setAlignment(Qt::AlignCenter);
    m_status_right = new QLabel(this);
    m_status_right->setAlignment(Qt::AlignRight);
    statusBar()->addWidget(m_status_left, 1);
    statusBar()->addWidget(m_status_mid, 1);
    statusBar()->addPermanentWidget(m_status_right, 2);

    auto* menu_capture = menuBar()->addMenu(trl::L("捕获(&C)"));
    auto* act_start = menu_capture->addAction(trl::L("开始"));
    act_start->setShortcut(QKeySequence("Ctrl+E"));
    connect(act_start, &QAction::triggered, this, &MainWindow::on_start);
    auto* act_stop = menu_capture->addAction(trl::L("停止"));
    act_stop->setShortcut(QKeySequence("Ctrl+."));
    connect(act_stop, &QAction::triggered, this, &MainWindow::on_stop);
    auto* act_pause = menu_capture->addAction(trl::L("暂停"));
    act_pause->setShortcut(QKeySequence("Ctrl+P"));
    act_pause->setCheckable(true);
    connect(act_pause, &QAction::toggled, this, &MainWindow::on_pause);
    menu_capture->addSeparator();
    auto* act_clear = menu_capture->addAction(trl::L("清空"));
    act_clear->setShortcut(QKeySequence("Ctrl+L"));
    connect(act_clear, &QAction::triggered, this, &MainWindow::on_clear);
    menu_capture->addSeparator();
    auto* act_export = menu_capture->addAction(trl::L("导出..."));
    connect(act_export, &QAction::triggered, this, &MainWindow::on_export);

    auto* menu_analyze = menuBar()->addMenu(trl::L("分析(&A)"));
    auto* act_filt = menu_analyze->addAction(trl::L("应用显示过滤器"));
    act_filt->setShortcut(QKeySequence("Ctrl+F"));
    connect(act_filt, &QAction::triggered, this, &MainWindow::on_apply_filter);

    // ---- 帮助菜单:检查更新 / 关于 ----
    auto* menu_help = menuBar()->addMenu(trl::L("帮助(&H)"));
    auto* act_check = menu_help->addAction(trl::L("检查更新(&U)..."));
    connect(act_check, &QAction::triggered, this, [this]() {
        check_for_updates(false);   // 手动检查:无新版本也弹窗告知
    });
    // 检查更新结束(无论结果)在状态栏留痕;失败(网络/清单)时以保守文案提示。
    // 注意:不用 Qt::UniqueConnection + lambda(Qt6 断言要求成员函数指针)。
    connect(QSimpleUpdater::getInstance(), &QSimpleUpdater::checkingFinished,
            this, &MainWindow::on_check_finished);
    auto* act_about = menu_help->addAction(trl::L("关于(&A)"));
    connect(act_about, &QAction::triggered, this, [this]() {
        // 关于:作者/作者邮箱/版本(如需打开仓库等外链请自行在浏览器访问)
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(trl::L("关于 BPLC STA Monitor"));
        box.setTextFormat(Qt::RichText);
        box.setText(
            QStringLiteral("<h3>%1 %2</h3>"
                           "<p>%3</p>"
                           "<table>"
                           "<tr><td><b>%4</b></td><td>%5</td></tr>"
                           "<tr><td><b>%6</b></td><td>%2</td></tr>"
                           "<tr><td><b>%7</b></td><td>%8</td></tr>"
                           "</table>")
                .arg(kModuleName, kAppVersion,
                     trl::L("BPLC/HRF 协议 STA 报文监控上位机(串口捕获 + 离线回放)。"),
                     trl::L("作者"), kAuthorName,
                     trl::L("版本"), trl::L("作者邮箱"), kAuthorEmail));
        box.addButton(QMessageBox::Close);
        box.exec();
    });

    // 发现新版本时在菜单栏右上角显示的"立即更新"按钮(默认隐藏;VSCode 风格蓝底)
    m_btn_update = new QToolButton(this);
    m_btn_update->setText(QStringLiteral("● ") + trl::L("立即更新"));
    m_btn_update->setCursor(Qt::PointingHandCursor);
    m_btn_update->setToolTip(trl::L("发现新版本,点击下载安装"));
    m_btn_update->setStyleSheet(QStringLiteral(
        "QToolButton { color: #ffffff; background-color: #0e639c;"
        " border: none; border-radius: 3px; padding: 3px 10px; font-weight: bold; }"
        "QToolButton:hover { background-color: #1177bb; }"));
    m_btn_update->setVisible(false);
    menuBar()->setCornerWidget(m_btn_update, Qt::TopRightCorner);
    connect(m_btn_update, &QToolButton::clicked, this, [this]() {
        const QString u = appcfg::update_url();
        const QString dl = QSimpleUpdater::getInstance()->getDownloadUrl(u);
        if (!dl.isEmpty())
            QDesktopServices::openUrl(QUrl(dl));
    });
}

void MainWindow::wire_signals() {
    connect(m_btn_start,       &QToolButton::clicked, this, &MainWindow::on_start);
    connect(m_btn_stop,        &QToolButton::clicked, this, &MainWindow::on_stop);
    connect(m_btn_pause,       &QToolButton::toggled, this, &MainWindow::on_pause);
    connect(m_btn_clear,       &QToolButton::clicked, this, &MainWindow::on_clear);
    connect(m_btn_export,      &QToolButton::clicked, this, &MainWindow::on_export);
    connect(m_btn_topo,        &QToolButton::clicked, this, &MainWindow::open_topo_window);
    connect(m_btn_apply_filter, &QToolButton::clicked, this, &MainWindow::on_apply_filter);
    connect(m_edt_filter,      &QLineEdit::returnPressed, this, &MainWindow::on_apply_filter);

    connect(m_table_packets, &QTableView::doubleClicked, this,
            [this](const QModelIndex& idx) {
                if (!m_model) return;
                m_model->activate_row(idx.row());  // 照常展示报文详情
                // 双击 → 强制历史追溯(冻结在该帧);即使是最新帧也不跟随实时
                PacketEntry e;
                if (m_model->entry_at(idx.row(), e))
                    enter_topo_history(e.index, e.epoch_ms);
            });
    connect(m_table_packets->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& cur, const QModelIndex&) {
                if (cur.isValid() && m_model) m_model->activate_row(cur.row());
            });
    // 选中两行 → 计算跨行时间差显示在状态栏中间(NTB 优先,与本地差>3s 降级本地时间)
    connect(m_table_packets->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection&, const QItemSelection&) {
                update_selection_delta();
            });

    connect(m_model, &PacketListModel::packet_activated, this, &MainWindow::on_row_activated);
    connect(m_tree_protocol, &ProtocolTree::ranges_selected,
            this,           &MainWindow::on_ranges_selected);

    // 滚轮/拖拽滚动离开底部 → 暂停自动跟随;手动滚回底部 → 恢复跟随最新帧
    connect(m_table_packets->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
                m_follow_bottom =
                    (value >= m_table_packets->verticalScrollBar()->maximum());
                // 滚动预取:按视口首/尾可见行预加载其所在盘块及相邻块,保证丝滑
                if (m_model && m_model->rowCount() > 0) {
                    const int top = m_table_packets->rowAt(0);
                    const int bottom =
                        m_table_packets->rowAt(m_table_packets->viewport()->height() - 1);
                    if (top >= 0) m_model->ensure_loaded(top);
                    if (bottom >= 0 && bottom != top) m_model->ensure_loaded(bottom);
                }
            });
}

static ReaderConfig load_config_from_settings() {
    ReaderConfig c;                       // 来源:config.ini [reader]
    int mode = appcfg::reader_mode();
    c.mode = ReaderMode(mode);
    c.serial_name = appcfg::reader_com();
    c.baud_rate   = appcfg::reader_baud();
    c.file_path   = appcfg::reader_file();
    c.has_time_tag = appcfg::reader_time_tag();
    return c;
}

static void save_config_to_settings(const ReaderConfig& c) {
    appcfg::set_reader(int(c.mode), c.serial_name, c.baud_rate,
                       c.file_path, c.has_time_tag);
}

void MainWindow::on_start() {
    if (!m_reader) return;
    ReaderConfig init = load_config_from_settings();
    const QString old_proto = appcfg::protocol();
    CommConfigDialog dlg(this, init);
    if (dlg.exec() != QDialog::Accepted) return;

    ReaderConfig cfg = dlg.config();
    save_config_to_settings(cfg);

    // 协议下拉框变更确定后:提示「重启后生效,若点击开始则立即生效」;
    // 点「开始」→ 停止 + 清空 + 重建 dispatcher(用新协议立即生效)。
    if (appcfg::protocol() != old_proto && confirm_protocol_rebuild())
        rebuild_dispatcher();

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
    m_btn_pause->setText(m_paused ? trl::L("继续") : trl::L("暂停"));
    if (m_paused) m_status_left->setText(QStringLiteral("Paused"));
}

void MainWindow::on_clear() {
    m_model->clear_all();
    m_index_counter = 0;
    m_last_ntb = 0;
    {
        QMutexLocker lock(&m_pending_mutex);
        m_pending.clear();
    }
    if (m_dispatch) m_dispatch->statistics()->reset();
    // 拓扑状态随报文清空(此前漏清会导致旧拓扑残留);回放日志/快照/回放水位同步清零
    m_topo_states.clear();
    m_topo_log.clear();
    m_topo_hist_states.clear();
    m_topo_hist_replayed = -1;
    m_topo_hist_active = false;
    if (m_topo_window) m_topo_window->show_live();
    m_tree_protocol->clear();
    m_hex_view->clear();
    m_status_left->setText(QStringLiteral("Cleared"));
}

namespace {
/// @brief 从盘块文件逐条反序列化并交给 writer(独立文件句柄,线程安全)
template <typename Writer>
void export_block_file(const QString& path, Writer& w) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QDataStream s(&f);
    while (!f.atEnd()) {
        quint32 len = 0;
        s >> len;
        if (s.status() != QDataStream::Ok || len == 0) break;
        QByteArray payload(int(len), Qt::Uninitialized);
        if (s.readRawData(payload.data(), int(len)) != int(len)) break;
        PacketEntry e;
        if (pser::deserialize_entry(payload, e)) w.add(e);
    }
    f.close();
}

/// 导出格式
enum class ExportFormat { Bin, Text, Csv };

// TEI→MAC 映射表(与 PacketListModel 内部一致)
using TeiMacMap = QHash<quint32, QHash<quint16, quint64>>;

static quint64 csv_lookup_mac(const TeiMacMap& macs, quint32 nid, quint16 tei) {
    const auto it = macs.constFind(nid);
    if (it == macs.constEnd()) return 0;
    const auto jt = it->constFind(tei);
    return (jt == it->constEnd()) ? 0 : jt.value();
}

/// CSV 单元格转义(含逗号/引号/换行时双引号包裹,内部引号翻倍)
static QString csv_escape(const QString& s) {
    if (!s.contains(',') && !s.contains('"') && !s.contains('\n') && !s.contains('\r'))
        return s;
    QString t = s;
    t.replace('"', QStringLiteral("\"\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

/// 复现 PacketListModel::data() 的列渲染,生成一行 CSV 单元格(与表格列一致)
static QStringList csv_row(const PacketEntry& e, const TeiMacMap& macs) {
    QStringList c;
    c << QString::number(e.index);
    // Time
    c << (e.meta.frame_time.isValid()
              ? e.meta.frame_time.toString(QStringLiteral("HH:mm:ss.zzz"))
              : QStringLiteral("%1 s").arg(e.epoch_ms / 1000.0, 0, 'f', 6));
    // Delta
    c << QStringLiteral("%1 s").arg(e.delta_us / 1e6, 0, 'f', 6);
    // Orig Src
    {
        QString s;
        if (e.accepted && e.msdu.present && !e.msdu.simple_head && e.msdu.msdu_src_tei > 0) {
            const int tei = e.msdu.msdu_src_tei;
            s = (tei == 1) ? QStringLiteral("CCO") : QStringLiteral("STA-%1").arg(tei);
            quint64 mac = e.msdu.msdu_src_mac;
            if (!mac) mac = csv_lookup_mac(macs, e.mpdu.net_id, quint16(tei));
            if (mac) s += QStringLiteral(" [%1]").arg(mac_str(mac));
        }
        c << s;
    }
    // Source
    {
        QString s;
        if (!e.accepted) {
            s = QStringLiteral("DROP");
        } else {
            const quint16 tei = e.mpdu.src_tei;
            if (e.mpdu.frame_type == 3 && tei == 0) {
                s = QStringLiteral("CCO");
                const quint64 mac = csv_lookup_mac(macs, e.mpdu.net_id, 1);
                if (mac) s += QStringLiteral(" [%1]").arg(mac_str(mac));
            } else if (tei != 0) {
                s = (tei == 1) ? QStringLiteral("CCO") : QStringLiteral("STA-%1").arg(tei);
                const quint64 mac = csv_lookup_mac(macs, e.mpdu.net_id, tei);
                if (mac) s += QStringLiteral(" [%1]").arg(mac_str(mac));
            } else if (e.msdu.sta_mac) {
                s = QStringLiteral("STA-X [%1]").arg(mac_str(e.msdu.sta_mac));
            } else {
                s = e.meta.is_rf ? QStringLiteral("RF") : QStringLiteral("PLC");
            }
        }
        c << s;
    }
    // Destination
    {
        QString s;
        if (!e.accepted) {
            s = e.reason;
        } else if (e.mpdu.dst_tei == 0xFFF) {
            s = QStringLiteral("BROADCAST");
        } else {
            const quint16 tei = e.mpdu.dst_tei;
            if (tei != 0) {
                s = (tei == 1) ? QStringLiteral("CCO") : QStringLiteral("STA-%1").arg(tei);
                const quint64 mac = csv_lookup_mac(macs, e.mpdu.net_id, tei);
                if (mac) s += QStringLiteral(" [%1]").arg(mac_str(mac));
            } else {
                s = QStringLiteral("*");
            }
        }
        c << s;
    }
    // Orig Dst
    {
        QString s;
        if (e.accepted && e.msdu.present && !e.msdu.simple_head && e.msdu.msdu_dst_tei > 0) {
            const int tei = e.msdu.msdu_dst_tei;
            s = (tei == 0xFFF) ? QStringLiteral("BCAST")
              : (tei == 1) ? QStringLiteral("CCO") : QStringLiteral("STA-%1").arg(tei);
            if (tei == 0xFFF) {
                if (e.msdu.msdu_dst_mac) s += QStringLiteral(" [%1]").arg(mac_str(e.msdu.msdu_dst_mac));
            } else {
                quint64 mac = e.msdu.msdu_dst_mac;
                if (!mac) mac = csv_lookup_mac(macs, e.mpdu.net_id, quint16(tei));
                if (mac) s += QStringLiteral(" [%1]").arg(mac_str(mac));
            }
        }
        c << s;
    }
    // Dir
    {
        QString dir;
        if (!e.accepted || !e.msdu.present || e.msdu.simple_head) {
            dir = QStringLiteral("*");
        } else {
            const bool relay = (e.mpdu.src_tei != 0)
                            && (e.msdu.msdu_src_tei > 0)
                            && (quint16(e.msdu.msdu_src_tei) != e.mpdu.src_tei);
            if (e.msdu.msdu_dst_tei == 0xFFF)
                dir = (e.msdu.msdu_send_type == 1 || e.msdu.msdu_send_type == 3)
                          ? QStringLiteral("\u2192") : QStringLiteral("*");
            else if (e.msdu.msdu_dst_tei == 1)
                dir = QStringLiteral("\u2191");
            else if (e.msdu.msdu_src_tei == 1)
                dir = QStringLiteral("\u2193");
            else
                dir = QStringLiteral("*");
            if (relay && dir != QStringLiteral("*")) dir += QStringLiteral("R");
        }
        c << dir;
    }
    // Protocol
    c << (!e.accepted ? QStringLiteral("ERR")
                      : e.meta.is_rf ? QStringLiteral("RF") : QStringLiteral("HPLC"));
    // Frame Type
    c << (!e.accepted ? QStringLiteral("-") : e.mpdu.frame_type_name());
    // MSDU Type
    c << ((e.accepted && e.msdu.present) ? e.msdu.summary : QString());
    // MSDU Seq
    c << ((e.accepted && e.msdu.present) ? QString::number(e.msdu.msdu_seq) : QString());
    // Length
    c << QString::number(e.raw_bytes.size());
    // Info
    {
        QString s;
        if (!e.accepted) {
            s = e.reason;
        } else {
            const QString nid = QString::number(e.mpdu.net_id, 16).toUpper().rightJustified(6, QChar('0'));
            if (e.mpdu.frame_type == 0)
                s = QStringLiteral("NetID=0x%1 ts=%2").arg(nid).arg(e.meta.timestamp);
            else if (e.mpdu.frame_type == 1)
                s = QStringLiteral("NetID=0x%1 src=%2 dst=%3 TMI=%4 PBNum=%5%6")
                        .arg(nid).arg(e.mpdu.src_tei).arg(e.mpdu.dst_tei)
                        .arg(e.mpdu.tmi).arg(e.mpdu.pb_num)
                        .arg(e.msdu_body.isEmpty() ? QString()
                                                   : QStringLiteral(" MSDU[%1B]").arg(e.msdu_body.size()));
            else
                s = QStringLiteral("NetID=0x%1 src=%2 dst=%3")
                        .arg(nid).arg(e.mpdu.src_tei).arg(e.mpdu.dst_tei);
        }
        c << s;
    }
    return c;
}

/// @brief 工作线程导出:遍历快照盘块 + 热区,流式写文件(不阻塞 GUI)
void run_export(const PacketListModel::ExportSnapshot& snap,
                const QString& path, ExportFormat fmt) {
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return;
    if (fmt == ExportFormat::Text) {
        playback::RawHexWriter w(&out);
        for (const QString& bp : snap.block_paths) export_block_file(bp, w);
        for (const PacketEntry& e : snap.hot) w.add(e);
    } else if (fmt == ExportFormat::Bin) {
        playback::PlaybackBinWriter w(&out);
        for (const QString& bp : snap.block_paths) export_block_file(bp, w);
        for (const PacketEntry& e : snap.hot) w.add(e);
    } else {   // ExportFormat::Csv
        QTextStream ts(&out);
        // 本地编码存储(Windows 中文系统=GBK/ANSI 代码页),Excel 直接打开不乱码
        ts.setEncoding(QStringConverter::System);
        ts << QStringLiteral("#,Time,Delta,Orig Src,Source,Destination,Orig Dst,Dir,"
                             "Protocol,Frame Type,MSDU Type,MSDU Seq,Length,Info\n");
        const auto emit_row = [&](const PacketEntry& e) {
            const QStringList cells = csv_row(e, snap.tei_mac);
            for (int i = 0; i < cells.size(); ++i) {
                if (i) ts << ',';
                ts << csv_escape(cells[i]);
            }
            ts << '\n';
        };
        for (const QString& bp : snap.block_paths) {
            QFile f(bp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            QDataStream s(&f);
            while (!f.atEnd()) {
                quint32 len = 0;
                s >> len;
                if (s.status() != QDataStream::Ok || len == 0) break;
                QByteArray payload(int(len), Qt::Uninitialized);
                if (s.readRawData(payload.data(), int(len)) != int(len)) break;
                PacketEntry e;
                if (pser::deserialize_entry(payload, e)) emit_row(e);
            }
            f.close();
        }
        for (const PacketEntry& e : snap.hot) emit_row(e);
    }
    out.close();
}
}  // namespace

void MainWindow::on_export() {
    if (m_model->total_count() == 0) {
        m_status_left->setText(trl::L("无可导出的帧"));
        return;
    }
    // 三种导出格式:①回放 bin(0x3C 封装帧流+BCD 时间标签)
    // ②裸数据 hex 文本(每行一帧,无 0x3C/0x3E/0x3D 封装):
    //   [ts 4B LE][phr_mcs][option][channel][isRF][MPDU];回放(RawHex)
    //   按 ts 还原捕获时刻,缺失时回退本地时间
    // ③CSV 表格(每行内容,便于 Excel 排查;仅导出,不支持导入)
    QString selected;
    const QString filter = trl::L("回放文件 (*.bin)") + QStringLiteral(";;") +
                           trl::L("裸 hex 文本 (*.txt)") + QStringLiteral(";;") +
                           trl::L("CSV 表格 (*.csv)");
    QString f = QFileDialog::getSaveFileName(
        this, trl::L("导出为文件"),
        "BPLC_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") +
            (selected.contains(QStringLiteral(".txt")) ? ".txt"
             : selected.contains(QStringLiteral(".csv")) ? ".csv" : ".bin"),
        filter, &selected);
    if (f.isEmpty()) return;
    ExportFormat fmt = ExportFormat::Bin;
    if (selected.contains(QStringLiteral(".txt"))) {
        fmt = ExportFormat::Text;
        if (!f.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
            f += QStringLiteral(".txt");
    } else if (selected.contains(QStringLiteral(".csv"))) {
        fmt = ExportFormat::Csv;
        if (!f.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
            f += QStringLiteral(".csv");
    } else if (!f.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)) {
        f += QStringLiteral(".bin");
    }
    // 导出快照 + 工作线程流式导出(不阻塞界面)
    const PacketListModel::ExportSnapshot snap = m_model->make_export_snapshot();
    const qint64 total = m_model->total_count();
    m_exporting = true;   // 暂停 flush 进模型,保证快照一致 + 线程安全
    m_status_left->setText(trl::L("正在导出 %1 帧...").arg(total));

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, f, total]() {
        m_exporting = false;
        watcher->deleteLater();
        QFile out(f);
        if (!out.exists() || out.size() == 0) {
            m_status_left->setText(trl::L("没有可写入的帧数据"));
        } else {
            m_status_left->setText(trl::L("已导出 %1 帧 → %2").arg(total).arg(f));
        }
    });
    watcher->setFuture(QtConcurrent::run([snap, f, fmt]() {
        run_export(snap, f, fmt);
    }));
}

void MainWindow::on_settings() {
    ReaderConfig init = load_config_from_settings();
    const QString old_proto = appcfg::protocol();
    CommConfigDialog dlg(this, init);
    if (dlg.exec() != QDialog::Accepted) return;
    save_config_to_settings(dlg.config());
    if (appcfg::protocol() != old_proto && confirm_protocol_rebuild())
        rebuild_dispatcher();
    m_status_left->setText(trl::L("配置已保存(Ctrl+E 开始捕获)"));
}

/// @brief 协议下拉框变更后的提示;返回 true=立即生效(点「开始」),false=重启后生效
bool MainWindow::confirm_protocol_rebuild() {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(trl::L("协议已更改"));
    box.setText(trl::L("协议已更改,重启后生效。若点击「开始」则立即生效。"));
    QPushButton* btn_apply = box.addButton(trl::L("开始"), QMessageBox::AcceptRole);
    box.addButton(trl::L("重启后生效"), QMessageBox::RejectRole);
    box.exec();
    return box.clickedButton() == btn_apply;
}

/// @brief 停止旧解析线程并重建 dispatcher(拖放导入/协议切换共用):
///        丢弃积压帧、重置 MSDU 重组与统计,按 config.ini 协议实例化新解析器
void MainWindow::reset_dispatcher() {
    // 回放忙碌时不能同步 delete(析构里 wait 会卡 GUI 直至崩溃):
    // shutdown 立即断开 worker 信号 + 退出解析线程(停止处理积压帧、
    // 停止向 GUI 投递 parsed),再断外部连接并 deleteLater 异步销毁。
    if (m_dispatch) {
        m_dispatch->shutdown();
        m_dispatch->disconnect_source(m_reader);
        m_dispatch->disconnect(this);
        m_dispatch->deleteLater();
        m_dispatch = nullptr;
    }
    m_dispatch = new FrameDispatcher(this);   // 按 config.ini 新协议实例化解析器
    m_dispatch->connect_source(m_reader);
    connect(m_dispatch, &FrameDispatcher::parsed,
            this,       &MainWindow::on_parsed,
            Qt::QueuedConnection);
}

/// @brief 协议切换立即生效:停止当前采集/回放 → 清空 → 用新协议重建解析器
void MainWindow::rebuild_dispatcher() {
    if (m_reader) m_reader->stop();
    on_clear();                       // 清空列表/协议树/hex/统计(旧 dispatcher 仍在)
    reset_dispatcher();
    m_tree_protocol->set_variant(protocol_from_key(appcfg::protocol()));  // 字段树切协议
    m_status_left->setText(trl::L("协议已立即生效(Ctrl+E 开始捕获)"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1 && urls.first().isLocalFile()) {
            const QString ext = QFileInfo(urls.first().toLocalFile()).suffix().toLower();
            if (ext == QStringLiteral("bin") || ext == QStringLiteral("txt")
                || ext == QStringLiteral("hex")) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) {
        event->ignore();
        return;
    }
    start_file_import(urls.first().toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::start_file_import(const QString& path) {
    // 仅允许在停止状态导入;采集/回放运行中拒绝(避免打断正在进行的捕获)
    if (m_reader && m_reader->is_running()) {
        QMessageBox::warning(this, trl::L("导入"),
                             trl::L("正在捕获/回放中,请先点击停止后再拖入文件"));
        return;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    ReaderConfig cfg = load_config_from_settings();
    if (ext == QStringLiteral("bin")) {
        cfg.mode = ReaderMode::FilePlayback;
    } else if (ext == QStringLiteral("txt") || ext == QStringLiteral("hex")) {
        cfg.mode = ReaderMode::RawHex;
    } else {
        QMessageBox::warning(this, trl::L("导入"),
                             trl::L("不支持的文件类型(支持 .bin / .txt / .hex): %1").arg(path));
        return;
    }
    cfg.file_path = path;
    save_config_to_settings(cfg);

    if (!m_reader) return;
    m_reader->stop();      // 停止旧采集/回放(abort 打断同步回放循环)
    on_clear();            // 清空列表/协议树/hex/统计
    reset_dispatcher();    // 重建解析器:丢弃旧回放积压帧 + 重置 MSDU 重组/统计
    m_reader->start(cfg);
    m_status_left->setText(QStringLiteral("Running: %1").arg(path));
}

void MainWindow::on_apply_filter() {
    const QString expr = m_edt_filter->text().trimmed();
    m_model->set_display_filter(expr);
    appcfg::set_filter(expr);              // 记忆到 config.ini,下次启动恢复
}

PacketEntry MainWindow::make_entry(const ParseResult& r, qint64 now) {
    PacketEntry e;
    e.index     = ++m_index_counter;
    // 带时间标签(回放导出的 bin)时用帧内绝对时刻,保证 Time/Delta/再导出
    // 均以原始捕获时间为基准;否则退化为本地接收时刻
    qint64 t = (r.meta.frame_time.isValid())
                   ? r.meta.frame_time.toMSecsSinceEpoch() : now;
    e.epoch_ms  = t;
    e.accepted  = r.accept;     // 先落 accepted,Delta/last 追踪依赖它
    e.reason    = r.reject_reason;
    // Delta:统一用帧内 NTB 差(tick × 40ns),实时串口与回放 bin 一致。
    // 本地接收时间(0x3C 打点 arrival_us)只作 serialreader 断段(seg_start)
    // 判断参考,不参与 Delta 计算。
    const quint32 ntb = r.meta.timestamp;   // NTB tick(实时/回放统一)
    if (r.meta.seg_start) {
        e.delta_us = 0;                     // 跨段断点:不计算与上一帧 delta
    } else {
        const qint64 dn = (qint32)(ntb - m_last_ntb);   // u32 回绕安全
        if (dn > 0 && dn <= playback::kMaxNtbGapTicks)
            e.delta_us = playback::ntb_to_us(quint32(dn));  // tick→µs
        else
            e.delta_us = 0;                 // NTB 异常(回绕/跳变):不计算
    }
    m_last_ntb = ntb;
    e.meta      = r.meta;
    e.mpdu      = r.mpdu;
    e.msdu_body = r.msdu_body;
    e.raw_wire  = r.raw_wire;
    e.msdu      = r.msdu;    // MSDU/MAC 层字段树(SOF 重组完成时非空)
    e.beacon    = r.beacon;  // BEACON 载荷区字段树(BEACON 帧时非空)
    e.msdu_raw_base = r.msdu_raw_base;
    e.raw_bytes = r.payload_for_log;
    // 拓扑:关键管理消息(关联确认/代理变更/发现列表/离网)喂给拓扑状态;
    // 同时记入回放日志:事件直接填充在 entry 内(nid/epoch_ms/is_rf),
    // 供 TOPO 历史回放调试(点击帧 → 按帧序号重放到该帧)
    if (e.msdu.topo_event.kind != TopoEventKind::Other) {
        TopoEvent& te = e.msdu.topo_event;
        te.nid = e.mpdu.net_id;
        te.epoch_ms = e.epoch_ms;
        te.is_rf = e.meta.is_rf;   // 接入方式(载波/RF)
        te.frame_index = e.index;  // 来源帧序号(路由表序号列/双击追溯)
        m_topo_states[e.mpdu.net_id].apply(te);
        m_topo_log.append({e.index, te});
        // 增量回放依赖日志严格按帧序追加;若乱序到达则重置回放水位,下次回放全量重建
        if (e.index <= m_topo_hist_replayed) m_topo_hist_replayed = -1;
        // 拓扑窗口节流刷新:仅置脏标志,由 TopoWindow 定时器批量刷新(防高频卡顿)
        // (历史回放模式下 mark_dirty 会被 TopoWindow 忽略,保持冻结)
        if (m_topo_window)
            m_topo_window->mark_dirty();
    }
    e.search_text = make_search_text(e);   // 缓存可搜索全文,过滤匹配复用
    return e;
}

void MainWindow::enqueue_entry(PacketEntry&& e) {
    QMutexLocker lock(&m_pending_mutex);
    m_pending.append(std::move(e));
    m_pending_count.fetch_add(1, std::memory_order_relaxed);
}

void MainWindow::on_parsed(const ParseResult& r) {
    if (m_paused) return;
    enqueue_entry(make_entry(r, QDateTime::currentMSecsSinceEpoch()));
    // 高速灌帧(错协议解析跳过重活、极快)时,parsed 事件会把 100ms 的
    // flush timer 挤出事件队列,导致 pending 堆积到数万帧、一次性 flush 卡顿。
    // 达到阈值即同步 flush,把巨批摊平为小批,与国网正常解析的节奏一致。
    if (m_pending_count.load(std::memory_order_relaxed) >= 1000)
        on_flush_buffer();
}

void MainWindow::on_flush_buffer() {
    if (m_exporting) return;   // 导出期间暂停 append 进模型,保证快照一致且线程安全
    QList<PacketEntry> snapshot;
    {
        QMutexLocker lock(&m_pending_mutex);
        if (m_pending.isEmpty()) return;
        snapshot.swap(m_pending);
        m_pending_count.store(0, std::memory_order_relaxed);
    }
    QVector<PacketEntry> entries;
    entries.reserve(snapshot.size());
    for (auto& e : snapshot) entries.append(std::move(e));
    m_model->append_packets(entries);

    if (m_table_packets->model()->rowCount() > 0 && !m_paused && m_follow_bottom) {
        m_table_packets->scrollToBottom();
    }
}

namespace {
// 原始帧 hex 文本:每行 "偏移:  xx xx …"(16 字节/行,无 ASCII 列)
QString raw_frame_text(const QByteArray& w) {
    if (w.isEmpty()) return QString();
    QString s;
    for (int i = 0; i < w.size(); ++i) {
        if (i % 16 == 0)
            s += QStringLiteral("%1  ").arg(i, 4, 16, QChar('0'));
        s += QStringLiteral("%1 ")
                 .arg(quint8(w[i]), 2, 16, QChar('0'));
        if ((i % 16) == 15 || i == w.size() - 1)
            s += QLatin1Char('\n');
    }
    return s;
}
}  // namespace

void MainWindow::on_row_activated(const PacketEntry& e) {
    m_tree_protocol->show_packet(e);
    m_hex_view->set_data(e.raw_bytes);
    m_hex_view->highlight_range(-1, 0);
    // 原始串口帧(0x3C...0x3E)展示,便于复制调试(无 ASCII,带偏移索引)
    if (m_raw_view) {
        m_raw_bytes = e.raw_wire;
        m_raw_view->setPlainText(raw_frame_text(e.raw_wire));
    }
    // TOPO 历史回放调试:路由状态冻结,只更新到当前点击的帧
    update_topo_history(e.index, e.epoch_ms);
}

/// @brief 双击帧 → 强制进入历史追溯(冻结在该帧,不跟随实时)
/// @details 与 update_topo_history(单击启发式)不同:双击任何帧都进历史模式,
///          即使是最新帧也不 live 跟随;回到实时用"回到实时"按钮或单击最新帧。
void MainWindow::enter_topo_history(qint64 frame_index, qint64 frame_ms) {
    m_topo_hist_frame = frame_index;
    m_topo_hist_ms = frame_ms;
    m_topo_hist_active = true;  // 强制历史模式,不再 live
    if (!m_topo_window || !m_topo_window->isVisible())
        return;  // TOPO 窗口未打开:只记模式,打开时再重放同步
    replay_topo_history();
}

/// @brief 帧点击 → TOPO 回放/实时切换(点到最新帧 = 回到实时)
void MainWindow::update_topo_history(qint64 frame_index, qint64 frame_ms) {
    m_topo_hist_frame = frame_index;
    m_topo_hist_ms = frame_ms;
    m_topo_hist_active = (m_model && frame_index < m_model->total_count());
    if (!m_topo_window || !m_topo_window->isVisible())
        return;  // TOPO 窗口未打开:只记模式,打开时再重放同步
    replay_topo_history();
}

/// @brief 按 m_topo_hist_frame 重放拓扑事件日志,生成冻结快照并显示
/// @details 增量回放:m_topo_hist_replayed 记录快照已覆盖到的帧;
///          目标帧更大时只 apply 差量区间(二分定位起点),目标更小时才全量重建。
///          日志按帧序追加,乱序到达时 make_entry 已重置水位,此处兜底全量重建。
void MainWindow::replay_topo_history() {
    // 点到更早的帧,或水位失效(初始 -1 / 日志乱序被重置):快照从零重建。
    // 注意:水位失效时必须清快照,否则从头重放会 double-apply(事件表翻倍)。
    if (m_topo_hist_replayed < 0 || m_topo_hist_frame < m_topo_hist_replayed) {
        m_topo_hist_states.clear();
        m_topo_hist_replayed = -1;
    }
    // 二分定位水位之后的第一条日志
    int lo = 0, hi = m_topo_log.size();
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (m_topo_log[mid].frame_index <= m_topo_hist_replayed) lo = mid + 1;
        else hi = mid;
    }
    for (int i = lo; i < m_topo_log.size(); ++i) {
        const TopoLogItem& it = m_topo_log[i];
        if (it.frame_index > m_topo_hist_frame)
            break;  // 日志按帧序追加,后续事件不属于本次回放
        m_topo_hist_states[it.event.nid].apply(it.event);
    }
    m_topo_hist_replayed = m_topo_hist_frame;
    m_topo_window->show_history(&m_topo_hist_states, m_topo_hist_active,
                                m_topo_hist_frame, m_topo_hist_ms);
}

void MainWindow::on_topo_request_live() {
    m_topo_hist_active = false;
    if (m_topo_window)
        m_topo_window->show_live();
}

/// @brief TOPO 路由变更表双击某行 → 进入历史追溯并冻结在该行对应的帧
/// @details 同时主帧列表联动定位到该帧(选中+居中),方便查看上下报文,无需手动翻找
void MainWindow::on_topo_request_history(qint64 frame_index, qint64 frame_ms) {
    enter_topo_history(frame_index, frame_ms);
    jump_packet_to_frame(frame_index);
}

/// @brief 主帧列表定位到指定帧序号
/// @details 表格为插入序(无排序),可见行 = 帧序号 - 1(序号 1-based);
///          若有显示过滤会隐藏目标帧,先同步清除过滤(清空是同步的)再定位;
///          定位后选中该行并滚动居中,同时激活详情面板
void MainWindow::jump_packet_to_frame(qint64 frame_index) {
    if (!m_model || !m_table_packets || frame_index < 1)
        return;
    if (frame_index > m_model->total_count())
        return;
    // 有过滤时目标帧可能不可见:先清除(同步),保证能看到上下报文上下文
    if (m_edt_filter && !m_edt_filter->text().trimmed().isEmpty()) {
        m_edt_filter->clear();
        m_model->set_display_filter(QString());
    }
    const int row = int(frame_index - 1);
    if (row < 0 || row >= m_model->rowCount())
        return;
    const QModelIndex idx = m_model->index(row, 0);
    if (!idx.isValid())
        return;
    m_table_packets->selectionModel()->select(
        idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_table_packets->setCurrentIndex(idx);
    m_table_packets->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    m_model->activate_row(row);
}

void MainWindow::on_ranges_selected(const QList<QPair<int, int>>& ranges,
                                    const QByteArray& copy_bytes) {
    m_hex_view->highlight_ranges(ranges);
    m_hex_view->set_copy_bytes(copy_bytes);
}

void MainWindow::check_for_updates(bool silent) {
    auto* su = QSimpleUpdater::getInstance();
    const QString up_url = appcfg::update_url();   // 更新地址来自 config.ini
    su->setModuleVersion(up_url, kAppVersion);
    su->setModuleName(up_url, kModuleName);
    su->setNotifyOnUpdate(up_url, true);      // 有新版本 → 弹窗 + 显示"立即更新"按钮
    su->setNotifyOnFinish(up_url, !silent);   // 静默(启动):无新版本不弹窗
    if (!silent)
        m_status_left->setText(trl::L("正在检查更新…"));
    su->checkForUpdates(up_url);
}

void MainWindow::on_check_finished(const QString& url) {
    if (url != appcfg::update_url()) return;
    bool avail = QSimpleUpdater::getInstance()->getUpdateAvailable(url);
    if (m_btn_update)
        m_btn_update->setVisible(avail);   // 有新版本 → 显示"立即更新"按钮
    m_status_left->setText(
        avail ? trl::L("发现新版本,请点击右上角\"立即更新\"下载")
              : trl::L("检查更新完成:暂无可更新版本(若网络不可达请检查连接)"));
}

void MainWindow::on_status_message(const QString& s) {
    m_status_left->setText(trl::L("[状态] ") + s);
}

void MainWindow::on_error(const QString& e) {
    m_status_left->setText(trl::L("[错误] ") + e);
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

void MainWindow::update_selection_delta() {
    if (!m_status_mid || !m_model || !m_table_packets) return;
    const QModelIndexList rows = m_table_packets->selectionModel()->selectedRows();
    if (rows.size() != 2) { m_status_mid->clear(); return; }
    int r0 = rows[0].row(), r1 = rows[1].row();
    if (r0 > r1) { const int t = r0; r0 = r1; r1 = t; }
    PacketEntry a, b;
    if (!m_model->entry_at(r0, a) || !m_model->entry_at(r1, b)) { m_status_mid->clear(); return; }

    // NTB 差(µs):帧内 NTB 为 40ns/tick → tick×40/1000 µs(qint32 差,回绕安全)
    const qint64 ntb_delta_us =
        (qint64)(qint32)(b.meta.timestamp - a.meta.timestamp) * 40 / 1000;
    const qint64 ntb_delta_ms = ntb_delta_us / 1000;
    const qint64 local_delta_ms = b.epoch_ms - a.epoch_ms;   // 本地接收时间差(ms)

    // 优先 NTB;NTB 差与本地时间差偏差 ≥3s 时 NTB 不可信(回绕/跳变)→ 降级本地时间
    const bool use_ntb = qAbs(local_delta_ms - ntb_delta_ms) < 3000;
    const qint64 us = use_ntb ? ntb_delta_us : local_delta_ms * 1000;

    // 格式化:µs / ms / s 自适应
    QString v;
    const bool neg = us < 0;
    const qint64 au = neg ? -us : us;
    if (au < 1000)          v = QStringLiteral("%1 µs").arg(au);
    else if (au < 1000000)  v = QStringLiteral("%1 ms").arg(au / 1000.0, 0, 'f', 3);
    else                    v = QStringLiteral("%1 s").arg(au / 1000000.0, 0, 'f', 6);
    if (neg) v.prepend(QLatin1Char('-'));
    const QString src = use_ntb ? QStringLiteral("NTB") : trl::L("本地时间");
    m_status_mid->setText(QStringLiteral("ΔT: %1 (%2)").arg(v, src));
}

void MainWindow::open_topo_window() {
    if (!m_topo_window) {
        m_topo_window = new TopoWindow(this);
        m_topo_window->set_state_map(&m_topo_states);
        connect(m_topo_window, &TopoWindow::request_live,
                this, &MainWindow::on_topo_request_live);
        connect(m_topo_window, &TopoWindow::request_history,
                this, &MainWindow::on_topo_request_history);
    }
    // 按当前模式同步:历史回放中打开 → 重放冻结快照;否则实时
    if (m_topo_hist_active)
        replay_topo_history();
    else
        m_topo_window->show_live();
    m_topo_window->show();
    m_topo_window->raise();
    m_topo_window->activateWindow();
}
namespace {
// 中→英注册(文件级,仅新增条目;菜单/状态等通用条目见 src/common/i18n.cpp 内置词典)
struct I18nRegMainWindow {
    I18nRegMainWindow() {
        trl::register_en("导出", "Export");
        trl::register_en("设置", "Settings");
        trl::register_en("应用", "Apply");
        trl::register_en("继续", "Resume");
        trl::register_en("作者邮箱", "Author email");
        trl::register_en("导出为回放文件", "Export as replay file");
        trl::register_en("导出为文件", "Export to file");
        trl::register_en("裸 hex 文本 (*.txt)", "Raw hex text (*.txt)");
        trl::register_en("回放文件 (*.bin)", "Replay files (*.bin)");
        trl::register_en("CSV 表格 (*.csv)", "CSV table (*.csv)");
        trl::register_en("[错误] ", "[Error] ");
        trl::register_en("  显示过滤器:", "  Display filter:");
        trl::register_en("字节视图(十六进制,左偏移 + 中间 hex + 右侧 ASCII + RAW DATA):",
                         "Byte view (hex, left offset + middle hex + right ASCII + RAW DATA):");
        trl::register_en("复制(含 0x 前缀)", "Copy (with 0x prefix)");
        trl::register_en("复制(纯 hex)", "Copy (plain hex)");
        trl::register_en("回放进度: %1%", "Replay progress: %1%");
        trl::register_en("立即更新", "Update Now");
        trl::register_en("发现新版本,点击下载安装", "New version available, click to download");
        trl::register_en("协议已更改", "Protocol changed");
        trl::register_en("协议已更改,重启后生效。若点击「开始」则立即生效。",
                         "Protocol changed. Takes effect after restart, or click [Start] to apply immediately.");
        trl::register_en("重启后生效", "Apply after restart");
        trl::register_en("协议已立即生效(Ctrl+E 开始捕获)", "Protocol applied immediately (Ctrl+E to start capture)");
        trl::register_en("本地时间", "Local time");
        trl::register_en("拓扑", "Topology");
    }
};
const I18nRegMainWindow g_i18n_reg_mainwindow;
}  // namespace
