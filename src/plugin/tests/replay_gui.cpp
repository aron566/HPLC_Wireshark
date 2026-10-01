/// @file replay_gui.cpp
/// @brief 真实帧回灌 GUI 工具实现
#include "replay_gui.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QFile>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QProcess>
#include <QDir>
#include <QStandardPaths>

#include "i18n.h"
#include "plugin_backend.h"
#include "js_backend.h"
#include "lua_backend.h"
#include "../plugin_api/plugin_manifest.h"

namespace {

// ---- 后端创建(与 replay_real_frames 相同的复用逻辑) ----
IPluginBackend* create_backend(const PluginManifest& m, QString* err) {
    if (m.runtime == QStringLiteral("js")) {
        auto* b = new JsBackend();
        if (!b->initialize(m, err)) { delete b; return nullptr; }
        return b;
    }
    if (m.runtime == QStringLiteral("lua")) {
        auto* b = new LuaBackend();
        if (!b->initialize(m, err)) { delete b; return nullptr; }
        return b;
    }
    if (err) *err = QStringLiteral("unknown runtime: %1").arg(m.runtime);
    return nullptr;
}

// 在字段树中找指定名的节点
const MsduFieldNode* find_node(const QVector<MsduFieldNode>& v, const QString& name) {
    for (const auto& n : v)
        if (n.name == name) return &n;
    return nullptr;
}

} // namespace

// =====================================================================
// 拆帧:与 SerialReader 相同的线帧语义
//   - 0x3C 起,首个 0x3E 止;0x3C 后若先遇到另一个 0x3C 则丢弃前者(假帧头)
//   - 返回含哨兵、0x3D 转义原样的线帧(与插件后端期望的输入一致)
//   - 8B BCD 时间标签不含 0x3C/0x3D/0x3E,扫描 0x3C 时自然跳过
// =====================================================================
QList<QByteArray> split_wire_frames(const QByteArray& data, int max_n) {
    QList<QByteArray> frames;
    const int n = data.size();
    int i = 0;
    while (i < n && (max_n <= 0 || frames.size() < max_n)) {
        if (static_cast<quint8>(data[i]) != 0x3C) { ++i; continue; }
        const int e3e = data.indexOf(char(0x3E), i + 1);
        if (e3e < 0) break;
        const int e3c = data.indexOf(char(0x3C), i + 1);
        if (e3c >= 0 && e3c < e3e) { i = e3c; continue; }  // 假帧头,重对齐
        if (e3e - i + 1 <= 4096)
            frames.append(data.mid(i, e3e - i + 1));
        i = e3e + 1;
    }
    return frames;
}

// =====================================================================
// 英文词典注册
// =====================================================================
void replay_gui_register_en() {
    using trl::register_en;
    register_en("插件真实帧回灌", "Plugin Real-Frame Replay");
    register_en("抓包文件", "Capture file");
    register_en("浏览...", "Browse...");
    register_en("插件目录", "Plugin dir");
    register_en("最大帧数(0=全部)", "Max frames (0=all)");
    register_en("开始", "Start");
    register_en("停止", "Stop");
    register_en("拓扑", "Topology");
    register_en("回放", "Replay");
    register_en("诊断", "Diagnostics");
    register_en("报表", "Report");
    register_en("日志", "Log");
    register_en("序号", "Seq");
    register_en("长度", "Length");
    register_en("到达时间(us)", "Arrival time (us)");
    register_en("摘要", "Summary");
    register_en("帧号", "Frame #");
    register_en("问题", "Problem");
    register_en("详情", "Detail");
    register_en("拓扑渲染图将显示在这里", "Topology rendering will appear here");
    register_en("已回灌 %1 帧,拓扑图实时更新中...", "Replayed %1 frames, topology updating...");
    register_en("共 %1 帧,接受 %2,拒绝 %3,耗时 %4 ms\n最后: %5",
                "%1 frames total, accept %2, reject %3, %4 ms\nLast: %5");
    register_en("选择抓包文件", "Select capture file");
    register_en("抓包文件", "Capture files");
    register_en("全部文件", "All files");
    register_en("提示", "Notice");
    register_en("请先选择抓包文件", "Please select a capture file first");
    register_en("请至少勾选一个插件", "Please select at least one plugin");
}

// =====================================================================
// ReplayWorker
// =====================================================================
ReplayWorker::ReplayWorker(const Job& job, QObject* parent)
    : QThread(parent), m_job(job) {}

void ReplayWorker::request_stop() { m_stop.store(true); }

