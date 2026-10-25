/// @file gw_2022_provider.cpp
/// @brief GW_2022 协议提供者:自注册 id / 显示名 / 排序 / 解析器与字段树构建器
/// @details 注册表只认 IProtocolProvider;本文件是 GW_2022 对外的唯一入口描述,
///          界面协议列表、make_parser_by_id、make_tree_builder 全部经此生效。
#include "iprotocolprovider.h"
#include "protocolfactory.h"

#include "gw_2022_parser.h"
#include "gw_2022_tree.h"

namespace {

class Gw2022Provider : public IProtocolProvider {
public:
    QString id() const override { return QStringLiteral("GW_2022"); }
    QString display_name_zh() const override { return QStringLiteral("国网 GW_2022"); }
    QString display_name_en() const override { return QStringLiteral("State Grid GW_2022"); }
    /// 排序:国网 2022 为旧版,排在新一代之后(10/20 给 2026)
    int order() const override { return 30; }
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }

    std::unique_ptr<IProtocolParser> create_parser() const override {
        return std::make_unique<GW_2022_Parser>();
    }
    std::unique_ptr<IProtocolTreeBuilder> create_tree_builder() const override {
        return std::make_unique<GW_2022_TreeBuilder>();
    }
};

/// 静态自注册:本 .cpp 编进链接即生效(由 gw_2022.pri 列出)
struct Gw2022Registrar {
    Gw2022Registrar() { register_protocol(std::make_shared<Gw2022Provider>()); }
};
Gw2022Registrar g_gw2022_registrar;

}  // namespace
