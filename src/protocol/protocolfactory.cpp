/// @file protocolfactory.cpp
/// @brief 协议解析器工厂 + 字段树构建器工厂实现
#include "protocolfactory.h"
#include "protocol_tree_builder.h"
#include "gw_protocol/gw_2022/gw_2022_parser.h"
#include "nw_protocol/nw_2021/nw_2021_parser.h"
#include "gw_protocol/gw_2022/gw_2022_tree.h"
#include "nw_protocol/nw_2021/nw_2021_tree.h"

std::unique_ptr<IProtocolParser> make_parser(ProtocolVariant v) {
    switch (v) {
        case ProtocolVariant::GW_2022:
            return std::make_unique<GW_2022_Parser>();
        case ProtocolVariant::NW_2021:
            return std::make_unique<NW_2021_Parser>();
    }
    return nullptr;
}

std::unique_ptr<IProtocolTreeBuilder> make_tree_builder(ProtocolVariant v) {
    switch (v) {
        case ProtocolVariant::GW_2022:
            return std::make_unique<GW_2022_TreeBuilder>();
        case ProtocolVariant::NW_2021:
            return std::make_unique<NW_2021_TreeBuilder>();
    }
    return nullptr;
}
