/// @file protocolfactory.h
/// @brief 协议解析器工厂:按 variant 实例化对应 IProtocolParser 实现
/// @details 用户选择协议 → config.ini general/protocol → ProtocolVariant →
///          make_parser 返回对应实现(国网 GW_2022 / 南网 NW_2021)。
///          协议升级 = 新增 *_YYYY 实现 + 工厂加一个 case,老代码零改动。
#ifndef PROTOCOLFACTORY_H
#define PROTOCOLFACTORY_H

#include "iprotocolparser.h"
#include <memory>

/// @brief config.ini 协议键 → ProtocolVariant("gw_2022"/"nw_2021")
inline ProtocolVariant protocol_from_key(const QString& key) {
    return (key == QLatin1String("nw_2021")) ? ProtocolVariant::NW_2021
                                              : ProtocolVariant::GW_2022;
}

/// @brief 按协议变体实例化解析器。返回 nullptr = 未知变体。
std::unique_ptr<IProtocolParser> make_parser(ProtocolVariant v);

#endif // PROTOCOLFACTORY_H
