/// @file nw_2026_provider.cpp
/// @brief NW_2026 协议提供者:自注册 id / 显示名 / 排序 / 解析器与字段树构建器
/// @details NW_2026_Parser 内部按标准版本号自适应:BPLC 版本帧整帧委托
///          NW_2021_Parser,结果 protocol 标记为 NW_2021,字段树随之分派。
#include "iprotocolprovider.h"
#include "protocolfactory.h"

#include "nw_2026_parser.h"
#include "nw_2026_tree.h"

namespace {

class Nw2026Provider : public IProtocolProvider {
public:
    QString id() const override { return QStringLiteral("NW_2026"); }
    QString display_name_zh() const override { return QStringLiteral("南网 NW_2026"); }
    QString display_name_en() const override { return QStringLiteral("CSG NW_2026"); }
    /// 排序:南网新一代排第二(仅次于国网新一代)
    int order() const override { return 20; }
    ProtocolVariant variant() const override { return ProtocolVariant::NW_2026; }

    std::unique_ptr<IProtocolParser> create_parser() const override {
        return std::make_unique<NW_2026_Parser>();
    }
    std::unique_ptr<IProtocolTreeBuilder> create_tree_builder() const override {
        return std::make_unique<NW_2026_TreeBuilder>();
    }
};

struct Nw2026Registrar {
    Nw2026Registrar() { register_protocol(std::make_shared<Nw2026Provider>()); }
};
Nw2026Registrar g_nw2026_registrar;

}  // namespace
