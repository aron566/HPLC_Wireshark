/// @file mainwindow.cpp
/// @brief 主窗口实现
#include "mainwindow.h"

#include "packetlistmodel.h"
#include "hexview.h"
#include "protocoltree.h"
#include "commconfigdialog.h"
#include "QSimpleUpdater.h"
#include "appconfig.h"
#include "playbackwriter.h"
#include "i18n.h"

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

#include <QIcon>

namespace {
// 当前版本与仓库信息(更新检查地址见 config.ini [general] update_url)
const QString kAppVersion = QStringLiteral("1.0.16");
const QString kModuleName = QStringLiteral("BPLC STA Monitor");
const QString kAuthorName = QStringLiteral("aron566");
const QString kAuthorEmail = QStringLiteral("aron566@163.com");
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
      m_paused(false), m_follow_bottom(true),
      m_last_epoch_ms(0), m_last_rx_us(0),
      m_index_counter(0) {
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
    pal.setColor(QPalette::Highlight, QColor("#3d6f9f"));
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
            for (char c : m_raw_bytes)
                out += QStringLiteral("0x%1 ").arg(quint8(c), 2, 16, QChar('0'));
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
    m_status_right = new QLabel(this);
    m_status_right->setAlignment(Qt::AlignRight);
    statusBar()->addWidget(m_status_left, 1);
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
        auto* su = QSimpleUpdater::getInstance();
        const QString up_url = appcfg::update_url();   // 更新地址来自 config.ini
        su->setModuleVersion(up_url, kAppVersion);
        su->setModuleName(up_url, kModuleName);
        su->setNotifyOnUpdate(up_url, true);   // 发现新版本 → 弹窗询问下载
        su->setNotifyOnFinish(up_url, true);   // 无新版本/清单正常 → 弹窗告知
        m_status_left->setText(trl::L("正在检查更新…"));
        su->checkForUpdates(up_url);
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
    m_btn_pause->setText(m_paused ? trl::L("继续") : trl::L("暂停"));
    if (m_paused) m_status_left->setText(QStringLiteral("Paused"));
}

void MainWindow::on_clear() {
    m_model->clear_all();
    m_index_counter = 0;
    m_last_epoch_ms = 0;
    m_last_rx_us = 0;
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
    if (m_model->total_count() == 0) {
        m_status_left->setText(trl::L("无可导出的帧"));
        return;
    }
    // 两种导出格式:①回放 bin(0x3C 封装帧流+BCD 时间标签)
    // ②裸数据 hex 文本(每行一帧,无 0x3C/0x3E/0x3D 封装):
    //   [ts 4B LE][phr_mcs][option][channel][isRF][MPDU];回放(RawHex)
    //   按 ts 还原捕获时刻,缺失时回退本地时间
    QString selected;
    const QString filter = trl::L("回放文件 (*.bin)") + QStringLiteral(";;") +
                           trl::L("裸 hex 文本 (*.txt)");
    QString f = QFileDialog::getSaveFileName(
        this, trl::L("导出为文件"),
        "BPLC_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") +
            (selected.contains(QStringLiteral(".txt")) ? ".txt" : ".bin"),
        filter, &selected);
    if (f.isEmpty()) return;
    const bool as_text = selected.contains(QStringLiteral(".txt"));
    if (as_text && !f.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        f += QStringLiteral(".txt");
    if (!as_text && !f.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
        f += QStringLiteral(".bin");
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly)) {
        m_status_left->setText(trl::L("导出失败:%1").arg(out.errorString()));
        return;
    }

    // 磁盘换页模型下按全局顺序流式遍历全部帧导出(不一次性载入内存)
    if (as_text) {
        playback::RawHexWriter w(&out);
        m_model->for_each_entry([&](const PacketEntry& e) { w.add(e); });
    } else {
        playback::PlaybackBinWriter w(&out);
        m_model->for_each_entry([&](const PacketEntry& e) { w.add(e); });
    }
    out.close();
    if (out.size() == 0) {
        m_status_left->setText(trl::L("没有可写入的帧数据"));
        return;
    }
    m_status_left->setText(
        trl::L("已导出 %1 帧 → %2").arg(m_model->total_count()).arg(f));
}

