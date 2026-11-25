/// @file protocolfactory.h
/// @brief 协议注册表:分层登记(内置层 + 插件外置层)与实例化
/// @details 两层(见 protocolfactory.cpp):
///          - 内置层(builtin_registry):四内置协议(GW_2022/NW_2021/GW_2026/NW_2026)
///            静态自注册,只读——写内置层拒绝覆盖已登记的内置 id(A-7 守卫);
///          - 覆盖层(override_registry):插件协议,带归属(owner_plugin_id)与注册
///            引用计数;条目以 (协议 id + 归属) 区分。
///          no-shadow(用户 2026-11-20 定):插件不可以覆盖内置解析器——覆盖层
///          只许注册新 id,注册内置 id 直接拒绝并告警(A4);影子覆盖路径已去掉,
///          A1 的"覆盖优先"语义随之作废,按 id 查找恒为内置优先,故
///          make_parser_by_id("NW_2021") 取到的恒为内置解析器。
///          外置解析器(Ext):插件协议与内置同名 id 允许共存,但只能经外置路径
///          (register_external_protocol,握手时注册)登记;Ext 条目永不参与按 id
///          的查找,只经按归属查找取用;下拉框里与内置并列显示,带 " (Ext)" 标记。
///          分流规则:register_protocol 按 provider->owner_plugin_id() 是否为空分流
///          (空写内置层、非空写覆盖层);register_parser / register_parser_provider
///          一律写覆盖层(影子路径,内置 id 拒绝),绝不触碰内置层。A-7 与 no-shadow
///          不矛盾:A-7 守的是内置层,覆盖层是插件协议的登记处(新 id 或 Ext 共存)。
///          make_parser_by_id 内置优先、其次覆盖层;unregister_* 只删覆盖层。
///          同一归属重复注册引用计数 +1;不同归属抢占已被登记的 id 直接拒绝
///          (后注册拒绝,先到先得);归属注销时引用计数 -1,到 0 才摘除条目。
///          引用计数计的是「注册引用」(握手注册/注销配对),不是在途解析器实例数:
///          解析器实例由调用方持有,注销不等待在途解析。
///            - make_parser_by_id   :按协议 id 取解析器(内置优先;插件新 id 取覆盖层)
///            - make_parser_by_id_for_owner:按 (协议 id + 归属) 取解析器(下拉框选择用)
///            - make_tree_builder   :按变体取字段树构建器(声明在 protocol_tree_builder.h;
///                                    内置优先于借用同变体的插件协议)
///            - registered_protocols:界面枚举协议(内置在前;同名 id 不再去重,
///                                    内置与 Ext 条目并列,各带归属)
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
/// @details 内置层已登记同名 id 时拒绝(返回 false,不覆盖);覆盖层注册内置 id
///          直接拒绝(no-shadow,见 register_override_protocol);同一归属重复注册
///          引用计数 +1。
/// @return true = 已登记(或引用计数 +1);false = 被拒绝(内置已存在/影子内置 id/归属冲突)
bool register_protocol(ProtocolProviderPtr provider);
/// @brief 注册协议到覆盖层(插件路径,显式指定归属)
/// @details 归属为空时仍写覆盖层(仅 register_protocol 按归属分流)。
///          no-shadow:内置 id 在此直接拒绝(返回 false,带归属告警),影子路径已去掉;
///          与内置同名共存请走外置路径 register_external_protocol。
bool register_override_protocol(ProtocolProviderPtr provider,
                                const QString& owner_plugin_id);
