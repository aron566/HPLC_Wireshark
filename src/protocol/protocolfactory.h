/// @file protocolfactory.h
/// @brief 协议解析器工厂:按 variant/协议 id 实例化对应 IProtocolParser 实现
/// @details 用户选择协议 → config.ini general/protocol → 协议 id 字符串 →
///          make_parser_by_id 返回对应实现(内置国网/南网,或插件协议)。
///          内置协议在启动时自注册;插件协议由 PluginManager 在握手成功后注册。
///          老的 make_parser(ProtocolVariant) 保留兼容,内部走注册表。
#ifndef PROTOCOLFACTORY_H
#define PROTOCOLFACTORY_H

#include "iprotocolparser.h"
#include <functional>
#include <memory>

/// @brief 解析器工厂函数
using ParserFactoryFn = std::function<std::unique_ptr<IProtocolParser>()>;

/// @brief 注册协议解析器(协议 id 如 "GW_2022"/"MYPROTO_2024")
/// @note 重复注册时后者覆盖前者(display_name 一并覆盖)
/// @param display_name 下拉框显示名(中文原文,显示时经 trl::L 翻译);空则用 id
void register_parser(const QString& protocol_id, ParserFactoryFn fn,
                     const QString& display_name = QString());
/// @brief 重新注册内置协议(GW_2022 / NW_2021)
/// @note 插件注销了与内置同名的协议 id 时调用,恢复内置解析器兜底。
///       幂等:重复调用仅覆盖同名条目。
void register_builtin_parsers();

/// @brief 注销协议解析器
void unregister_parser(const QString& protocol_id);
/// @brief 已注册的协议 id 列表(内置 + 插件)
QStringList registered_protocol_ids();
/// @brief 协议显示名(下拉框用);未注册显示名时返回 id
QString protocol_display_name(const QString& protocol_id);

/// @brief 按协议 id 实例化解析器。返回 nullptr = 未知协议。
std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id);

/// @brief 协议 id → ProtocolVariant(字段树构建器用)
/// @note "NW_2021"(大小写不敏感) → NW_2021;其余(含插件新协议)→ GW_2022
inline ProtocolVariant protocol_variant_for_id(const QString& id) {
    return (id.compare(QLatin1String("NW_2021"), Qt::CaseInsensitive) == 0)
               ? ProtocolVariant::NW_2021
               : ProtocolVariant::GW_2022;
}

/// @brief config.ini 协议键 → ProtocolVariant("gw_2022"/"nw_2021")
/// @note 仅用于内置协议;插件协议直接用 id 字符串,不经过此函数
inline ProtocolVariant protocol_from_key(const QString& key) {
    return (key == QLatin1String("nw_2021")) ? ProtocolVariant::NW_2021
                                              : ProtocolVariant::GW_2022;
}

/// @brief 按协议变体实例化解析器(兼容老接口,内部走注册表)
inline std::unique_ptr<IProtocolParser> make_parser(ProtocolVariant v) {
    return make_parser_by_id(
        v == ProtocolVariant::NW_2021 ? QStringLiteral("NW_2021")
                                     : QStringLiteral("GW_2022"));
}

#endif // PROTOCOLFACTORY_H
