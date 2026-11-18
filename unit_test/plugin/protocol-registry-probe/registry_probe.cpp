/// @file registry_probe.cpp
/// @brief 分层注册表探针(A1 内置层/覆盖层 + A-7 内置只读守卫 + A4 影子告警)
/// @details 只链接 protocolfactory.cpp,用测试替身提供者模拟四内置协议与插件协议,
///          不依赖真实协议实现。断言(失败即计数,退出码非 0):
///          内置只读(写内置层拒绝覆盖)、覆盖层影子(覆盖优先、卸载只删覆盖层)、
///          归属 + 引用计数(同归属重复注册 +1、注销 -1、到 0 摘除;不同归属抢占拒绝)、
///          同名覆盖告警(带插件 id)、未注册 id 返回空、registered_protocols 去重内置在前、
///          make_tree_builder 内置优先。预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。
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

// ---- qWarning 捕获(A4 告警须带插件 id,光靠肉眼看日志不够) ----
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

static QString tree_tag_of(ProtocolVariant variant) {
    std::unique_ptr<IProtocolTreeBuilder> tb = make_tree_builder(variant);
    if (!tb) return QStringLiteral("<null>");
    const auto* t = dynamic_cast<TestTreeBuilder*>(tb.get());
    return t ? t->tag() : QStringLiteral("<non-test-builder>");
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

    // ---- A1 + A4:影子覆盖(覆盖层优先,告警带插件 id) ----
    mark = g_warnings.size();
    const auto shadow_a = []() {
        return std::make_unique<TestParser>(QStringLiteral("shadow-a"));
    };
    check(register_parser_provider(QStringLiteral("GW_2022"),
                                   QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                   shadow_a, 200, QStringLiteral("plugin-a")),
          "A1: shadow GW_2022 by plugin-a");
    check(warned_since(mark, QStringLiteral("GW_2022"))
              && warned_since(mark, QStringLiteral("plugin-a"))
              && warned_since(mark, QStringLiteral("shadows")),
          "A4: shadow warning carries protocol id and owner plugin id");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("shadow-a"),
          "A1: override wins over builtin");
    check(parser_tag_of(QStringLiteral("gw_2022")) == QStringLiteral("shadow-a"),
          "A1: shadow lookup case-insensitive");
    check(protocol_owner_plugin_id(QStringLiteral("GW_2022"))
              == QStringLiteral("plugin-a"),
          "A1: override owner is plugin-a");
    check(override_ref_count(QStringLiteral("GW_2022")) == 1,
          "A1: override refcount 1 after first register");
    check(registered_protocol_ids().size() == 4,
          "A1: shadowed id listed once (dedup)");
    check(registered_protocol_ids().first() == QStringLiteral("GW_2026"),
          "A1: builtins still first with shadow present");

    // ---- A1:同一归属重复注册引用计数 +1,注销 -1,到 0 摘除 ----
    check(register_parser_provider(QStringLiteral("GW_2022"),
                                   QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                   shadow_a, 200, QStringLiteral("plugin-a")),
          "A1: same-owner re-register ok");
    check(override_ref_count(QStringLiteral("GW_2022")) == 2,
          "A1: override refcount 2 after re-register");
    check(unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                      QStringLiteral("plugin-a")),
          "A1: owner unregister once ok");
    check(override_ref_count(QStringLiteral("GW_2022")) == 1
              && parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("shadow-a"),
          "A1: still shadowed after first owner unregister (refcount 1)");
    check(unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                      QStringLiteral("plugin-a")),
          "A1: owner unregister twice ok");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "A1: builtin restored after refcount reaches 0");

    // ---- A1:不同归属抢占已被影子的 id 直接拒绝(后注册拒绝,先到先得) ----
    check(register_parser_provider(QStringLiteral("GW_2022"),
                                   QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                   shadow_a, 200, QStringLiteral("plugin-a")),
          "A1: re-shadow GW_2022 by plugin-a");
    mark = g_warnings.size();
    const auto shadow_b = []() {
        return std::make_unique<TestParser>(QStringLiteral("shadow-b"));
    };
    check(!register_parser_provider(QStringLiteral("GW_2022"),
                                    QStringLiteral("插件 B"), QStringLiteral("Plugin B"),
                                    shadow_b, 200, QStringLiteral("plugin-b")),
          "A1: second owner shadow rejected");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("shadow-a"),
          "A1: first owner shadow intact after rejected second owner");
    check(warned_since(mark, QStringLiteral("plugin-b")),
          "A1: rejection warning names rejected owner");
    check(!unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                       QStringLiteral("plugin-b")),
          "A1: wrong-owner unregister refused");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("shadow-a"),
          "A1: shadow intact after wrong-owner unregister");
    check(unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                      QStringLiteral("plugin-a")),
          "A1: right-owner unregister ok");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "A1: builtin restored after right-owner unregister");

    // ---- A1:unregister_protocol 只删覆盖层(停机清理语义),内置永远不受影响 ----
    check(register_parser_provider(QStringLiteral("NW_2021"),
                                   QStringLiteral("插件 A"), QStringLiteral("Plugin A"),
                                   shadow_a, 200, QStringLiteral("plugin-a")),
          "A1: shadow NW_2021 by plugin-a");
    check(unregister_protocol(QStringLiteral("NW_2021")),
          "A1: unregister_protocol removes override");
    check(parser_tag_of(QStringLiteral("NW_2021")) == QStringLiteral("builtin-nw2021"),
          "A1: builtin restored after unregister_protocol");
    check(!unregister_protocol(QStringLiteral("NW_2021")),
          "A1: unregister_protocol again returns false");
    check(!unregister_protocol(QStringLiteral("GW_2026")),
          "A1: unregister_protocol on pure builtin returns false");
    check(parser_tag_of(QStringLiteral("GW_2026")) == QStringLiteral("builtin-gw2026"),
          "A1: pure builtin untouched by unregister_protocol");

    // ---- A-7 与 A1 叠加:register_parser(旧接口)写覆盖层,内置同名不拒绝 ----
    const auto shadow_legacy = []() {
        return std::make_unique<TestParser>(QStringLiteral("shadow-legacy"));
    };
    check(register_parser(QStringLiteral("GW_2022"), shadow_legacy),
          "A1/A-7: register_parser shadows builtin (override layer, not refused)");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("shadow-legacy"),
          "A1/A-7: legacy shadow effective");
    check(unregister_parser(QStringLiteral("GW_2022")),
          "A1/A-7: unregister_parser removes legacy shadow only");
    check(parser_tag_of(QStringLiteral("GW_2022")) == QStringLiteral("builtin-gw2022"),
          "A1/A-7: builtin intact after legacy shadow removed");

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
    check(register_protocol(test_provider(QStringLiteral("NW_2021"),
                                          QStringLiteral("plugin-a"), 200,
                                          ProtocolVariant::NW_2021,
                                          QStringLiteral("shadow-nw2021"),
                                          QStringLiteral("tree-shadow"))),
          "A1: shadow NW_2021 via register_protocol with owner");
    check(tree_tag_of(ProtocolVariant::NW_2021) == QStringLiteral("tree-nw2021"),
          "A1: tree builder still builtin while NW_2021 shadowed");
    check(parser_tag_of(QStringLiteral("NW_2021")) == QStringLiteral("shadow-nw2021"),
          "A1: parser still shadowed while tree builder stays builtin");
    check(unregister_protocol_for_owner(QStringLiteral("NW_2021"),
                                        QStringLiteral("plugin-a")),
          "A1: cleanup shadow NW_2021");

    // ---- A1:register_protocol 带归属直接写覆盖层;归属为空的影子告警记 <unknown> ----
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
    check(register_override_protocol(
              test_provider(QStringLiteral("GW_2026"), QString(), 200,
                            ProtocolVariant::GW_2026,
                            QStringLiteral("shadow-unknown")),
              QString()),
          "A1: override with empty owner accepted");
    check(warned_since(mark, QStringLiteral("<unknown>")),
          "A4: shadow with unknown owner warns as <unknown>");
    check(unregister_protocol(QStringLiteral("GW_2026")),
          "A1: cleanup unknown-owner shadow");

    // ---- 收尾:插件条目清理后只剩四内置 ----
    check(unregister_protocol(QStringLiteral("MYPROTO_2024")),
          "cleanup: unregister MYPROTO_2024");
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
