/// @file plugin_unregister_fallback_probe.cpp
/// @brief A-8 回归:插件注销后分发器重建回退内置、下一帧正常解析;
///        注册到达时采集中不切换、空闲时切换。
/// @details 探针在 FrameDispatcher 层复现 MainWindow::on_plugin_status_changed
///          的三个分支(槽本身是 3 行分发,行为链在此验证):
///          - 注销 → 重建分发器(即 reset_dispatcher 的分发器部分):
///            新分发器经 make_parser_by_id 回退内置,下一帧正常解析;
///          - 注册到达 + 采集中 → 不重建:运行中的分发器持有旧解析器实例,
///            继续用内置解析,不打断在途 MSDU 重组;
///          - 注册到达 + 空闲 → 重建:新分发器取到覆盖层插件解析器。
///          对照信号:内置 GW_2022 拒收坏 FCH CRC 帧(计 dropped),替身插件
///          解析器恒接受并计数调用次数。prepare_entry 用探针内桩(只搬
///          accept/mpdu/meta,免链接整个 UI/entrybuilder→topo),与
///          frame-dispatcher-lifetime-probe 的桩策略一致。
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <cstdio>

#include "appconfig.h"
#include "entrybuilder.h"
#include "framedispatcher.h"
#include "iprotocolparser.h"
#include "protocolfactory.h"
#include "crc.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

// 本探针不喂帧给真实 EntryBuilder:桩只搬 on_parsed 统计所需的字段
PreparedEntryPtr prepare_entry(ParseResult&& r) {
    auto p = QSharedPointer<PreparedEntry>::create();
    p->entry.accepted = r.accept;
    p->entry.mpdu = r.mpdu;
    p->entry.meta = r.meta;
    p->entry.raw_wire = r.raw_wire;
    return p;
}

/// @brief 喂帧信号源(connect_source 要求信号 frame_ready(BplcFrame, quint64))
class FrameSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void frame_ready(BplcFrame, quint64);
    void finished();
};

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
///        data = [dlen 2B][NTB 4B][媒介头 4B][MPDU](不经 reader,直接喂分发器)
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

/// @brief 等当前分发器统计总数达到 want(解析线程排队投递,需泵事件循环)
static bool wait_total(FrameDispatcher* d, qint64 want, int timeout_ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (d->statistics()->total() < want && t.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return d->statistics()->total() >= want;
}

static qint64 dropped_of(FrameDispatcher* d) {
    return d->statistics()->snapshot().value(int(FrameStatistics::KEY_DROPPED), -1);
}

/// @brief 重建分发器(与 CaptureController::reset_dispatcher 的分发器部分同义:
///        shutdown + 删旧 + 按当前注册表新建并接回帧源)
static FrameDispatcher* rebuild(FrameDispatcher* d, FrameSource& src) {
    if (d) {
        d->shutdown();
        for (int k = 0; k < 5; ++k) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QThread::msleep(1);
        }
        delete d;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    auto* nd = new FrameDispatcher();
    nd->connect_source(&src);
    return nd;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString old_protocol = appcfg::protocol();
    appcfg::set_protocol(QStringLiteral("gw_2022"));

    qRegisterMetaType<BplcFrame>("BplcFrame");
    FrameSource src;
    const BplcFrame bad = build_frame(false);    // 内置拒收、插件恒接受
    const BplcFrame good = build_frame(true);    // 内置接受

    // ---- 1. 基线:无覆盖层,分发器用内置 GW_2022 ----
    FrameDispatcher* d = new FrameDispatcher();
    check("baseline: dispatcher has statistics", d->statistics() != nullptr);
    d->connect_source(&src);
    emit src.frame_ready(bad, 0);
    check("baseline: bad frame parsed", wait_total(d, 1));
    check("baseline: builtin handled it (plugin calls 0)",
          FakePluginParser::s_calls.load() == 0);
    check("baseline: builtin rejected bad CRC (dropped 1)", dropped_of(d) == 1);

    // ---- 2. 注册到达 + 采集中:不重建,运行中的分发器继续用内置 ----
    check("register: shadow GW_2022 by probe-plugin",
          register_parser_provider(QStringLiteral("GW_2022"),
                                   QStringLiteral("探针插件"),
                                   QStringLiteral("Probe Plugin"),
                                   []() { return std::make_unique<FakePluginParser>(); },
                                   200, QStringLiteral("probe-plugin")));
    emit src.frame_ready(bad, 0);
    check("capturing: second bad frame parsed", wait_total(d, 2));
    check("capturing: no switch mid-capture (plugin calls still 0)",
          FakePluginParser::s_calls.load() == 0);
    check("capturing: still builtin (dropped 2)", dropped_of(d) == 2);

    // ---- 3. 注册到达 + 空闲:重建,新分发器取到插件解析器 ----
    d = rebuild(d, src);
    check("idle-switch: dispatcher rebuilt", d->statistics() != nullptr);
    emit src.frame_ready(bad, 0);
    check("idle-switch: bad frame parsed by new dispatcher", wait_total(d, 1));
    check("idle-switch: plugin took over (plugin calls 1)",
          FakePluginParser::s_calls.load() == 1);
    check("idle-switch: plugin accepted (dropped 0)", dropped_of(d) == 0);

    // ---- 4. 注销:按归属摘除覆盖层 → 重建回退内置,下一帧正常解析 ----
    check("unregister: override removed by owner",
          unregister_parser_for_owner(QStringLiteral("GW_2022"),
                                      QStringLiteral("probe-plugin")));
    d = rebuild(d, src);
    check("fallback: dispatcher rebuilt", d->statistics() != nullptr);
    emit src.frame_ready(bad, 0);
    check("fallback: bad frame parsed by rebuilt dispatcher", wait_total(d, 1));
    check("fallback: plugin no longer called (calls still 1)",
          FakePluginParser::s_calls.load() == 1);
    check("fallback: builtin back (dropped 1)", dropped_of(d) == 1);
    emit src.frame_ready(good, 0);
    check("fallback: next good frame parsed", wait_total(d, 2));
    check("fallback: good frame accepted by builtin (dropped still 1)",
          dropped_of(d) == 1);
    check("fallback: plugin still not called (calls still 1)",
          FakePluginParser::s_calls.load() == 1);

    delete d;
    appcfg::set_protocol(old_protocol);
    if (g_fails) {
        std::printf("plugin_unregister_fallback_probe: FAILURES (%d failures)\n", g_fails);
        return 1;
    }
    std::printf("RESULT: ALL PASS (0 failures)\n");
    return 0;
}

#include "plugin_unregister_fallback_probe.moc"
