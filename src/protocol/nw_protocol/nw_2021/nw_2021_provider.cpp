/// @file nw_2021_provider.cpp
/// @brief NW_2021 协议提供者:自注册 id / 显示名 / 排序 / 解析器与字段树构建器
/// @details BPLC 载波 SOF 等旧版布局只在 nw_2021_tree.cpp 一处实现;
///          NW_2026 解析器遇到 BPLC 版本帧会整帧委托本解析器。
#include "iprotocolprovider.h"
#include "protocolfactory.h"

#include "nw_2021_parser.h"
#include "nw_2021_tree.h"

namespace {

class Nw2021Provider : public IProtocolProvider {
public:
    QString id() const override { return QStringLiteral("NW_2021"); }
    QString display_name_zh() const override { return QStringLiteral("南网 NW_2021"); }
    QString display_name_en() const override { return QStringLiteral("CSG NW_2021"); }
    /// 排序:南网 2021 为旧版,排在新一代之后(20 给 2026)
    int order() const override { return 40; }
    ProtocolVariant variant() const override { return ProtocolVariant::NW_2021; }

    std::unique_ptr<IProtocolParser> create_parser() const override {
        return std::make_unique<NW_2021_Parser>();
    }
    std::unique_ptr<IProtocolTreeBuilder> create_tree_builder() const override {
        return std::make_unique<NW_2021_TreeBuilder>();
    }
};

struct Nw2021Registrar {
    Nw2021Registrar() { register_protocol(std::make_shared<Nw2021Provider>()); }
};
Nw2021Registrar g_nw2021_registrar;

}  // namespace
