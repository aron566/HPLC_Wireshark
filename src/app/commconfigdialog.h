/// @file commconfigdialog.h
/// @brief 通讯口/数据源设置对话框
#ifndef COMMCONFIGDIALOG_H
#define COMMCONFIGDIALOG_H

#include <QDialog>
#include "serialreader.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QCheckBox;

class CommConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommConfigDialog(QWidget* parent = nullptr,
                              const ReaderConfig& initial = ReaderConfig());

    ReaderConfig config() const { return m_cfg; }

private slots:
    void on_browse_file();
    void on_source_type_changed();
    void on_accept();

private:
    void build_ui();
    void load_initial(const ReaderConfig& c);

    QComboBox*    m_cmb_source_type;
    QComboBox*    m_cmb_port_name;
    QComboBox*    m_cmb_baud_rate;
    QComboBox*    m_cmb_data_bits;
    QComboBox*    m_cmb_stop_bits;
    QComboBox*    m_cmb_parity;
    QLineEdit*    m_edt_file_path;
    QPushButton*  m_btn_browse;
    QCheckBox*    m_chk_time_tag;

    ReaderConfig  m_cfg;
};

#endif // COMMCONFIGDIALOG_H
