#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QByteArray>
#include <QList>
#include <QVector>
#include "serialreader.h"
#include "framedispatcher.h"
#include "bplcparser.h"
#include "commconfigdialog.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QToolBar;
class QToolButton;
class QLineEdit;
class QLabel;
class QTableView;
class QTreeWidget;
class QPlainTextEdit;
class QSplitter;
class QStatusBar;
QT_END_NAMESPACE

class PacketListModel;
class HexView;
class ProtocolTree;
struct PacketEntry;

class MainWindow : public q_main_window {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_start();
    void on_stop();
    void on_pause();
    void on_clear();
    void on_export();
    void on_settings();
    void on_apply_filter();
    void on_parsed(const BplcParser::result& r);
    void on_status_message(const QString& s);
    void on_error(const QString& e);
    void on_packet_selected(const QModelIndex& idx, const QModelIndex& prev);
    void on_range_selected(int start, int len);
    void on_flush_buffer();           // 定时刷新:把积攒的 parsed 推到 model
    void refresh_status_bar();

private:
    void build_ui();                 // 不用 .ui,代码搭界面
    void wire_signals();
    PacketEntry make_entry(const BplcParser::result& r, qint64 now);
    void enqueue_entry(PacketEntry&& e);

    // 控件
    QToolBar*      m_toolbar = nullptr;
    QToolButton*   m_btnStart = nullptr;
    QToolButton*   m_btnStop = nullptr;
    QToolButton*   m_btnPause = nullptr;
    QToolButton*   m_btnClear = nullptr;
    QToolButton*   m_btnExport = nullptr;
    QToolButton*   m_btnSettings = nullptr;
    QLineEdit*     m_edtFilter = nullptr;
    QToolButton*   m_btnApplyFilter = nullptr;

    QSplitter*     m_splitterMain = nullptr;
    QTableView*    m_tablePackets = nullptr;
    QSplitter*     m_splitterBottom = nullptr;
    ProtocolTree*  m_treeProtocol = nullptr;
    HexView*       m_hexView = nullptr;
    QLabel*        m_lblHexTitle = nullptr;

    QLabel*        m_statusLeft = nullptr;
    QLabel*        m_statusRight = nullptr;

    // 后端
    SerialReader*    m_reader   = nullptr;
    FrameDispatcher* m_dispatch = nullptr;
    PacketListModel* m_model    = nullptr;

    // 缓冲 + 节流
    QTimer*          m_flushTimer = nullptr;
    QTimer*          m_statusTimer = nullptr;
    QList<PacketEntry> m_pending;          // 等待 flush 到 model
    QMutex           m_pendingMutex;
    bool             m_paused = false;

    qint64           m_lastEpochMs = 0;
    int              m_indexCounter = 0;
};

#endif // MAINWINDOW_H
