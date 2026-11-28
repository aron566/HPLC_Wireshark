/// @file registry_probe.cpp
/// @brief 分层注册表探针(no-shadow + Ext + A-7 内置只读守卫)
/// @details 只链接 protocolfactory.cpp,用测试替身提供者模拟四内置协议与插件协议,
///          不依赖真实协议实现。断言(失败即计数,退出码非 0):
///          内置只读(写内置层拒绝覆盖)、no-shadow(影子覆盖内置 id 直接拒绝并告警,
///          按 id 查找恒取内置)、归属 + 引用计数(同归属重复注册 +1、注销 -1、到 0 摘除;
///          不同归属抢占拒绝)、Ext 共存(外置条目与内置同名并列,按 (id + 归属) 区分,
///          按归属查找取外置、未知归属回退内置)、选择编解码(下拉框 (id + 归属) 编码)、
///          未注册 id 返回空、registered_protocols 内置在前、make_tree_builder 内置优先。
///          预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。
#include "protocolfactory.h"
#include "protocol_tree_builder.h"   // make_tree_builder / IProtocolTreeBuilder 完整定义

#include <QCoreApplication>
#include <cstdio>
#include <memory>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const char* name, const QString& detail = QString()) {
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s", name);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s", name);
    }
    if (!detail.isEmpty()) std::printf(" - %s", qPrintable(detail));
    std::printf("\n");
    std::fflush(stdout);
}

// ---- qWarning 捕获(拒绝告警须带插件 id,光靠肉眼看日志不够) ----
static QStringList g_warnings;

static void probe_message_handler(QtMsgType type, const QMessageLogContext&,
                                  const QString& msg) {
    if (type == QtWarningMsg) g_warnings.append(msg);
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}

static bool warned_since(int mark, const QString& needle) {
    for (int i = mark; i < g_warnings.size(); ++i) {
        if (g_warnings.at(i).contains(needle)) return true;
    }
    return false;
}

// ---- 测试替身 ----
class TestParser : public IProtocolParser {
public:
    explicit TestParser(QString tag) : m_tag(std::move(tag)) {}
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }
    ParseResult parse(const BplcFrame&, MsduState&, const ParseFilter&) override {
        return ParseResult();
    }
    QString tag() const { return m_tag; }
private:
    QString m_tag;
};

class TestTreeBuilder : public IProtocolTreeBuilder {
public:
    explicit TestTreeBuilder(QString tag) : m_tag(std::move(tag)) {}
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }
    void build(QTreeWidgetItem*, const PacketEntry&) override {}
    QString tag() const { return m_tag; }
private:
    QString m_tag;
};

class TestProvider : public IProtocolProvider {
public:
    TestProvider(QString id, QString owner, int order, ProtocolVariant variant,
                 QString parser_tag, QString tree_tag = QString())
        : m_id(std::move(id)), m_owner(std::move(owner)), m_order(order),
          m_variant(variant), m_parser_tag(std::move(parser_tag)),
          m_tree_tag(std::move(tree_tag)) {}

    QString id() const override { return m_id; }
    QString display_name_zh() const override { return m_id; }
    QString display_name_en() const override { return m_id; }
    int order() const override { return m_order; }
    ProtocolVariant variant() const override { return m_variant; }
    QString owner_plugin_id() const override { return m_owner; }

    std::unique_ptr<IProtocolParser> create_parser() const override {
        return std::make_unique<TestParser>(m_parser_tag);
    }
    std::unique_ptr<IProtocolTreeBuilder> create_tree_builder() const override {
        if (m_tree_tag.isEmpty()) return nullptr;
        return std::make_unique<TestTreeBuilder>(m_tree_tag);
    }

private:
    QString         m_id;
    QString         m_owner;
    int             m_order;
    ProtocolVariant m_variant;
    QString         m_parser_tag;
    QString         m_tree_tag;
};

