/// @file mainwindow.h
/// @brief 主窗口
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QByteArray>
#include <QList>
#include <QVector>
#include <atomic>
#include "serialreader.h"
#include "framedispatcher.h"
#include "iprotocolparser.h"
#include "commconfigdialog.h"

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
class QCheckBox;
class QPushButton;
class QDragEnterEvent;
class QDropEvent;

class PacketListModel;
class HexView;
class ProtocolTree;
struct PacketEntry;

class MainWindow : public QMainWindow {
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
    void on_parsed(const ParseResult& r);
    void on_status_message(const QString& s);
    void on_error(const QString& e);
    void on_row_activated(const PacketEntry& e);
    void on_ranges_selected(const QList<QPair<int, int>>& ranges, const QByteArray& copy_bytes);
    void on_flush_buffer();
    void refresh_status_bar();
    void on_check_finished(const QString& url);   // 检查更新结束(QSimpleUpdater)

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void build_ui();
    void wire_signals();
    PacketEntry make_entry(const ParseResult& r, qint64 now);
    void enqueue_entry(PacketEntry&& e);
    void check_for_updates(bool silent);   // 检查更新(silent=true:启动静默检查)
    void rebuild_dispatcher();             // 协议切换立即生效:停止+清空+重建解析器
    void reset_dispatcher();               // 停止旧解析+重建 dispatcher(拖放导入/协议切换共用)
    bool confirm_protocol_rebuild();       // 协议变更提示,返回是否立即生效
    void start_file_import(const QString& path);  // 拖放文件导入(按扩展名判定回放/裸hex)
    void update_selection_delta();  // 选中两行算时间差(NTB 优先,与本地差>3s 时降级本地时间)

    QToolBar*      m_toolbar;
    QToolButton*   m_btn_start;
    QToolButton*   m_btn_stop;
    QToolButton*   m_btn_pause;
    QToolButton*   m_btn_clear;
    QToolButton*   m_btn_export;
    QToolButton*   m_btn_settings;
    QToolButton*   m_btn_update;   ///< 发现新版本时显示的"立即更新"按钮(菜单栏右上角,默认隐藏)
    QLineEdit*     m_edt_filter;
    QToolButton*   m_btn_apply_filter;

    QSplitter*     m_splitter_main;
    QTableView*    m_table_packets;
    QSplitter*     m_splitter_bottom;
    ProtocolTree*  m_tree_protocol;
    HexView*       m_hex_view;
    QLabel*        m_lbl_hex_title;
    QSplitter*     m_split_hex;       ///< HexView | 原始报文 水平分离
    QPlainTextEdit* m_raw_view;       ///< 原始串口帧(0x3C...0x3E)只读文本(常显)
    QByteArray     m_raw_bytes;       ///< 当前帧原始字节(右键复制用)

    QLabel*        m_status_left;
    QLabel*        m_status_mid;    ///< 状态栏中间:选中两行时间差显示
    QLabel*        m_status_right;

    SerialReader*    m_reader;
    FrameDispatcher* m_dispatch;
    PacketListModel* m_model;

    QTimer*          m_flush_timer;
    QTimer*          m_status_timer;
    QList<PacketEntry> m_pending;
    QMutex           m_pending_mutex;
    bool             m_paused;
    bool             m_exporting;      ///< 导出中(on_flush_buffer 暂停 append 进模型)
    bool             m_follow_bottom;  ///< 是否自动滚动到最新帧(用户滚离底部则暂停)

    quint32          m_last_ntb;     ///< 上一帧帧内 NTB(tick),Delta 统一用 NTB 差
    int              m_index_counter;
    std::atomic<int> m_pending_count{0};  ///< pending 帧数(原子,阈值触发同步 flush 用)
};

#endif // MAINWINDOW_H
