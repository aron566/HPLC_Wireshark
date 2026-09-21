/// @file protocolfactory.cpp
/// @brief 协议解析器工厂实现
#include "protocolfactory.h"
#include "gw_protocol/gw_2022/gw_2022_parser.h"
// #include "nw_protocol/nw_2021/nw_2021_parser.h"  // 南网实现后启用

std::unique_ptr<IProtocolParser> make_parser(ProtocolVariant v) {
    switch (v) {
        case ProtocolVariant::GW_2022:
            return std::make_unique<GW_2022_Parser>();
        case ProtocolVariant::NW_2021:
            // TODO: 南网 NW_2021 实现后替换为 std::make_unique<NW_2021_Parser>()
            // 暂降级到国网实现,保证南网选项不崩溃(仅链路层字段暂不适用)
            return std::make_unique<GW_2022_Parser>();
    }
    return nullptr;
}