static ProtocolProviderPtr test_provider(const QString& id, const QString& owner,
                                         int order, ProtocolVariant variant,
                                         const QString& parser_tag,
                                         const QString& tree_tag = QString()) {
    return std::make_shared<TestProvider>(id, owner, order, variant, parser_tag,
                                          tree_tag);
}

static QString parser_tag_of(const QString& protocol_id) {
    std::unique_ptr<IProtocolParser> p = make_parser_by_id(protocol_id);
    if (!p) return QStringLiteral("<null>");
    const auto* t = dynamic_cast<TestParser*>(p.get());
    return t ? t->tag() : QStringLiteral("<non-test-parser>");
}

static QString parser_tag_of_for_owner(const QString& protocol_id,
                                       const QString& owner) {
    std::unique_ptr<IProtocolParser> p =
        make_parser_by_id_for_owner(protocol_id, owner);
    if (!p) return QStringLiteral("<null>");
    const auto* t = dynamic_cast<TestParser*>(p.get());
    return t ? t->tag() : QStringLiteral("<non-test-parser>");
}

static QString tree_tag_of(ProtocolVariant variant) {
    std::unique_ptr<IProtocolTreeBuilder> tb = make_tree_builder(variant);
    if (!tb) return QStringLiteral("<null>");
    const auto* t = dynamic_cast<TestTreeBuilder*>(tb.get());
    return t ? t->tag() : QStringLiteral("<non-test-builder>");
}