void ReplayWorker::run() {
    QVector<PluginReplayResult> results;

    QFile f(m_job.bin_path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit log_line(QStringLiteral("ERROR: cannot open capture file: %1")
                          .arg(m_job.bin_path));
        emit finished(results);
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    emit log_line(QStringLiteral("capture file: %1 (%2 bytes)")
                      .arg(m_job.bin_path).arg(data.size()));

    QList<QByteArray> frames = split_wire_frames(data, m_job.max_frames);
    emit log_line(QStringLiteral("extracted %1 wire frames").arg(frames.size()));
    if (frames.isEmpty()) {
        emit finished(results);
        return;
    }

    const int preview_step = qMax(1, frames.size() / 20);

    for (const QString& pname : m_job.plugins) {
        if (m_stop.load()) break;
        PluginReplayResult pr;
        pr.name = pname;
        emit log_line(QStringLiteral("--- %1 ---").arg(pname));

        QString err;
        const PluginManifest m =
            read_plugin_manifest(m_job.examples_dir + QLatin1Char('/') + pname);
        if (!m.error.isEmpty()) {
            pr.error = QStringLiteral("manifest: %1").arg(m.error);
            emit log_line(QStringLiteral("FAIL manifest: %1").arg(m.error));
            results.append(pr);
            continue;
        }
        IPluginBackend* b = create_backend(m, &err);
        if (!b) {
            pr.error = QStringLiteral("backend: %1").arg(err);
            emit log_line(QStringLiteral("FAIL backend: %1").arg(err));
            results.append(pr);
            continue;
        }

        QElapsedTimer t;
        t.start();
        int last_progress = 0;
        for (int i = 0; i < frames.size(); ++i) {
            if (m_stop.load()) break;
            BplcFrame fr;
            fr.data = frames[i];
            fr.arrival_us = static_cast<qint64>(i) * 1000LL;
            MsduState msdu;
            ParseFilter filter;
            QString perr;
            const ParseResult r = b->parse(fr, msdu, filter, &perr);
            if (perr.isEmpty() && r.accept) {
                ++pr.accept;
                pr.last_summary = r.msdu.summary;
                // diag:收集 Diagnosis 子节点告警
                if (pname == QStringLiteral("lua_diag")) {
                    if (const MsduFieldNode* d = find_node(r.msdu.tree, QStringLiteral("Diagnosis"))) {
                        for (const auto& c : d->children) {
                            if (pr.alarms.size() < 2000)
                                pr.alarms.append({i + 1, c.name, c.value});
                        }
                    }
                }
                // replay:保留最近 300 行
                if (pname == QStringLiteral("js_replay")) {
                    pr.replay_rows.append({pr.accept, frames[i].size(),
                                           fr.arrival_us, r.msdu.summary});
                    while (pr.replay_rows.size() > 300)
                        pr.replay_rows.removeFirst();
                }
            } else {
                ++pr.reject;
            }
            if (i - last_progress >= 200) {
                last_progress = i;
                emit progress(i + 1, frames.size());
            }
            // topo:定期渲染预览
            if (pname == QStringLiteral("js_topo") && b->has_graphics() &&
                (i % preview_step == 0 || i + 1 == frames.size())) {
                QString rerr;
                const QImage img = b->render_graphics(400, 300, &rerr);
                if (!img.isNull()) emit topo_preview(img, i + 1);
            }
        }
        pr.elapsed_ms = t.elapsed();
        emit progress(frames.size(), frames.size());

        // 插件特有收尾
        if (pname == QStringLiteral("js_topo") && b->has_graphics()) {
            QString rerr;
            pr.final_image = b->render_graphics(400, 300, &rerr);
            if (pr.final_image.isNull() && pr.error.isEmpty())
                pr.error = QStringLiteral("render failed: %1").arg(rerr);
        } else if (pname == QStringLiteral("lua_report")) {
            QString rerr;
            pr.extra_text = b->call_text_function("get_report", &rerr);
            if (pr.extra_text.isEmpty() && pr.error.isEmpty())
                pr.error = QStringLiteral("get_report: %1").arg(rerr);
        } else if (pname == QStringLiteral("js_replay")) {
            QString rerr;
            pr.extra_text = b->call_text_function("get_replay_data", &rerr);
            if (pr.extra_text.isEmpty() && pr.error.isEmpty())
                pr.error = QStringLiteral("get_replay_data: %1").arg(rerr);
        }
        pr.ok = pr.error.isEmpty();
        emit log_line(QStringLiteral(
                          "%1: %2 frames, accept %3, reject %4, %5 ms")
                          .arg(pname).arg(frames.size()).arg(pr.accept)
                          .arg(pr.reject).arg(pr.elapsed_ms));
        delete b;
        results.append(pr);
    }
    emit finished(results);
}

// =====================================================================
// ReplayMainWindow
// =====================================================================
ReplayMainWindow::ReplayMainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(trl::L("插件真实帧回灌"));
    resize(980, 720);

    auto* central = new QWidget(this);
    auto* main = new QVBoxLayout(central);

    // --- 文件行 ---
    auto* row_file = new QHBoxLayout();
    row_file->addWidget(new QLabel(trl::L("抓包文件") + ":"));
    m_ed_bin = new QLineEdit();
    m_ed_bin->setPlaceholderText("BPLC_20260914_105445.bin");
    row_file->addWidget(m_ed_bin, 1);
    auto* btn_browse = new QPushButton(trl::L("浏览..."));
    connect(btn_browse, &QPushButton::clicked, this, &ReplayMainWindow::on_browse);
    row_file->addWidget(btn_browse);
    main->addLayout(row_file);

    // --- 插件目录行 ---
    auto* row_ex = new QHBoxLayout();
    row_ex->addWidget(new QLabel(trl::L("插件目录") + ":"));
    m_ed_examples = new QLineEdit();
    row_ex->addWidget(m_ed_examples, 1);
    main->addLayout(row_ex);

    // --- 控制行 ---
    auto* row_ctl = new QHBoxLayout();
    m_ck_replay = new QCheckBox("js_replay"); m_ck_replay->setChecked(true);
    m_ck_topo = new QCheckBox("js_topo");     m_ck_topo->setChecked(true);
    m_ck_diag = new QCheckBox("lua_diag");    m_ck_diag->setChecked(true);
    m_ck_report = new QCheckBox("lua_report"); m_ck_report->setChecked(true);
    row_ctl->addWidget(m_ck_replay);
    row_ctl->addWidget(m_ck_topo);
    row_ctl->addWidget(m_ck_diag);
    row_ctl->addWidget(m_ck_report);
    row_ctl->addWidget(new QLabel(trl::L("最大帧数(0=全部)") + ":"));
    m_sp_max = new QSpinBox();
    m_sp_max->setRange(0, 1000000);
    m_sp_max->setValue(5000);
    m_sp_max->setSingleStep(1000);
    row_ctl->addWidget(m_sp_max);
    row_ctl->addStretch(1);
    m_btn_start = new QPushButton(trl::L("开始"));
    m_btn_stop = new QPushButton(trl::L("停止"));
    m_btn_stop->setEnabled(false);
    connect(m_btn_start, &QPushButton::clicked, this, &ReplayMainWindow::on_start);
    connect(m_btn_stop, &QPushButton::clicked, this, &ReplayMainWindow::on_stop);
    row_ctl->addWidget(m_btn_start);
    row_ctl->addWidget(m_btn_stop);
    main->addLayout(row_ctl);

    m_progress = new QProgressBar();
    m_progress->setRange(0, 100);
    main->addWidget(m_progress);

    // --- 结果页 ---
    auto* tabs = new QTabWidget();

    // 拓扑
    auto* w_topo = new QWidget();
    auto* lay_topo = new QVBoxLayout(w_topo);
    m_lb_topo = new QLabel();
    m_lb_topo->setMinimumSize(400, 300);
    m_lb_topo->setAlignment(Qt::AlignCenter);
    m_lb_topo->setStyleSheet("QLabel { background: #202020; color: #888; }");
    m_lb_topo->setText(trl::L("拓扑渲染图将显示在这里"));
    m_lb_topo->setScaledContents(false);
    lay_topo->addWidget(m_lb_topo, 1);
    m_te_topo_stat = new QTextEdit();
    m_te_topo_stat->setReadOnly(true);
    m_te_topo_stat->setMaximumHeight(80);
    lay_topo->addWidget(m_te_topo_stat);
    tabs->addTab(w_topo, trl::L("拓扑"));

    // 回放
    m_tw_replay = new QTableWidget(0, 4);
    m_tw_replay->setHorizontalHeaderLabels(
        {trl::L("序号"), trl::L("长度"), trl::L("到达时间(us)"), trl::L("摘要")});
    m_tw_replay->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tw_replay->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_tw_replay, trl::L("回放"));

    // 诊断
    m_tw_diag = new QTableWidget(0, 3);
    m_tw_diag->setHorizontalHeaderLabels(
        {trl::L("帧号"), trl::L("问题"), trl::L("详情")});
    m_tw_diag->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tw_diag->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_tw_diag, trl::L("诊断"));

    // 报表
    m_te_report = new QTextEdit();
    m_te_report->setReadOnly(true);
    m_te_report->setFontFamily("monospace");
    tabs->addTab(m_te_report, trl::L("报表"));

    // 日志
    m_te_log = new QTextEdit();
    m_te_log->setReadOnly(true);
    m_te_log->setFontFamily("monospace");
    tabs->addTab(m_te_log, trl::L("日志"));

    main->addWidget(tabs, 1);
    setCentralWidget(central);

    // --- 恢复上次设置 ---
    QSettings s("BPLC", "PluginReplayGui");
    const QString last_bin = s.value("bin").toString();
    if (!last_bin.isEmpty()) {
        m_ed_bin->setText(last_bin);
    } else {
        const QString tmp_bin =
            QDir::tempPath() + "/replay/BPLC_20260914_105445.bin";
        if (QFile::exists(tmp_bin)) m_ed_bin->setText(tmp_bin);
    }
    const QString last_ex = s.value("examples").toString();
    if (!last_ex.isEmpty()) {
        m_ed_examples->setText(last_ex);
    } else {
        m_ed_examples->setText(
            QCoreApplication::applicationDirPath() + "/../examples");
    }
    m_sp_max->setValue(s.value("max_frames", 5000).toInt());
}

