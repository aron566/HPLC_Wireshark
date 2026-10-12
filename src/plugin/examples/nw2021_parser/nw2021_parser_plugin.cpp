/// @file nw2021_parser_plugin.cpp
/// @brief 南网 NW_2021 协议解析器插件(native)
/// @details 复用主程序内 NW_2021_Parser(南网双模 2021 报批版解析器),
///          将其编译进插件 dll,经 IPC 由 bplc-plugin-host 加载,主程序通过
///          PluginManager 注册协议 "NW_2021"(覆盖内置同名注册,插件即南网
///          解析器;插件卸载后 register_builtin_parsers 恢复内置兜底)。
///          与内置 NW_2021 的区别:内置直接进程内解析;本插件跨进程解析,
///          返回完全相同的 ParseResult(字段树 msdu.tree),字段树渲染一致。
#include <QObject>

#include "iprotocolparserplugin.h"
#include "nw_2021_parser.h"

/// @brief 南网 NW_2021 插件:包装内置 NW_2021_Parser
class Nw2021ParserPlugin : public QObject, public IProtocolParserPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID BPLC_PARSER_PLUGIN_IID)
    Q_INTERFACES(IPlugin IProtocolParserPlugin)
public:
    QString id() const override { return QStringLiteral("nw2021-parser"); }
    QString display_name() const override {
        return QStringLiteral("南网 NW_2021 协议解析器");
    }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QString description() const override {
        return QStringLiteral("南网双模 2021 报批版协议解析器(插件实现,"
                             "与内置 NW_2021 字段树一致)");
    }
    QString author() const override { return QStringLiteral("BPLC Team"); }
    QString protocol_id() const override { return QStringLiteral("NW_2021"); }

    bool initialize() override { return true; }
    void shutdown() override {}

    IProtocolParser* create_parser() override { return new NW_2021_Parser(); }
};

#include "nw2021_parser_plugin.moc"