static QStringList registered_owners() {
    QStringList out;
    for (const ProtocolProviderPtr& p : registered_protocols())
        out << p->owner_plugin_id();
    return out;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(probe_message_handler);

    // ---- A-7 守卫名单(与注册表内容无关的静态名单) ----
    check(is_builtin_protocol(QStringLiteral("GW_2022")), "builtin: GW_2022");
    check(is_builtin_protocol(QStringLiteral("NW_2021")), "builtin: NW_2021");
    check(is_builtin_protocol(QStringLiteral("GW_2026")), "builtin: GW_2026");
    check(is_builtin_protocol(QStringLiteral("NW_2026")), "builtin: NW_2026");
    check(is_builtin_protocol(QStringLiteral("gw_2022")), "builtin: lowercase gw_2022");
    check(!is_builtin_protocol(QStringLiteral("MYPROTO")), "builtin: MYPROTO false");
    check(!is_builtin_protocol(QString()), "builtin: empty false");

    // ---- 内置层:乱序注册,验证注册成功与内置在前排序 ----
    check(register_protocol(test_provider(QStringLiteral("NW_2021"), QString(), 40,
                                          ProtocolVariant::NW_2021,
                                          QStringLiteral("builtin-nw2021"),
                                          QStringLiteral("tree-nw2021"))),
          "builtin register NW_2021");
    check(register_protocol(test_provider(QStringLiteral("GW_2022"), QString(), 30,
                                          ProtocolVariant::GW_2022,
                                          QStringLiteral("builtin-gw2022"),
                                          QStringLiteral("tree-gw2022"))),
          "builtin register GW_2022");
    check(register_protocol(test_provider(QStringLiteral("NW_2026"), QString(), 20,
                                          ProtocolVariant::NW_2026,
                                          QStringLiteral("builtin-nw2026"),
                                          QStringLiteral("tree-nw2026"))),
          "builtin register NW_2026");
    check(register_protocol(test_provider(QStringLiteral("GW_2026"), QString(), 10,
                                          ProtocolVariant::GW_2026,
                                          QStringLiteral("builtin-gw2026"))),
          "builtin register GW_2026 (no tree builder)");
    check(registered_protocol_ids()
              == QStringList({QStringLiteral("GW_2026"), QStringLiteral("NW_2026"),
                              QStringLiteral("GW_2022"), QStringLiteral("NW_2021")}),
          "registered ids: builtins first by order",
          registered_protocol_ids().join(QStringLiteral(",")));
    check(registered_protocols().size() == 4, "registered protocols size 4");

    // ---- A1:未注册 id 返回空 ----
    check(make_parser_by_id(QStringLiteral("NO_SUCH_PROTO")) == nullptr,
          "unknown id returns null parser");
    check(find_protocol(QStringLiteral("NO_SUCH_PROTO")) == nullptr,
          "unknown id returns null provider");

    // ---- A-7:写内置层拒绝覆盖已登记的内置 id ----
    int mark = g_warnings.size();
    check(!register_protocol(test_provider(QStringLiteral("GW_2022"), QString(), 30,
                                           ProtocolVariant::GW_2022,
                                           QStringLiteral("evil-builtin"))),
          "A-7: builtin overwrite refused");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "A-7: builtin entry intact after refused overwrite");
    check(warned_since(mark, QStringLiteral("GW_2022")),
          "A-7: refusal warned with protocol id");

    // ---- no-shadow:影子覆盖内置 id 直接拒绝并告警(A4:拒绝 + 告警) ----
    mark = g_warnings.size();
    const auto shadow_a = []() {
        return std::make_unique<TestParser>(QStringLiteral("shadow-a"));
    };
    check(!register_parser_provider(QStringLiteral("GW_2022"),
                                    QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                    shadow_a, 200, QStringLiteral("plugin-a")),
          "no-shadow: shadow GW_2022 by plugin-a refused");
    check(warned_since(mark, QStringLiteral("GW_2022"))
              && warned_since(mark, QStringLiteral("plugin-a"))
              && warned_since(mark, QStringLiteral("refuses")),
          "no-shadow: refusal warns with protocol id and owner plugin id");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "no-shadow: builtin parser intact after refused shadow");
    check(protocol_owner_plugin_id(QStringLiteral("GW_2022")).isEmpty(),
          "no-shadow: no override entry after refused shadow");
    check(override_ref_count(QStringLiteral("GW_2022")) == 0,
          "no-shadow: override refcount 0 after refused shadow");
    check(registered_protocol_ids().size() == 4,
          "no-shadow: refused shadow not listed");

    // ---- no-shadow:同一归属重复注册内置 id 同样拒绝,不产生引用计数 ----
    check(!register_parser_provider(QStringLiteral("GW_2022"),
                                    QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                    shadow_a, 200, QStringLiteral("plugin-a")),
          "no-shadow: same-owner re-register of builtin id refused");
    check(override_ref_count(QStringLiteral("GW_2022")) == 0,
          "no-shadow: refcount stays 0");

    // ---- no-shadow:不同归属影子同样拒绝(不再是先到先得,而是内置 id 一律拒绝) ----
    mark = g_warnings.size();
    const auto shadow_b = []() {
        return std::make_unique<TestParser>(QStringLiteral("shadow-b"));
    };
    check(!register_parser_provider(QStringLiteral("GW_2022"),
                                    QStringLiteral("插件 B"), QStringLiteral("Plugin B"),
                                    shadow_b, 200, QStringLiteral("plugin-b")),
          "no-shadow: second owner shadow refused");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "no-shadow: builtin intact after rejected second owner");
    check(warned_since(mark, QStringLiteral("plugin-b")),
          "no-shadow: rejection warning names rejected owner");
    check(!unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                       QStringLiteral("plugin-b")),
          "no-shadow: wrong-owner unregister refused (no entry)");
    check(!unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                       QStringLiteral("plugin-a")),
          "no-shadow: unregister of never-registered shadow returns false");

    // ---- no-shadow:unregister_protocol 对从未影子过的内置 id 返回 false ----
    check(!register_parser_provider(QStringLiteral("NW_2021"),
                                    QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                    shadow_a, 200, QStringLiteral("plugin-a")),
          "no-shadow: shadow NW_2021 refused");
    check(!unregister_protocol(QStringLiteral("NW_2021")),
          "no-shadow: unregister_protocol on never-shadowed builtin returns false");
    check(parser_tag_of(QStringLiteral("NW_2021")) == QStringLiteral("builtin-nw2021"),
          "no-shadow: builtin NW_2021 intact");
    check(!unregister_protocol(QStringLiteral("GW_2026")),
          "no-shadow: unregister_protocol on pure builtin returns false");
    check(parser_tag_of(QStringLiteral("GW_2026")) == QStringLiteral("builtin-gw2026"),
          "no-shadow: pure builtin untouched by unregister_protocol");

    // ---- no-shadow:register_parser(旧接口)影子内置同样拒绝 ----
    const auto shadow_legacy = []() {
        return std::make_unique<TestParser>(QStringLiteral("shadow-legacy"));
    };
    check(!register_parser(QStringLiteral("GW_2022"), shadow_legacy),
          "no-shadow: register_parser shadow of builtin refused");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "no-shadow: builtin intact after refused legacy shadow");
    check(!unregister_parser(QStringLiteral("GW_2022")),
          "no-shadow: unregister_parser of never-registered shadow returns false");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "no-shadow: builtin intact after unregister_parser");

    // ---- A1:全新插件协议(非影子)排在内置之后 ----
    check(register_parser_provider(QStringLiteral("MYPROTO_2024"),
                                   QStringLiteral("我的协议"), QStringLiteral("My Protocol"),
                                   shadow_a, 200, QStringLiteral("plugin-c")),
          "A1: register new plugin protocol MYPROTO_2024");
    check(registered_protocol_ids()
              == QStringList({QStringLiteral("GW_2026"), QStringLiteral("NW_2026"),
                              QStringLiteral("GW_2022"), QStringLiteral("NW_2021"),
                              QStringLiteral("MYPROTO_2024")}),
          "A1: new plugin protocol listed after builtins",
          registered_protocol_ids().join(QStringLiteral(",")));
    check(parser_tag_of(QStringLiteral("myproto_2024")) == QStringLiteral("shadow-a"),
          "A1: new plugin protocol lookup case-insensitive");
    check(protocol_owner_plugin_id(QStringLiteral("MYPROTO_2024"))
              == QStringLiteral("plugin-c"),
          "A1: new plugin protocol owner is plugin-c");

    // ---- A1:make_tree_builder 内置优先(插件借用同变体也抢不走) ----
    check(tree_tag_of(ProtocolVariant::NW_2021) == QStringLiteral("tree-nw2021"),
          "A1: tree builder builtin NW_2021");
    check(tree_tag_of(ProtocolVariant::GW_2026) == QStringLiteral("<null>"),
          "A1: tree builder null when builtin provides none");
    check(!register_protocol(test_provider(QStringLiteral("NW_2021"),
                                           QStringLiteral("plugin-a"), 200,
                                           ProtocolVariant::NW_2021,
                                           QStringLiteral("shadow-nw2021"),
                                           QStringLiteral("tree-shadow"))),
          "no-shadow: register_protocol with owner cannot shadow builtin NW_2021");
    check(tree_tag_of(ProtocolVariant::NW_2021) == QStringLiteral("tree-nw2021"),
          "no-shadow: tree builder still builtin after refused shadow");
    check(parser_tag_of(QStringLiteral("NW_2021")) == QStringLiteral("builtin-nw2021"),
          "no-shadow: parser stays builtin after refused shadow");

    // ---- A1:register_protocol 带归属直接写覆盖层(新 id);归属注销按归属校验 ----
    check(register_protocol(test_provider(QStringLiteral("MYPROTO2"),
                                          QStringLiteral("plugin-d"), 200,
                                          ProtocolVariant::GW_2022,
                                          QStringLiteral("proto2"))),
          "A1: register_protocol with owner writes override layer");
    check(protocol_owner_plugin_id(QStringLiteral("MYPROTO2"))
              == QStringLiteral("plugin-d"),
          "A1: MYPROTO2 owner is plugin-d");
    check(!unregister_protocol_for_owner(QStringLiteral("MYPROTO2"),
                                         QStringLiteral("plugin-x")),
          "A1: MYPROTO2 wrong-owner unregister refused");
    check(unregister_protocol_for_owner(QStringLiteral("MYPROTO2"),
                                        QStringLiteral("plugin-d")),
          "A1: MYPROTO2 right-owner unregister ok");
    mark = g_warnings.size();
    check(!register_override_protocol(
              test_provider(QStringLiteral("GW_2026"), QString(), 200,
                            ProtocolVariant::GW_2026,
                            QStringLiteral("shadow-unknown")),
              QString()),
          "no-shadow: override with empty owner cannot shadow builtin GW_2026");
    check(warned_since(mark, QStringLiteral("<unknown>")),
          "no-shadow: refusal with unknown owner warns as <unknown>");

    // ---- Ext:外置条目与内置同名共存,按 (id + 归属) 区分 ----
    const auto ext_a = []() {
        return std::make_unique<TestParser>(QStringLiteral("ext-a"));
    };
    check(register_external_parser_provider(QStringLiteral("NW_2026"),
                                            QStringLiteral("插件 A"),
                                            QStringLiteral("Plugin A"),
                                            ext_a, 200, QStringLiteral("plugin-a")),
          "Ext: plugin-a registers NW_2026 as external");
    check(parser_tag_of(QStringLiteral("NW_2026")) == QStringLiteral("builtin-nw2026"),
          "Ext: id-only lookup still returns builtin");
    check(parser_tag_of_for_owner(QStringLiteral("NW_2026"),
                                  QStringLiteral("plugin-a")) == QStringLiteral("ext-a"),
          "Ext: owner-aware lookup returns plugin parser");
    check(parser_tag_of_for_owner(QStringLiteral("NW_2026"),
                                  QString()) == QStringLiteral("builtin-nw2026"),
          "Ext: empty-owner lookup returns builtin");
    check(parser_tag_of_for_owner(QStringLiteral("NW_2026"),
                                  QStringLiteral("plugin-x"))
              == QStringLiteral("builtin-nw2026"),
          "Ext: unknown-owner lookup falls back to builtin");
    check(registered_protocols().size() == 6,
          "Ext: builtin and Ext entries coexist (size 6)");
    check(registered_protocol_ids()
              == QStringList({QStringLiteral("GW_2026"), QStringLiteral("NW_2026"),
                              QStringLiteral("GW_2022"), QStringLiteral("NW_2021"),
                              QStringLiteral("MYPROTO_2024"), QStringLiteral("NW_2026")}),
          "Ext: ids list builtin first, Ext entry after",
          registered_protocol_ids().join(QStringLiteral(",")));
    check(registered_owners()
              == QStringList({QString(), QString(), QString(), QString(),
                              QStringLiteral("plugin-c"), QStringLiteral("plugin-a")}),
          "Ext: entries distinguished by (id, owner)",
          registered_owners().join(QStringLiteral(",")));

    // ---- Ext:同一 id 的第二个归属直接拒绝(先到先得);按归属注销 ----
    const auto ext_b = []() {
        return std::make_unique<TestParser>(QStringLiteral("ext-b"));
    };
    check(!register_external_parser_provider(QStringLiteral("NW_2026"),
                                             QStringLiteral("插件 B"),
                                             QStringLiteral("Plugin B"),
                                             ext_b, 200, QStringLiteral("plugin-b")),
          "Ext: second owner Ext registration rejected (first wins)");
    check(parser_tag_of_for_owner(QStringLiteral("NW_2026"),
                                  QStringLiteral("plugin-a")) == QStringLiteral("ext-a"),
          "Ext: first owner Ext entry intact");
    check(!unregister_parser_for_owner(QStringLiteral("NW_2026"),
                                       QStringLiteral("plugin-b")),
          "Ext: wrong-owner unregister refused");
    check(unregister_parser_for_owner(QStringLiteral("NW_2026"),
                                      QStringLiteral("plugin-a")),
          "Ext: right-owner unregister ok");
    check(parser_tag_of_for_owner(QStringLiteral("NW_2026"),
                                  QStringLiteral("plugin-a"))
              == QStringLiteral("builtin-nw2026"),
          "Ext: owner-aware lookup falls back to builtin after unregister");

    // ---- Ext:新 id 经外置路径注册,按 id 查找同样可达(无内置可取) ----
    const auto ext_e = []() {
        return std::make_unique<TestParser>(QStringLiteral("ext-e"));
    };
    check(register_external_parser_provider(QStringLiteral("MYPROTO_EXT"),
                                            QStringLiteral("外置协议"),
                                            QStringLiteral("External Protocol"),
                                            ext_e, 200, QStringLiteral("plugin-e")),
          "Ext: new plugin id registers via external path");
    check(parser_tag_of(QStringLiteral("MYPROTO_EXT")) == QStringLiteral("ext-e"),
          "Ext: new plugin id reachable by id-only lookup");
    check(parser_tag_of_for_owner(QStringLiteral("MYPROTO_EXT"),
                                  QStringLiteral("plugin-e")) == QStringLiteral("ext-e"),
          "Ext: new plugin id reachable by owner lookup");

    // ---- 选择编解码:下拉框 (id + 归属) 编码 ----
    const ProtocolSelection sel_b =
        decode_protocol_selection(QStringLiteral("nw_2026"));
    check(sel_b.protocol_id == QStringLiteral("NW_2026")
              && sel_b.owner_plugin_id.isEmpty(),
          "selection: builtin decodes to empty owner");
    const ProtocolSelection sel_e =
        decode_protocol_selection(QStringLiteral("nw_2026@plugin-a"));
    check(sel_e.protocol_id == QStringLiteral("NW_2026")
              && sel_e.owner_plugin_id == QStringLiteral("plugin-a"),
          "selection: Ext decodes to owner");
    check(encode_protocol_selection(QStringLiteral("NW_2026"), QString())
              == QStringLiteral("nw_2026"),
          "selection: builtin encodes as lowercase id");
    check(encode_protocol_selection(QStringLiteral("NW_2026"),
                                    QStringLiteral("plugin-a"))
              == QStringLiteral("nw_2026@plugin-a"),
          "selection: Ext encodes id and owner");
    const ProtocolSelection sel_empty = decode_protocol_selection(QString());
    check(sel_empty.protocol_id.isEmpty() && sel_empty.owner_plugin_id.isEmpty(),
          "selection: empty string decodes to empty");
    const ProtocolSelection sel_rt = decode_protocol_selection(
        encode_protocol_selection(QStringLiteral("NW_2026"),
                                  QStringLiteral("plugin-a")));
    check(sel_rt.protocol_id == QStringLiteral("NW_2026")
              && sel_rt.owner_plugin_id == QStringLiteral("plugin-a"),
          "selection: Ext encode/decode round-trips");

    // ---- 收尾:插件条目清理后只剩四内置 ----
    check(unregister_protocol(QStringLiteral("MYPROTO_2024")),
          "cleanup: unregister MYPROTO_2024");
    check(unregister_protocol(QStringLiteral("MYPROTO_EXT")),
          "cleanup: unregister MYPROTO_EXT");
    check(registered_protocol_ids().size() == 4
              && parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022")
              && parser_tag_of(QStringLiteral("NW_2021")) == QStringLiteral("builtin-nw2021"),
          "cleanup: only builtins remain");

    std::printf("RESULT: %s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail);
    std::printf("  pass=%d fail=%d\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
