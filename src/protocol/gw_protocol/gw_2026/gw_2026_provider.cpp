/// @file gw_2026_provider.cpp
/// @brief GW_2026 协议提供者:自注册 id / 显示名 / 排序 / 解析器与字段树构建器
/// @details GW_2026_Parser 内部按 FCH 版本号自适应:HDC 1.0 帧整帧委托
///          GW_2022_Parser,结果 protocol 标记为 GW_2022,字段树随之分派。
#include "iprotocolprovider.h"
#include "protocolfactory.h"

#include "gw_2026_parser.h"
#include "gw_2026_tree.h"

namespace {

class Gw2026Provider : public IProtocolProvider {
public:
    QString id() const override { return QStringLiteral("GW_2026"); }
    QString display_name_zh() const override { return QStringLiteral("国网 GW_2026"); }
    QString display_name_en() const override { return QStringLiteral("State Grid GW_2026"); }
    /// 排序:国网新一代排最前
    int order() const override { return 10; }
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2026; }

    std::unique_ptr<IProtocolParser> create_parser() const override {
        return std::make_unique<GW_2026_Parser>();
    }
    std::unique_ptr<IProtocolTreeBuilder> create_tree_builder() const override {
        return std::make_unique<GW_2026_TreeBuilder>();
    }
};

struct Gw2026Registrar {
    Gw2026Registrar() { register_protocol(std::make_shared<Gw2026Provider>()); }
};
Gw2026Registrar g_gw2026_registrar;

}  // namespace