void ReplayMainWindow::on_browse() {
    const QString f = QFileDialog::getOpenFileName(
        this, trl::L("选择抓包文件"), m_ed_bin->text(),
        trl::L("抓包文件") + " (*.bin);;" + trl::L("全部文件") + " (*)");
    if (!f.isEmpty()) m_ed_bin->setText(f);
}

void ReplayMainWindow::log(const QString& s) {
    m_te_log->append(s);
}

void ReplayMainWindow::set_running(bool running) {
    m_btn_start->setEnabled(!running);
    m_btn_stop->setEnabled(running);
    m_ed_bin->setEnabled(!running);
    m_ed_examples->setEnabled(!running);
}

// 若 bin 不存在,尝试从同名 .7z 自动解压(持久位置优先)
static bool ensure_bin(const QString& bin_path, QString* log_out) {
    if (QFile::exists(bin_path)) return true;
    QStringList candidates;
    QString base = bin_path;
    if (base.endsWith(".bin", Qt::CaseInsensitive))
        base.chop(4);
    candidates << base + ".7z" << base + ".7Z";
    candidates << QDir::homePath() + "/workspace/user/files/BPLC_20260914_105445.7z";
    for (const QString& z : candidates) {
        if (!QFile::exists(z)) continue;
        *log_out = QStringLiteral("bin missing, extracting from %1 ...").arg(z);
        QDir().mkpath(QFileInfo(bin_path).absolutePath());
        QProcess py;
        py.start("python3",
                 {"-c",
                  "import py7zr,sys; py7zr.SevenZipFile(sys.argv[1]).extractall(sys.argv[2])",
                  z, QFileInfo(bin_path).absolutePath()});
        if (py.waitForFinished(120000) && py.exitCode() == 0 &&
            QFile::exists(bin_path)) {
            *log_out += QStringLiteral(" done.");
            return true;
        }
        *log_out += QStringLiteral(" FAILED: %1")
                        .arg(QString::fromUtf8(py.readAllStandardError()));
        return false;
    }
    *log_out = QStringLiteral("bin file not found: %1").arg(bin_path);
    return false;
}

