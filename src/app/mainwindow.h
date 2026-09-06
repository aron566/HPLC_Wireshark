/// @file mainwindow.h
/// @brief 主窗口
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
    void on_parsed(const BplcParser::Result& r);
    void on_status_message(const QString& s);
    void on_error(const QString& e);
    void on_row_activated(const PacketEntry& e);
    void on_range_selected(int start, int len);
    void on_flush_buffer();
    void refresh_status_bar();

private:
    void build_ui();
    void wire_signals();
    PacketEntry make_entry(const BplcParser::Result& r, qint64 now);
    void enqueue_entry(PacketEntry&& e);

    QToolBar*      m_toolbar;
    QToolButton*   m_btn_start;
    QToolButton*   m_btn_stop;
    QToolButton*   m_btn_pause;
    QToolButton*   m_btn_clear;
    QToolButton*   m_btn_export;
    QToolButton*   m_btn_settings;
    QLineEdit*     m_edt_filter;
    QToolButton*   m_btn_apply_filter;

    QSplitter*     m_splitter_main;
    QTableView*    m_table_packets;
    QSplitter*     m_splitter_bottom;
    ProtocolTree*  m_tree_protocol;
    HexView*       m_hex_view;
    QLabel*        m_lbl_hex_title;

    QLabel*        m_status_left;
    QLabel*        m_status_right;

    SerialReader*    m_reader;
    FrameDispatcher* m_dispatch;
    PacketListModel* m_model;

    QTimer*          m_flush_timer;
    QTimer*          m_status_timer;
    QList<PacketEntry> m_pending;
    QMutex           m_pending_mutex;
    bool             m_paused;

    qint64           m_last_epoch_ms;
    int              m_index_counter;
};

#endif // MAINWINDOW_H
