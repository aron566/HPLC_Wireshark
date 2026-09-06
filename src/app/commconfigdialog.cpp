/// @file commconfigdialog.cpp
/// @brief 通讯口配置对话框实现
#include "commconfigdialog.h"
#include "i18n.h"
#include "appconfig.h"
#include "theme.h"

#include <QComboBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QLocale>
#include <QGroupBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSerialPort>

CommConfigDialog::CommConfigDialog(QWidget* parent, const ReaderConfig& initial)
    : QDialog(parent),
      m_cmb_source_type(nullptr), m_cmb_port_name(nullptr),
      m_cmb_baud_rate(nullptr), m_cmb_data_bits(nullptr),
      m_cmb_stop_bits(nullptr), m_cmb_parity(nullptr),
      m_edt_file_path(nullptr), m_btn_browse(nullptr),
      m_chk_time_tag(nullptr) {
    setWindowTitle(trl::L("通讯口设置"));
    setMinimumWidth(420);
    build_ui();
    load_initial(initial);
}

void CommConfigDialog::build_ui() {
    auto* root = new QVBoxLayout(this);
    // 显式内容边距:主题 QSS 下各 style 默认布局边距不一致(深色会额外缩进
    // ~12px),统一后两主题显示对齐
    root->setContentsMargins(11, 11, 11, 11);
    root->setSpacing(8);

    auto* src_group = new QGroupBox(trl::L("数据源类型"), this);
    auto* src_lay = new QHBoxLayout(src_group);
    m_cmb_source_type = new QComboBox(src_group);
    m_cmb_source_type->addItem(trl::L("实时串口"),       int(ReaderMode::SerialPort));
    m_cmb_source_type->addItem(trl::L("文件回放 (.bin)"), int(ReaderMode::FilePlayback));
    m_cmb_source_type->addItem(trl::L("裸 hex 文本"),    int(ReaderMode::RawHex));
    src_lay->addWidget(new QLabel(trl::L("类型:"), src_group));
    src_lay->addWidget(m_cmb_source_type, 1);
    root->addWidget(src_group);

    auto* port_group = new QGroupBox(trl::L("串口参数"), this);
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
    m_cmb_data_bits->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cmb_data_bits->setMinimumContentsLength(3);

    m_cmb_stop_bits = new QComboBox(port_group);
    m_cmb_stop_bits->addItems({"1", "1.5", "2"});
    m_cmb_stop_bits->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cmb_stop_bits->setMinimumContentsLength(3);

    m_cmb_parity = new QComboBox(port_group);
    m_cmb_parity->addItem(trl::L("无"),     int(QSerialPort::NoParity));
    m_cmb_parity->addItem(trl::L("奇"),     int(QSerialPort::OddParity));
    m_cmb_parity->addItem(trl::L("偶"),     int(QSerialPort::EvenParity));
    m_cmb_parity->addItem(trl::L("标记"),   int(QSerialPort::MarkParity));
    m_cmb_parity->addItem(trl::L("空"),     int(QSerialPort::SpaceParity));
    m_cmb_parity->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_cmb_parity->setMinimumContentsLength(4);

    port_form->addRow(trl::L("串口:"),   m_cmb_port_name);
    port_form->addRow(trl::L("波特率:"), m_cmb_baud_rate);
    port_form->addRow(trl::L("数据位:"), m_cmb_data_bits);
    port_form->addRow(trl::L("停止位:"), m_cmb_stop_bits);
    port_form->addRow(trl::L("校验位:"), m_cmb_parity);

    root->addWidget(port_group);

    auto* file_group = new QGroupBox(trl::L("文件参数"), this);
    auto* file_lay = new QHBoxLayout(file_group);
    m_edt_file_path = new QLineEdit(file_group);
    m_btn_browse = new QPushButton(trl::L("浏览..."), file_group);
    file_lay->addWidget(new QLabel(trl::L("路径:"), file_group));
    file_lay->addWidget(m_edt_file_path, 1);
    file_lay->addWidget(m_btn_browse);
    root->addWidget(file_group);

    auto* opt_group = new QGroupBox(trl::L("其他"), this);
    auto* opt_lay = new QVBoxLayout(opt_group);
    m_chk_time_tag = new QCheckBox(trl::L("带时间标签(has_time_tag=1,BCD 8B)"), opt_group);
    opt_lay->addWidget(m_chk_time_tag);

    // 语言/Language:auto=跟随系统 / zh=中文 / en=English(config.ini general/lang)
    auto* lang_row = new QHBoxLayout;
    lang_row->addWidget(new QLabel(trl::L("语言/Language:"), opt_group));
    auto* cmb_lang = new QComboBox(opt_group);
    cmb_lang->addItem(trl::L("跟随系统"), QStringLiteral("auto"));
    cmb_lang->addItem(trl::L("中文"),     QStringLiteral("zh"));
    cmb_lang->addItem(QStringLiteral("English"), QStringLiteral("en"));
    cmb_lang->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cmb_lang->setMinimumContentsLength(10);
    lang_row->addWidget(cmb_lang);
    lang_row->addStretch(1);
    opt_lay->addLayout(lang_row);

    // 主题/Theme:dark=深色(QDarkStyleSheet)/ light=浅色(config.ini general/theme)
    auto* theme_row = new QHBoxLayout;
    theme_row->addWidget(new QLabel(trl::L("主题/Theme:"), opt_group));
    auto* cmb_theme = new QComboBox(opt_group);
    cmb_theme->addItem(trl::L("深色"), QStringLiteral("dark"));
    cmb_theme->addItem(trl::L("浅色"), QStringLiteral("light"));
    cmb_theme->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cmb_theme->setMinimumContentsLength(6);
    theme_row->addWidget(cmb_theme);
    theme_row->addStretch(1);
    opt_lay->addLayout(theme_row);
    root->addWidget(opt_group);

    // 恢复已保存主题并放在 connect 之前;变更即保存并即时全局应用
    const QString saved_theme = appcfg::theme();
    const int theme_idx = cmb_theme->findData(saved_theme);
    if (theme_idx >= 0) cmb_theme->setCurrentIndex(theme_idx);
    connect(cmb_theme, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [cmb_theme](int) {
        const QString v = cmb_theme->currentData().toString();
        appcfg::set_theme(v);
        theme::apply(v);        // 样式表全局应用,即时生效
    });

    // 恢复已保存语言并放在 connect 之前(避免打开对话框即弹提示);
    // 变更即保存并立即 trl::set_enabled,窗口 chrome 重启后完全生效
    const QString saved_lang = appcfg::lang();
    const int lang_idx = cmb_lang->findData(saved_lang);
    if (lang_idx >= 0) cmb_lang->setCurrentIndex(lang_idx);
    connect(cmb_lang, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, cmb_lang](int) {
        const QString v = cmb_lang->currentData().toString();
        appcfg::set_lang(v);
        bool en = false;
        if (v == QLatin1String("en")) {
            en = true;
        } else if (v == QLatin1String("zh")) {
            en = false;
        } else {  // auto:跟随系统(系统为中文 → 中文,否则英文)
            en = QLocale::system().language() != QLocale::Chinese;
        }
        trl::set_enabled(en);
        QMessageBox::information(this, trl::L("语言"),
                                 trl::L("重启后界面语言完全生效"));
    });

    auto* btn_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btn_box->button(QDialogButtonBox::Ok)->setText(trl::L("开始捕获"));
    btn_box->button(QDialogButtonBox::Cancel)->setText(trl::L("取消"));
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
        filter = trl::L("二进制文件 (*.bin);;所有 (*.*)");
    else
        filter = trl::L("Hex 文本 (*.txt *.hex);;所有 (*.*)");
    QString f = QFileDialog::getOpenFileName(this, trl::L("选择文件"), "", filter);
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
namespace {
// 中→英注册(文件级:通讯口设置对话框 + 语言选择)
struct I18nRegCommConfig {
    I18nRegCommConfig() {
        trl::register_en("通讯口设置", "Serial Port Settings");
        trl::register_en("数据源类型", "Data Source Type");
        trl::register_en("实时串口", "Live Serial Port");
        trl::register_en("文件回放 (.bin)", "File Replay (.bin)");
        trl::register_en("裸 hex 文本", "Raw Hex Text");
        trl::register_en("类型:", "Type:");
        trl::register_en("串口参数", "Serial Port Parameters");
        trl::register_en("串口:", "Serial Port:");
        trl::register_en("波特率:", "Baud Rate:");
        trl::register_en("数据位:", "Data Bits:");
        trl::register_en("停止位:", "Stop Bits:");
        trl::register_en("校验位:", "Parity:");
        trl::register_en("无", "None");
        trl::register_en("奇", "Odd");
        trl::register_en("偶", "Even");
        trl::register_en("标记", "Mark");
        trl::register_en("空", "Space");
        trl::register_en("文件参数", "File Parameters");
        trl::register_en("浏览...", "Browse...");
        trl::register_en("路径:", "Path:");
        trl::register_en("其他", "Other");
        trl::register_en("带时间标签(has_time_tag=1,BCD 8B)",
                         "With time tag (has_time_tag=1,BCD 8B)");
        trl::register_en("开始捕获", "Start Capture");
        trl::register_en("取消", "Cancel");
        trl::register_en("二进制文件 (*.bin);;所有 (*.*)",
                         "Binary files (*.bin);;All files (*.*)");
        trl::register_en("Hex 文本 (*.txt *.hex);;所有 (*.*)",
                         "Hex text (*.txt *.hex);;All files (*.*)");
        trl::register_en("选择文件", "Select File");
        trl::register_en("语言/Language:", "Language:");
        trl::register_en("跟随系统", "Follow system");
        trl::register_en("中文", "Chinese");
        trl::register_en("语言", "Language");
        trl::register_en("重启后界面语言完全生效",
                         "UI language fully applies after restart");
    }
};
const I18nRegCommConfig g_i18n_reg_commconfig;
}  // namespace