void ReplayMainWindow::on_start() {
    QString bin = m_ed_bin->text().trimmed();
    if (bin.isEmpty()) {
        QMessageBox::warning(this, trl::L("提示"), trl::L("请先选择抓包文件"));
        return;
    }
    QString note;
    if (!ensure_bin(bin, &note)) {
        QMessageBox::warning(this, trl::L("提示"), note);
        return;
    }
    if (!note.isEmpty()) log(note);

    QStringList plugins;
    if (m_ck_replay->isChecked()) plugins << "js_replay";
    if (m_ck_topo->isChecked()) plugins << "js_topo";
    if (m_ck_diag->isChecked()) plugins << "lua_diag";
    if (m_ck_report->isChecked()) plugins << "lua_report";
    if (plugins.isEmpty()) {
        QMessageBox::warning(this, trl::L("提示"), trl::L("请至少勾选一个插件"));
        return;
    }

    QSettings s("BPLC", "PluginReplayGui");
    s.setValue("bin", bin);
    s.setValue("examples", m_ed_examples->text());
    s.setValue("max_frames", m_sp_max->value());

    // 清空旧结果
    m_tw_replay->setRowCount(0);
    m_tw_diag->setRowCount(0);
    m_te_report->clear();
    m_te_topo_stat->clear();
    m_lb_topo->setPixmap(QPixmap());
    m_lb_topo->setText(trl::L("拓扑渲染图将显示在这里"));
    m_progress->setValue(0);

    ReplayWorker::Job job;
    job.examples_dir = m_ed_examples->text();
    job.bin_path = bin;
    job.plugins = plugins;
    job.max_frames = m_sp_max->value();

    m_worker = new ReplayWorker(job, this);
    connect(m_worker, &ReplayWorker::progress, this, &ReplayMainWindow::on_progress);
    connect(m_worker, &ReplayWorker::log_line, this, &ReplayMainWindow::on_log);
    connect(m_worker, &ReplayWorker::topo_preview, this,
            &ReplayMainWindow::on_topo_preview);
    connect(m_worker, &ReplayWorker::finished, this,
            &ReplayMainWindow::on_finished);
    connect(m_worker, &ReplayWorker::finished, m_worker, &QObject::deleteLater);
    set_running(true);
    log(tr("--- replay start ---"));
    m_worker->start();
}

