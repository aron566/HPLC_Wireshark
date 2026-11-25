/// @file delegate_probe.cpp
/// @brief NW_2026 委托走注册表探针(no-shadow 委托语义)
/// @details NW_2026 把 BPLC 版帧经 make_parser_by_id("NW_2021") 委托:
///          内置委托结果与裸 NW_2021_Parser 逐项一致;影子注册内置 id 直接拒绝,
///          委托恒取内置(标记解析器不生效);外置注册同样不影响委托——按 id 查找
///          恒取内置,外置解析器只经按归属查找取用。
///          用法: delegate_probe <user_frame_container.bin>(首帧为聚合 BPLC SOF,
///          探针在内存里清聚合标志位走标准委托分支,不改入库文件)。
///          预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。
#include <QApplication>
#include <QFile>
#include <QDataStream>
#include <cstdio>
#include <memory>
#include "bplcframe.h"
#include "nw_2026_parser.h"
#include "nw_2021_parser.h"
#include "protocolfactory.h"

class MarkerParser : public IProtocolParser {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::NW_2021; }
    ParseResult parse(const BplcFrame&, MsduState&, const ParseFilter&) override {
        ParseResult r;
        r.accept = false;
        r.reject_reason = QStringLiteral("shadow-marker");
        return r;
    }
};

static int g_fail = 0;
static void check(bool ok, const char* name) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

/// @brief 经 NW_2026 委托解析一帧,并与裸 NW_2021_Parser 的结果比对
static bool delegate_equals_bare(const BplcFrame& in) {
    NW_2026_Parser parser;
    MsduState st;
    ParseFilter pf;
    const ParseResult r = parser.parse(in, st, pf);
    NW_2021_Parser direct;
    MsduState st2;
    const ParseResult r2 = direct.parse(in, st2, pf);
    std::printf("  [diag] nw2026 accept=%d reject='%s' | bare nw2021 accept=%d reject='%s'\n",
                (int)r.accept, qPrintable(r.reject_reason),
                (int)r2.accept, qPrintable(r2.reject_reason));
    std::fflush(stdout);
    return r.accept == r2.accept && r.reject_reason == r2.reject_reason;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 2) { std::fprintf(stderr, "usage: delegate_check <container>\n"); return 2; }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "cannot open\n"); return 1; }
    QDataStream ds(&f); ds.setByteOrder(QDataStream::LittleEndian);
    quint32 len = 0; ds >> len;
    const QByteArray mpdu = f.read(len);
    if (mpdu.size() != (int)len) { std::fprintf(stderr, "short frame\n"); return 1; }
    // 清聚合标志位(mpdu[18] bit0):原帧是聚合 BPLC 例外,走本地表解析不委托;
    // 清位后走标准委托分支(parse_legacy)。委托发生在 PB 处理之前,改位不影响
    // 委托可达性判定(PB CRC 等由委托方再算)。
    QByteArray mod = mpdu;
    mod[18] = char(quint8(mod.at(18)) & 0xFE);
    BplcFrame in;
    in.data = QByteArray(1, char(0)) + mod;
    in.meta.from_raw = true;

    check(make_parser_by_id(QStringLiteral("NW_2021")) != nullptr,
          "registry provides builtin NW_2021");
    check(delegate_equals_bare(in),
          "delegate via registry equals bare NW_2021 parse");

    // no-shadow:影子注册内置 id 直接拒绝,委托恒取内置
    check(!register_parser_provider(QStringLiteral("NW_2021"),
                                    QStringLiteral("影子"), QStringLiteral("Shadow"),
                                    []() { return std::make_unique<MarkerParser>(); },
                                    200, QStringLiteral("shadow-plugin")),
          "no-shadow: shadow NW_2021 registration refused");
    {
        std::unique_ptr<IProtocolParser> direct = make_parser_by_id(QStringLiteral("NW_2021"));
        check(dynamic_cast<MarkerParser*>(direct.get()) == nullptr,
              "no-shadow: registry still returns builtin NW_2021");
        check(delegate_equals_bare(in),
              "no-shadow: delegate still equals bare NW_2021 parse");
    }

    // Ext:外置注册不影响委托——按 id 查找恒取内置,外置只经按归属查找取用
    check(register_external_parser_provider(QStringLiteral("NW_2021"),
                                            QStringLiteral("外置"),
                                            QStringLiteral("External"),
                                            []() { return std::make_unique<MarkerParser>(); },
                                            200, QStringLiteral("ext-plugin")),
          "Ext: external NW_2021 registered");
    {
        std::unique_ptr<IProtocolParser> direct = make_parser_by_id(QStringLiteral("NW_2021"));
        check(dynamic_cast<MarkerParser*>(direct.get()) == nullptr,
              "Ext: id-only lookup still returns builtin NW_2021");
        std::unique_ptr<IProtocolParser> ext =
            make_parser_by_id_for_owner(QStringLiteral("NW_2021"),
                                        QStringLiteral("ext-plugin"));
        check(dynamic_cast<MarkerParser*>(ext.get()) != nullptr,
              "Ext: owner-aware lookup returns external parser");
        check(delegate_equals_bare(in),
              "Ext: delegate still equals bare NW_2021 parse");
        check(unregister_parser_for_owner(QStringLiteral("NW_2021"),
                                          QStringLiteral("ext-plugin")),
              "Ext: cleanup external NW_2021");
    }
    std::printf("RESULT: %s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
