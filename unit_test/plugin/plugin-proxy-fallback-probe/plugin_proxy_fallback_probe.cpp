/// @file plugin_proxy_fallback_probe.cpp
/// @brief A2 回归:逐帧懒回退——插件协议不可用时代理按帧懒取内置解析器
/// @details 移植来源 parser 分支 1fc0a21 (A2 逐帧懒回退):
/// PluginParserProxy::parse 在 is_plugin_protocol(id) 为 false 时,懒创建
/// 内置回退解析器并逐帧经它解析,而不是直接拒收。回退只取内置层
/// (make_builtin_parser_by_id),永不取他方 Ext 条目:采集中不切换,且插件
/// 在检查与查找之间重注册的竞态下也不会误取新插件实例。
/// 直接构造代理(无运行时、无 host 进程),用 BEACON 帧验证(帧构造抄
/// plugin-unregister-fallback-probe 的 build_frame):
///  - GW_2022 好 CRC 帧 → 内置接受;坏 CRC 帧 → 内置拒收(拒因是内置的 CRC
///    拒因,不是 "plugin unavailable");重复解析复用缓存的回退实例;
///  - GW_2022 注册一个 Ext 条目(计数替身)后,代理仍走内置(替身 0 次调用);
///  - 无内置的新 id PROBE_PURE_NEW → 拒收 "plugin unavailable (no builtin
///    fallback)"。
/// 预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。旧代码跑本探针:
/// 好帧断言 FAIL(拒收 "plugin unavailable")。
/// 必须以 ASan+UBSan 构建运行(build.sh 已加)。
#include <QCoreApplication>
#include <atomic>
#include <cstdio>
#include <memory>

#include "crc.h"
#include "plugin_manager.h"
#include "plugin_parser_proxy.h"
#include "protocolfactory.h"

namespace {

int g_checks = 0;
int g_fails = 0;

void check(const char* name, bool ok) {
    ++g_checks;
    if (!ok) ++g_fails;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
}

/// @brief 替身插件解析器:恒接受并计数调用(与内置拒收坏帧形成对照)
class FakePluginParser : public IProtocolParser {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2022; }
    ParseResult parse(const BplcFrame&, MsduState&, const ParseFilter&) override {
        s_calls.fetch_add(1);
        ParseResult r;
        r.protocol = ProtocolVariant::GW_2022;
        r.accept = true;
        return r;
    }
    static std::atomic<int> s_calls;
};
std::atomic<int> FakePluginParser::s_calls{0};

/// @brief 合成一帧 BEACON:16B FCH(CRC24 好/坏由 good_crc 定)+ 32B 零载荷,
///        data = [dlen 2B][NTB 4B][媒介头 4B][MPDU](抄
///        plugin-unregister-fallback-probe 的 build_frame)
static BplcFrame build_frame(bool good_crc) {
    QByteArray fch(16, char(0));
    fch[1] = char(0x56);   // net_id 0x123456 LE
    fch[2] = char(0x34);
    fch[3] = char(0x12);
    fch[8] = char(0x01);   // src_tei = 1(CCO)
    quint32 crc = crc24_lsb(reinterpret_cast<const quint8*>(fch.constData()), 16);
    if (!good_crc) crc ^= 0xFFFFFFu;   // 坏 CRC:恒与算值不同
    fch[13] = char(crc & 0xFF);
    fch[14] = char((crc >> 8) & 0xFF);
    fch[15] = char((crc >> 16) & 0xFF);
    const QByteArray mpdu = fch + QByteArray(32, char(0));

    QByteArray data;
    const quint16 dlen = quint16(mpdu.size() + 4);
    data.append(char(dlen & 0xFF)).append(char(dlen >> 8));
    const quint32 ntb = 100000;
    data.append(char(ntb & 0xFF)).append(char((ntb >> 8) & 0xFF))
        .append(char((ntb >> 16) & 0xFF)).append(char((ntb >> 24) & 0xFF));
    data.append(char(0x11)).append(char(0x22)).append(char(2)).append(char(0));
    data.append(mpdu);

    BplcFrame f;
    f.data = data;
    f.accepted = true;
    return f;
}

ParseResult parse_once(PluginParserProxy& proxy, const BplcFrame& frame) {
    MsduState msdu;
    ParseFilter filter;
    return proxy.parse(frame, msdu, filter);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // 内置层直取助手本身:有内置则取到,无内置则空(与 Ext 条目无关)。
    check("builtin helper returns GW_2022 parser",
          make_builtin_parser_by_id(QStringLiteral("GW_2022")) != nullptr);
    check("builtin helper returns null for unknown id",
          make_builtin_parser_by_id(QStringLiteral("PROBE_PURE_NEW")) == nullptr);

    // 无运行时:代理按帧懒取内置回退。
    PluginParserProxy proxy(QStringLiteral("GW_2022"));
    const ParseResult bad = parse_once(proxy, build_frame(false));
    check("A2: bad-CRC frame rejected by builtin (not plugin-unavailable)",
          !bad.accept && bad.reject_reason != QStringLiteral("plugin unavailable"));
    const ParseResult good = parse_once(proxy, build_frame(true));
    check("A2: good frame accepted via lazy builtin fallback", good.accept);
    const ParseResult good2 = parse_once(proxy, build_frame(true));
    check("A2: repeated parse reuses cached fallback", good2.accept);

    // Ext 条目在场时回退仍只取内置,替身一次也不被调。
    FakePluginParser::s_calls.store(0);
    check("Ext GW_2022 registered by probe-plugin",
          register_external_parser_provider(
              QStringLiteral("GW_2022"), QStringLiteral("探针插件"),
              QStringLiteral("Probe Plugin"),
              []() { return std::make_unique<FakePluginParser>(); }, 200,
              QStringLiteral("probe-plugin")));
    const ParseResult good3 = parse_once(proxy, build_frame(true));
    check("A2: fallback stays builtin with Ext present (accepted)", good3.accept);
    check("A2: Ext parser never called as fallback",
          FakePluginParser::s_calls.load() == 0);
    check("Ext entry cleaned up",
          unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                      QStringLiteral("probe-plugin")));

    // 无内置的新 id:无回退可取,拒收并点明原因。
    PluginParserProxy proxy2(QStringLiteral("PROBE_PURE_NEW"));
    const ParseResult r2 = parse_once(proxy2, build_frame(true));
    check("A2: pure-new id rejected", !r2.accept);
    check("A2: pure-new reject reason names missing builtin fallback",
          r2.reject_reason == QStringLiteral("plugin unavailable (no builtin fallback)"));

    std::printf("\nRESULT: %s (%d failures)\n", g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
