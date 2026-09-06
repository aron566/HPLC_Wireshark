/// @file commconfigdialog.cpp
/// @brief 通讯口配置对话框实现
#include "commconfigdialog.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QSerialPortInfo>
#include <QSerialPort>

CommConfigDialog::CommConfigDialog(QWidget* parent, const ReaderConfig& initial)
    : QDialog(parent),
      m_cmb_source_type(nullptr), m_cmb_port_name(nullptr),
      m_cmb_baud_rate(nullptr), m_cmb_data_bits(nullptr),
      m_cmb_stop_bits(nullptr), m_cmb_parity(nullptr),
      m_edt_file_path(nullptr), m_btn_browse(nullptr),
      m_chk_time_tag(nullptr) {
    setWindowTitle(QStringLiteral("通讯口设置"));
    setMinimumWidth(420);
    build_ui();
    load_initial(initial);
}

void CommConfigDialog::build_ui() {
    auto* root = new QVBoxLayout(this);

    auto* src_group = new QGroupBox(QStringLiteral("数据源类型"), this);
    auto* src_lay = new QHBoxLayout(src_group);
    m_cmb_source_type = new QComboBox(src_group);
    m_cmb_source_type->addItem(QStringLiteral("实时串口"),       int(ReaderMode::SerialPort));
    m_cmb_source_type->addItem(QStringLiteral("文件回放 (.bin)"), int(ReaderMode::FilePlayback));
    m_cmb_source_type->addItem(QStringLiteral("裸 hex 文本"),    int(ReaderMode::RawHex));
    src_lay->addWidget(new QLabel(QStringLiteral("类型:"), src_group));
    src_lay->addWidget(m_cmb_source_type, 1);
    root->addWidget(src_group);

    auto* port_group = new QGroupBox(QStringLiteral("串口参数"), this);
    auto* port_form = new QFormLayout(port_group);

    m_cmb_port_name = new QComboBox(port_group);
    m_cmb_port_name->setEditable(true);
    {
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto& p : ports) {
            m_cmb_port_name->addItem(p.portName() + " - " + p.description(), p.portName());
        }
        if (m_cmb_port_name->count() == 0) {
            m_cmb_port_name->addItem("COM1");
            m_cmb_port_name->addItem("COM3");
            m_cmb_port_name->addItem("/dev/ttyUSB0");
        }
    }

    m_cmb_baud_rate = new QComboBox(port_group);
    m_cmb_baud_rate->setEditable(true);
    m_cmb_baud_rate->addItems({"9600", "19200", "38400", "57600", "115200",
                               "230400", "460800", "921600", "1500000"});

    m_cmb_data_bits = new QComboBox(port_group);
    m_cmb_data_bits->addItems({"5", "6", "7", "8"});

    m_cmb_stop_bits = new QComboBox(port_group);
    m_cmb_stop_bits->addItems({"1", "1.5", "2"});

    m_cmb_parity = new QComboBox(port_group);
    m_cmb_parity->addItem(QStringLiteral("无"),     int(QSerialPort::NoParity));
    m_cmb_parity->addItem(QStringLiteral("奇"),     int(QSerialPort::OddParity));
    m_cmb_parity->addItem(QStringLiteral("偶"),     int(QSerialPort::EvenParity));
    m_cmb_parity->addItem(QStringLiteral("标记"),   int(QSerialPort::MarkParity));
    m_cmb_parity->addItem(QStringLiteral("空"),     int(QSerialPort::SpaceParity));

    port_form->addRow(QStringLiteral("串口:"),   m_cmb_port_name);
    port_form->addRow(QStringLiteral("波特率:"), m_cmb_baud_rate);
    port_form->addRow(QStringLiteral("数据位:"), m_cmb_data_bits);
    port_form->addRow(QStringLiteral("停止位:"), m_cmb_stop_bits);
    port_form->addRow(QStringLiteral("校验位:"), m_cmb_parity);

    root->addWidget(port_group);

    auto* file_group = new QGroupBox(QStringLiteral("文件参数"), this);
    auto* file_lay = new QHBoxLayout(file_group);
    m_edt_file_path = new QLineEdit(file_group);
    m_btn_browse = new QPushButton(QStringLiteral("浏览..."), file_group);
    file_lay->addWidget(new QLabel(QStringLiteral("路径:"), file_group));
    file_lay->addWidget(m_edt_file_path, 1);
    file_lay->addWidget(m_btn_browse);
    root->addWidget(file_group);

    auto* opt_group = new QGroupBox(QStringLiteral("其他"), this);
    auto* opt_lay = new QHBoxLayout(opt_group);
    m_chk_time_tag = new QCheckBox(QStringLiteral("带时间标签(has_time_tag=1,BCD 8B)"), opt_group);
    opt_lay->addWidget(m_chk_time_tag);
    root->addWidget(opt_group);

    auto* btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btn_box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始捕获"));
    btn_box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(btn_box);

    connect(m_btn_browse, &QPushButton::clicked, this, &CommConfigDialog::on_browse_file);
    connect(m_cmb_source_type,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, &CommConfigDialog::on_source_type_changed);
    connect(btn_box, &QDialogButtonBox::accepted, this, &CommConfigDialog::on_accept);
    connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    on_source_type_changed();
}

