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

/// @brief 是否为内置协议 id（插件不可影射/注销，见 register/unregister）
bool is_builtin_protocol(const QString& protocol_id);
/// @brief 注册协议解析器(协议 id 如 "GW_2022"/"MYPROTO_2024")
/// @note 重复注册时后者覆盖前者;但 id 与内置协议冲突时拒绝注册并
///       返回 false（内置条目不可被插件覆盖）。
bool register_parser(const QString& protocol_id, ParserFactoryFn fn);
/// @brief 注销协议解析器
void unregister_parser(const QString& protocol_id);
/// @brief 已注册的协议 id 列表(内置 + 插件)
QStringList registered_protocol_ids();

/// @brief 按协议 id 实例化解析器。返回 nullptr = 未知协议。
std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id);

/// @brief config.ini 协议键 → ProtocolVariant("gw_2022"/"nw_2021")
/// @note 前缀匹配:插件协议 id 以内置 id 为前缀派生(如 nw_2021_plugin)
///       时也映射到同源变体;通用渲染路径实际不依赖变体,此仅为兜底
///       builder 选型不至于跨协议。
inline ProtocolVariant protocol_from_key(const QString& key) {
    return key.toLower().startsWith(QLatin1String("nw_2021"))
               ? ProtocolVariant::NW_2021
               : ProtocolVariant::GW_2022;
}

/// @brief 按协议变体实例化解析器(兼容老接口,内部走注册表)
inline std::unique_ptr<IProtocolParser> make_parser(ProtocolVariant v) {
    return make_parser_by_id(
        v == ProtocolVariant::NW_2021 ? QStringLiteral("NW_2021")
                                     : QStringLiteral("GW_2022"));
}

#endif // PROTOCOLFACTORY_H
