/// @file protocolfactory.cpp
/// @brief 协议解析器工厂实现
#include "protocolfactory.h"
#include "gw_protocol/gw_2022/gw_2022_parser.h"
#include "nw_protocol/nw_2021/nw_2021_parser.h"

std::unique_ptr<IProtocolParser> make_parser(ProtocolVariant v) {
    switch (v) {
        case ProtocolVariant::GW_2022:
            return std::make_unique<GW_2022_Parser>();
        case ProtocolVariant::NW_2021:
            return std::make_unique<NW_2021_Parser>();
    }
    return nullptr;
}
