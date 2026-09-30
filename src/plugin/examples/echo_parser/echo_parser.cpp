/// @file echo_parser.cpp
/// @brief 示例插件:ECHO_2024 协议解析器(验证插件全链路)
/// @details 极简实现:接受所有帧,返回基础 ParseResult(含演示字段树)。
///          用于验证:plugin.json → plugin-host 加载 → IPC → 主程序显示。
#include <QObject>

#include "iprotocolparserplugin.h"

/// @brief ECHO 解析器:把帧数据原样回显为字段树
class EchoParser : public IProtocolParser {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }

    ParseResult parse(const BplcFrame& in, MsduState& msdu,
                      const ParseFilter& f) override {
        Q_UNUSED(msdu); Q_UNUSED(f);
        ParseResult r;
        r.meta = in.meta;
        r.raw_wire = in.raw_wire;
        r.arrival_us = in.arrival_us;
        r.payload_for_log = in.data;
        r.accept = true;

        // MPDU 基础信息
        r.mpdu.frame_type = 1;  // SOF
        if (!in.data.isEmpty())
            r.mpdu.frame_len = (quint16)in.data.size();

        // MSDU 演示字段树
        r.msdu.present = true;
        r.msdu.summary = QStringLiteral("ECHO Plugin Frame");
        MsduFieldNode root;
        root.name = QStringLiteral("ECHO_2024");
        root.value = QStringLiteral("%1 bytes").arg(in.data.size());
        root.rel_start = 0; root.rel_len = in.data.size();
        MsduFieldNode len_node;
        len_node.name = QStringLiteral("Length");
        len_node.value = QString::number(in.data.size());
        root.children.append(len_node);
        if (!in.data.isEmpty()) {
            MsduFieldNode first;
            first.name = QStringLiteral("FirstByte");
            first.value = QStringLiteral("0x%1")
                .arg((quint8)in.data[0], 2, 16, QChar('0')).toUpper();
            first.rel_start = 0; first.rel_len = 1;
            root.children.append(first);
        }
        r.msdu.tree.append(root);
        return r;
    }
};

/// @brief ECHO 插件
class EchoParserPlugin : public QObject, public IProtocolParserPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID BPLC_PARSER_PLUGIN_IID)
    Q_INTERFACES(IPlugin IProtocolParserPlugin)
public:
    QString id() const override { return QStringLiteral("echo-parser"); }
    QString display_name() const override { return QStringLiteral("ECHO 示例插件"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QString description() const override {
        return QStringLiteral("示例协议解析器插件,验证插件系统全链路");
    }
    QString author() const override { return QStringLiteral("BPLC Team"); }
    QString protocol_id() const override { return QStringLiteral("ECHO_2024"); }

    bool initialize() override { return true; }
    void shutdown() override {}

    IProtocolParser* create_parser() override { return new EchoParser(); }
};

#include "echo_parser.moc"