/// @brief 注册外置解析器(Ext):插件协议与内置同名 id 允许共存,条目以 (id, 归属) 区分
/// @details 与影子路径的区别:Ext 条目永不参与按 id 的查找(make_parser_by_id 对
///          内置 id 恒取内置),只经按归属查找取用;下拉框里与内置并列显示,带
///          " (Ext)" 标记。同一归属重复注册引用计数 +1;不同归属抢占同 id 的 Ext
///          条目直接拒绝(先到先得)。握手时由 PluginManager 调用。
/// @return true = 已登记(或引用计数 +1);false = 被拒绝(归属冲突)
bool register_external_protocol(ProtocolProviderPtr provider,
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

/// @brief 已注册协议快照(内置在前;同名 id 不去重,内置与 Ext 条目并列,各带归属)
QVector<ProtocolProviderPtr> registered_protocols();
/// @brief 按 id 查协议(大小写不敏感,内置优先);未注册返回空
/// @details 内置 id 恒取内置(Ext 条目不参与按 id 查找);插件新 id 取覆盖层条目
ProtocolProviderPtr find_protocol(const QString& protocol_id);
/// @brief 按 (协议 id + 归属) 查协议(下拉框选择用)
/// @details 归属为空 → 同 find_protocol(内置优先);归属非空 → 该归属的外置条目,
///          归属条目不存在(插件已卸载)时回退同 id 内置,绝不取另一归属的条目;
///          无内置亦无归属条目时返回空
ProtocolProviderPtr find_protocol_for_owner(const QString& protocol_id,
                                            const QString& owner_plugin_id);
/// @brief 已注册协议 id 列表(大写,内置在前;Ext 条目与内置同名时该 id 出现两次)
QStringList registered_protocol_ids();

/// @brief 按协议 id 实例化解析器。返回 nullptr = 未知协议。
/// @details 内置优先:内置 id 恒取内置解析器(影子已禁,Ext 条目不参与)
std::unique_ptr<IProtocolParser> make_parser_by_id(const QString& protocol_id);
/// @brief 按 (协议 id + 归属) 实例化解析器(下拉框选择用)。返回 nullptr = 未知协议。
/// @details 归属为空 → 同 make_parser_by_id;归属非空 → 该归属的外置解析器,
///          归属条目已注销时回退同 id 内置(热回退),无内置则返回空
std::unique_ptr<IProtocolParser> make_parser_by_id_for_owner(
    const QString& protocol_id, const QString& owner_plugin_id);

/// @brief 下拉框选择:(协议 id + 归属);归属为空 = 内置协议,非空 = 插件外置解析器(Ext)
/// @details 存入 appcfg::protocol() 的编码:内置为小写 id(如 "nw_2026",与旧配置兼容),
///          外置为小写 id + "@" + 归属插件 id(如 "nw_2026@plugin-x");解码按首个
///          '@' 切分(协议 id 本身不含 '@')
struct ProtocolSelection {
    QString protocol_id;      ///< 大写 id,如 NW_2026
    QString owner_plugin_id;  ///< 空 = 内置;非空 = 归属插件 id
    bool is_external() const { return !owner_plugin_id.isEmpty(); }
};
/// @brief 选择编码为存入配置的串(内置:小写 id;外置:小写 id + "@" + 归属)
QString encode_protocol_selection(const QString& protocol_id,
                                  const QString& owner_plugin_id);
/// @brief 配置串解码为选择(无 '@' → 内置;有 '@' → 按首个 '@' 切分 id 与归属)
ProtocolSelection decode_protocol_selection(const QString& encoded);

// ── 插件便捷接口:只提供解析器(插件侧不构造 QTreeWidget,故无字段树构建器) ──
/// @brief 注册"仅解析器"的协议;display_zh/en 用于界面协议列表
/// @details 一律写覆盖层(插件路径);归属缺省为空(兼容旧调用,新调用请传插件 id)。
///          no-shadow:内置 id 在此直接拒绝(返回 false,带归属告警);与内置同名共存
///          请走外置路径 register_external_parser_provider。
bool register_parser_provider(const QString& protocol_id,
                              const QString& display_zh,
                              const QString& display_en,
                              ParserFactoryFn fn,
                              int order = 200,
                              const QString& owner_plugin_id = QString());
/// @brief 注册"仅解析器"的外置协议(Ext,握手路径);display_zh/en 用于界面协议列表
/// @details 一律写覆盖层;与内置同名 id 允许共存,条目以 (id, 归属) 区分,
///          永不参与按 id 的查找,只经按归属查找取用。
bool register_external_parser_provider(const QString& protocol_id,
                                       const QString& display_zh,
                                       const QString& display_en,
                                       ParserFactoryFn fn,
                                       int order,
                                       const QString& owner_plugin_id);
/// @brief 兼容旧接口:显示名回退为协议 id;一律写覆盖层
/// @details no-shadow:内置 id 在此直接拒绝(返回 false);影子覆盖路径已去掉,
///          与内置同名共存请走外置路径。
bool register_parser(const QString& protocol_id, ParserFactoryFn fn);
/// @brief 兼容旧接口:同 unregister_protocol(只删覆盖层,停机清理语义)
bool unregister_parser(const QString& protocol_id);
/// @brief 兼容旧接口:同 unregister_protocol_for_owner(按归属注销)
bool unregister_parser_for_owner(const QString& protocol_id,
                                 const QString& owner_plugin_id);

#endif  // BPLC_PROTOCOLFACTORY_H