void ReplayMainWindow::on_stop() {
    if (m_worker) {
        log(tr("stop requested..."));
        m_worker->request_stop();
        m_btn_stop->setEnabled(false);
    }
}

void ReplayMainWindow::on_progress(int done, int total) {
    if (total > 0) m_progress->setValue(done * 100 / total);
}

void ReplayMainWindow::on_log(const QString& s) { log(s); }

void ReplayMainWindow::on_topo_preview(const QImage& img, int done) {
    if (img.isNull()) return;
    m_lb_topo->setPixmap(QPixmap::fromImage(img).scaled(
        m_lb_topo->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_te_topo_stat->setText(
        trl::L("已回灌 %1 帧,拓扑图实时更新中...").arg(done));
}

void ReplayMainWindow::on_finished(const QVector<PluginReplayResult>& results) {
    m_worker = nullptr;
    set_running(false);
    m_progress->setValue(100);

    for (const auto& pr : results) {
        if (!pr.ok) {
            log(QStringLiteral("[%1] FAIL: %2").arg(pr.name, pr.error));
            continue;
        }
        log(QStringLiteral("[%1] accept=%2 reject=%3 %4ms")
                .arg(pr.name).arg(pr.accept).arg(pr.reject).arg(pr.elapsed_ms));

        if (pr.name == QStringLiteral("js_topo")) {
            if (!pr.final_image.isNull()) {
                m_lb_topo->setPixmap(QPixmap::fromImage(pr.final_image).scaled(
                    m_lb_topo->size(), Qt::KeepAspectRatio,
                    Qt::SmoothTransformation));
            }
            m_te_topo_stat->setText(
                trl::L("共 %1 帧,接受 %2,拒绝 %3,耗时 %4 ms\n最后: %5")
                    .arg(pr.accept + pr.reject).arg(pr.accept).arg(pr.reject)
                    .arg(pr.elapsed_ms).arg(pr.last_summary));
        } else if (pr.name == QStringLiteral("js_replay")) {
            m_tw_replay->setRowCount(pr.replay_rows.size());
            for (int i = 0; i < pr.replay_rows.size(); ++i) {
                const auto& r = pr.replay_rows[i];
                m_tw_replay->setItem(i, 0, new QTableWidgetItem(QString::number(r.seq)));
                m_tw_replay->setItem(i, 1, new QTableWidgetItem(QString::number(r.len)));
                m_tw_replay->setItem(i, 2,
                    new QTableWidgetItem(QString::number(r.arrival_us)));
                m_tw_replay->setItem(i, 3, new QTableWidgetItem(r.summary));
            }
            if (!pr.extra_text.isEmpty())
                log(tr("[js_replay] get_replay_data: %1 lines")
                        .arg(pr.extra_text.count('\n') + 1));
        } else if (pr.name == QStringLiteral("lua_diag")) {
            m_tw_diag->setRowCount(pr.alarms.size());
            for (int i = 0; i < pr.alarms.size(); ++i) {
                const auto& a = pr.alarms[i];
                m_tw_diag->setItem(i, 0,
                    new QTableWidgetItem(QString::number(a.frame_no)));
                m_tw_diag->setItem(i, 1, new QTableWidgetItem(a.problem));
                m_tw_diag->setItem(i, 2, new QTableWidgetItem(a.detail));
            }
            log(tr("[lua_diag] alarms: %1").arg(pr.alarms.size()));
        } else if (pr.name == QStringLiteral("lua_report")) {
            m_te_report->setPlainText(pr.extra_text);
            log(tr("[lua_report] report received (%1 chars)")
                    .arg(pr.extra_text.size()));
        }
    }
    log(tr("--- replay done ---"));
}