void MainWindow::on_settings() {
    ReaderConfig init = load_config_from_settings();
    CommConfigDialog dlg(this, init);
    if (dlg.exec() != QDialog::Accepted) return;
    save_config_to_settings(dlg.config());
    m_status_left->setText(trl::L("配置已保存(Ctrl+E 开始捕获)"));
}

void MainWindow::on_apply_filter() {
    const QString expr = m_edt_filter->text().trimmed();
    m_model->set_display_filter(expr);
    appcfg::set_filter(expr);              // 记忆到 config.ini,下次启动恢复
}

PacketEntry MainWindow::make_entry(const BplcParser::Result& r, qint64 now) {
    PacketEntry e;
    e.index     = ++m_index_counter;
    // 带时间标签(回放导出的 bin)时用帧内绝对时刻,保证 Time/Delta/再导出
    // 均以原始捕获时间为基准;否则退化为本地接收时刻
    qint64 t = (r.meta.frame_time.isValid())
                   ? r.meta.frame_time.toMSecsSinceEpoch() : now;
    e.epoch_ms  = t;
    e.accepted  = r.accept;     // 先落 accepted,Delta/last 追踪依赖它
    e.reason    = r.reject_reason;
    // Delta:不用 NTB(各设备 tick 不同轴,实测不可比)。实时串口帧在
    // 收到起始分节符 0x3C 的当下打单调 µs 时刻 → 接收时刻差(µs 分辨);
    // 文件回放/无高精度打点(0x3C 未逐帧记录)时用帧时间戳 epoch ms 差
    const qint64 ms_fallback = (m_last_epoch_ms == 0)
                                   ? 0 : (t - m_last_epoch_ms) * 1000;
    if (r.meta.seg_start) {
        e.delta_us = 0;                            // 跨段断点:不计算与上一帧 delta
    } else if (r.arrival_us > 0 && m_last_rx_us > 0) {
        const qint64 d = r.arrival_us - m_last_rx_us;
        if (d > 0 && d <= playback::ntb_to_us(playback::kMaxNtbGapTicks))
            e.delta_us = d;                       // 正常接收间隔
        else if (d > 0)
            e.delta_us = 0;                       // 长时间无报文:不计算与上一帧 delta
        else
            e.delta_us = ms_fallback;
    } else {
        e.delta_us = ms_fallback;
    }
    if (r.arrival_us > 0) m_last_rx_us = r.arrival_us;
    m_last_epoch_ms = t;
    e.meta      = r.meta;
    e.mpdu      = r.mpdu;
    e.msdu_body = r.msdu_body;
    e.raw_wire  = r.raw_wire;
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
}

void MainWindow::on_range_selected(int start, int len) {
    m_hex_view->highlight_range(start, len);
}

void MainWindow::on_check_finished(const QString& url) {
    if (url != appcfg::update_url()) return;
    bool avail = QSimpleUpdater::getInstance()->getUpdateAvailable(url);
    m_status_left->setText(
        avail ? trl::L("发现新版本,请按提示下载更新")
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
        trl::register_en("[错误] ", "[Error] ");
        trl::register_en("  显示过滤器:", "  Display filter:");
        trl::register_en("字节视图(十六进制,左偏移 + 中间 hex + 右侧 ASCII + RAW DATA):",
                         "Byte view (hex, left offset + middle hex + right ASCII + RAW DATA):");
        trl::register_en("复制(含 0x 前缀)", "Copy (with 0x prefix)");
        trl::register_en("复制(纯 hex)", "Copy (plain hex)");
        trl::register_en("回放进度: %1%", "Replay progress: %1%");
    }
};
const I18nRegMainWindow g_i18n_reg_mainwindow;
}  // namespace
