/// @file protocolfactory.h
/// @brief 协议注册表:分层登记(内置层 + 插件覆盖层)与实例化
/// @details 两层(见 protocolfactory.cpp):
///          - 内置层(builtin_registry):四内置协议(GW_2022/NW_2021/GW_2026/NW_2026)
///            静态自注册,只读——写内置层拒绝覆盖已登记的内置 id(A-7 守卫);
///          - 覆盖层(override_registry):插件协议,带归属(owner_plugin_id)与注册
///            引用计数;影子覆盖内置同名 id 是经批准的路径(A1),注册时告警(A4)。
///          分流规则:register_protocol 按 provider->owner_plugin_id() 是否为空分流
///          (空写内置层、非空写覆盖层);register_parser / register_parser_provider
///          一律写覆盖层,绝不触碰内置层。A-7 与 A1 不矛盾:A-7 守的是内置层,
///          覆盖层是经批准的影子覆盖路径(带归属 + 告警)。
///          make_parser_by_id 覆盖层优先、其次内置层;unregister_* 只删覆盖层。
///          同一归属重复注册引用计数 +1;不同归属抢占已被影子的 id 直接拒绝
///          (后注册拒绝,先到先得);归属注销时引用计数 -1,到 0 才摘除条目。
///          引用计数计的是「注册引用」(握手注册/注销配对),不是在途解析器实例数:
///          解析器实例由调用方持有,注销不等待在途解析。
///            - make_parser_by_id   :按协议 id 取解析器(插件经 IPC 代理)
///            - make_tree_builder   :按变体取字段树构建器(声明在 protocol_tree_builder.h;
///                                    内置优先于借用同变体的插件协议)
///            - registered_protocols:界面枚举协议(内置在前,同名 id 去重,影子取生效条目)
#ifndef BPLC_PROTOCOLFACTORY_H
#define BPLC_PROTOCOLFACTORY_H

#include "iprotocolparser.h"
#include "iprotocolprovider.h"

#include <QVector>
#include <functional>
#include <memory>

/// @brief 解析器工厂函数(只需提供解析器的场景,如插件)
using ParserFactoryFn = std::function<std::unique_ptr<IProtocolParser>()>;

/// @brief 协议提供者指针(注册表快照;可安全跨调用持有,注销后对象仍有效)
using ProtocolProviderPtr = std::shared_ptr<IProtocolProvider>;

/// @brief 注册协议,按归属分流(内置写内置层、插件写覆盖层)
/// @details 内置层已登记同名 id 时拒绝(返回 false,不覆盖);覆盖层已被另一归属
///          影子时拒绝(后注册拒绝,先到先得);同一归属重复注册引用计数 +1。
/// @return true = 已登记(或引用计数 +1);false = 被拒绝(内置已存在/归属冲突)
bool register_protocol(ProtocolProviderPtr provider);
/// @brief 注册协议到覆盖层(插件路径,显式指定归属)
/// @details 归属为空时仍写覆盖层(仅 register_protocol 按归属分流)。
///          影子覆盖内置同名 id 时告警(带归属插件 id,见 A4)。
bool register_override_protocol(ProtocolProviderPtr provider,
                                const QString& owner_plugin_id);

/// @brief 注销协议(插件卸载/停机清理时调用)
/// @details 只删覆盖层,内置层永远不受影响;忽略引用计数(停机清理语义)。
/// @return true = 摘除了覆盖层条目;false = 覆盖层无此 id
bool unregister_protocol(const QString& protocol_id);
/// @brief 按归属注销协议(单个插件的卸载/禁用路径)
/// @details 归属不符时拒绝(返回 false,条目不动);归属相符时引用计数 -1,
///          到 0 才摘除条目。
/// @return true = 引用计数 -1(或已摘除);false = 无条目/归属不符
bool unregister_protocol_for_owner(const QString& protocol_id,
                                   const QString& owner_plugin_id);

/// @brief 是否内置协议 id(四内置变体,与注册表内容无关的静态名单)
bool is_builtin_protocol(const QString& protocol_id);
/// @brief 覆盖层条目的归属插件 id;无覆盖条目(内置或未注册)返回空
QString protocol_owner_plugin_id(const QString& protocol_id);
/// @brief 覆盖层条目的注册引用计数;无覆盖条目返回 0(诊断用)
int override_ref_count(const QString& protocol_id);

/// @brief 已注册协议快照(内置在前,同名 id 去重,影子取覆盖层生效条目)
QVector<ProtocolProviderPtr> registered_protocols();
/// @brief 按 id 查协议(大小写不敏感,覆盖层优先);未注册返回空
ProtocolProviderPtr find_protocol(const QString& protocol_id);
/// @brief 已注册协议 id 列表(大写,内置在前,同名 id 去重)
QStringList registered_protocol_ids();

/// @brief 按协议 id 实例化解析器。返回 nullptr = 未知协议。
std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id);

// ── 插件便捷接口:只提供解析器(插件侧不构造 QTreeWidget,故无字段树构建器) ──
/// @brief 注册"仅解析器"的协议;display_zh/en 用于界面协议列表
/// @details 一律写覆盖层(插件路径);归属缺省为空(兼容旧调用,新调用请传插件 id)。
bool register_parser_provider(const QString& protocol_id,
                              const QString& display_zh,
                              const QString& display_en,
                              ParserFactoryFn fn,
                              int order = 200,
                              const QString& owner_plugin_id = QString());
/// @brief 兼容旧接口:显示名回退为协议 id;一律写覆盖层
/// @details 内置同名 id 在此不拒绝(A-7 只守内置层):影子覆盖是经批准的路径,
///          只告警(带归属,归属为空时记 <unknown>)。
bool register_parser(const QString& protocol_id, ParserFactoryFn fn);
/// @brief 兼容旧接口:同 unregister_protocol(只删覆盖层,停机清理语义)
bool unregister_parser(const QString& protocol_id);
/// @brief 兼容旧接口:同 unregister_protocol_for_owner(按归属注销)
bool unregister_parser_for_owner(const QString& protocol_id,
                                 const QString& owner_plugin_id);

#endif  // BPLC_PROTOCOLFACTORY_H