void CommConfigDialog::load_initial(const ReaderConfig& c) {
    int idx = m_cmb_source_type->findData(int(c.mode));
    if (idx >= 0) m_cmb_source_type->setCurrentIndex(idx);

    if (!c.serial_name.isEmpty()) {
        int pidx = m_cmb_port_name->findData(c.serial_name);
        if (pidx >= 0) m_cmb_port_name->setCurrentIndex(pidx);
        else m_cmb_port_name->setEditText(c.serial_name);
    }
    m_cmb_baud_rate->setEditText(QString::number(c.baud_rate));
    m_cmb_data_bits->setCurrentText("8");
    m_cmb_stop_bits->setCurrentText("1");
    m_cmb_parity->setCurrentIndex(0);

    m_edt_file_path->setText(c.file_path);

    m_chk_time_tag->setChecked(c.has_time_tag);

    on_source_type_changed();
}

void CommConfigDialog::on_source_type_changed() {
    int mode = m_cmb_source_type->currentData().toInt();
    bool is_serial = (mode == int(ReaderMode::SerialPort));
    bool is_file   = (mode != int(ReaderMode::SerialPort));

    QGroupBox* port_group = qobject_cast<QGroupBox*>(m_cmb_port_name->parentWidget()->parentWidget());
    if (port_group) port_group->setEnabled(is_serial);

    QGroupBox* file_group = qobject_cast<QGroupBox*>(m_edt_file_path->parentWidget()->parentWidget());
    if (file_group) file_group->setEnabled(is_file);
}

void CommConfigDialog::on_browse_file() {
    int mode = m_cmb_source_type->currentData().toInt();
    QString filter;
    if (mode == int(ReaderMode::FilePlayback))
        filter = QStringLiteral("二进制文件 (*.bin);;所有 (*.*)");
    else
        filter = QStringLiteral("Hex 文本 (*.txt *.hex);;所有 (*.*)");
    QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"), "", filter);
    if (!f.isEmpty()) m_edt_file_path->setText(f);
}

void CommConfigDialog::on_accept() {
    int mode = m_cmb_source_type->currentData().toInt();
    m_cfg.mode = ReaderMode(mode);

    if (mode == int(ReaderMode::SerialPort)) {
        m_cfg.serial_name = m_cmb_port_name->currentData().toString();
        if (m_cfg.serial_name.isEmpty())
            m_cfg.serial_name = m_cmb_port_name->currentText().section(' ', 0, 0);

        m_cfg.baud_rate = m_cmb_baud_rate->currentText().toInt();
    } else {
        m_cfg.file_path = m_edt_file_path->text().trimmed();
        if (m_cfg.file_path.isEmpty()) {
            m_edt_file_path->setFocus();
            return;
        }
    }
    m_cfg.has_time_tag = m_chk_time_tag->isChecked();
    accept();
}
