/// @file topo_event_shot.cpp
/// @brief 合成 TopoEvent 经 parse(frame) 的 frame.topoEvent 喂给 js_topo,
///        渲染截图验证(对齐原版 TOPO 页面)。
/// 覆盖: discoverList(含 upRoutes RouteType=3 父节点)、assocReq(入网中)、
/// assocCnf(入网+父节点)、changeProxyCnf(换代理)、leaveInd(离线)、
/// successRate(成功率)、双 NID 隔离、hover tooltip、
/// 帧记录双击 → host.jumpToFrame 回调。
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QImage>
#include <cstdio>

#include "plugin_backend.h"
#include "js_backend.h"
#include "../plugin_api/plugin_manifest.h"
#include "bplcframe.h"  // TopoEvent / TopoEventKind

// MAC u64 约定(与仓库一致):低字节存首个帧内字节
static quint64 mac_u64(int a, int b, int c, int d, int e, int f) {
    const quint8 byte[6] = { (quint8)a, (quint8)b, (quint8)c,
                             (quint8)d, (quint8)e, (quint8)f };
    quint64 v = 0;
    for (int i = 0; i < 6; ++i) v |= quint64(byte[i]) << (8 * i);
    return v;
}

static TopoEvent make_evt(TopoEventKind kind, quint32 nid, qint64 frame_idx) {
    TopoEvent e;
    e.kind = kind;
    e.nid = nid;
    e.frame_index = frame_idx;
    e.epoch_ms = 1700000000000LL + frame_idx * 1000;
    e.is_rf = false;
    e.restart_count = -1;
    return e;
}

// 单入口:事件挂在 BplcFrame.topo_event 上,经 parse(frame) 投递
static void feed_evt(JsBackend& b, const TopoEvent& e) {
    BplcFrame f;
    f.decoded_index = e.frame_index;
    f.decoded_epoch_ms = e.epoch_ms;
    f.topo_event = e;
    MsduState msdu;
    ParseFilter filter;
    QString err;
    const ParseResult r = b.parse(f, msdu, filter, &err);
    if (!err.isEmpty() || !r.accept)
        printf("feed evt %d frame %lld: %s\n", (int)e.kind,
               (long long)e.frame_index, qPrintable(err));
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCommandLineParser p;
    p.addOption(QCommandLineOption(QStringList() << "examples", "examples dir", "dir"));
    p.addOption(QCommandLineOption(QStringList() << "out", "output png", "png"));
    p.process(app);

    PluginManifest m = read_plugin_manifest(p.value("examples") + "/js_topo");
    if (!m.error.isEmpty()) { printf("manifest: %s\n", qPrintable(m.error)); return 1; }
    QString err;
    JsBackend b;
    if (!b.initialize(m, &err)) { printf("init: %s\n", qPrintable(err)); return 1; }

    // host.jumpToFrame 回调探针(验证帧记录双击联动)
    qint64 jumped_to = -1;
    b.set_host_jump_callback([&jumped_to](qint64 idx) { jumped_to = idx; });

    const quint32 NID1 = 0x1234;
    const quint32 NID2 = 0x5678;
    const quint64 CCO_MAC = mac_u64(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01);

    // 1) discoverList: CCO + STA2(父=CCO) + STA3(父=STA2,经 upRoutes)
    {
        TopoEvent e = make_evt(TopoEventKind::DiscoverList, NID1, 100);
        e.cco_mac = CCO_MAC;
        e.discover_src_tei = 2;
        e.restart_count = 5;
        e.nodes = { {2, mac_u64(0xAA,0xBB,0xCC,0xDD,0xEE,0x02)},
                    {3, mac_u64(0xAA,0xBB,0xCC,0xDD,0xEE,0x03)} };
        e.routes = { {2, 1} };
        e.up_routes = { {3, 2} };  // RouteType=3 已由解析层过滤
        e.neighbor_teis = {1, 3};
        feed_evt(b, e);
    }
    // 2) assocReq: 新 STA 正在入网(按 MAC)
    {
        TopoEvent e = make_evt(TopoEventKind::AssocReq, NID1, 200);
        e.nodes = { {0, mac_u64(0xAA,0xBB,0xCC,0xDD,0xEE,0x04)} };
        e.restart_count = 2;
        feed_evt(b, e);
    }
    // 在 assocReq 后、assocCnf 前渲染一张:验证 pending(入网中)节点
    // (图:层级1末尾、边连CCO;映射表: TEI "-", Joining, Level 1, Proxy 1)
    {
        QString rerr2;
        QImage pimg = b.render_graphics(1000, 700, &rerr2);
        QString pout = p.value("out");
        pout.replace(".png", "_pending.png");
        if (pimg.save(pout)) printf("saved: %s (pending)\n", qPrintable(pout));
    }
    // 3) assocCnf: STA4 入网,代理=STA2
    {
        TopoEvent e = make_evt(TopoEventKind::AssocCnf, NID1, 300);
        e.nodes = { {4, mac_u64(0xAA,0xBB,0xCC,0xDD,0xEE,0x04)} };
        e.routes = { {4, 2} };
        feed_evt(b, e);
    }
    // 4) changeProxyCnf: STA3 换代理 2->4
    {
        TopoEvent e = make_evt(TopoEventKind::ChangeProxyCnf, NID1, 400);
        e.routes = { {3, 4} };
        feed_evt(b, e);
    }
    // 5) successRate: STA2/STA3 上下行成功率
    {
        TopoEvent e = make_evt(TopoEventKind::SuccessRate, NID1, 500);
        e.comm_rates = { {2, 98, 95}, {3, 90, 88} };
        feed_evt(b, e);
    }
    // 6) leaveInd: STA4 离线
    {
        TopoEvent e = make_evt(TopoEventKind::LeaveInd, NID1, 600);
        e.leaves = { mac_u64(0xAA,0xBB,0xCC,0xDD,0xEE,0x04) };
        feed_evt(b, e);
    }
    // 7) 第二个 NID:独立网络
    {
        TopoEvent e = make_evt(TopoEventKind::DiscoverList, NID2, 700);
        e.cco_mac = mac_u64(0x11,0x22,0x33,0x44,0x55,0x01);
        e.discover_src_tei = 5;
        e.nodes = { {5, mac_u64(0x11,0x22,0x33,0x44,0x55,0x05)} };
        e.routes = { {5, 1} };
        feed_evt(b, e);
    }
    // 8) CCO 重启:restart_count 变化 -> 合成警告事件
    {
        TopoEvent e = make_evt(TopoEventKind::DiscoverList, NID1, 800);
        e.cco_mac = CCO_MAC;
        e.restart_count = 6;
        feed_evt(b, e);
    }


    QString rerr;
    QImage img = b.render_graphics(1000, 700, &rerr);
    if (img.isNull()) { printf("render: %s\n", qPrintable(rerr)); return 1; }
    QString out = p.value("out");
    if (!img.save(out)) { printf("save failed\n"); return 1; }
    printf("saved: %s (%dx%d)\n", qPrintable(out), img.width(), img.height());

    // 帧记录双击 → host.jumpToFrame: 1000x700 时底部记录区 y=518 起,
    // 首行(帧 100)在 y≈538,双击 (500,548) 应命中首行
    {
        QString err2;
        b.handle_graphics_event(GraphicsEvent{GraphicsEventType::MouseDblClick,
                                              500, 548, 1, 0, 0, 0, 0}, &err2);
        if (jumped_to == 100)
            printf("dblclick jump: OK (frame %lld)\n", (long long)jumped_to);
        else
            printf("dblclick jump: FAIL (got %lld, want 100)\n",
                   (long long)jumped_to);
    }

    // hover 一下 STA-2 节点再截一张(验证 tooltip)。节点位置需与脚本布局一致,
    // 这里简单地对全图做一次 move 扫不到就不强求:直接按压 NID 盒切换 NID2 截图。
    QString err2;
    // press NID box (x=46..156, y=5..25) -> open dropdown
    b.handle_graphics_event(GraphicsEvent{GraphicsEventType::MousePress,
                                          60, 15, 1, 0, 0, 0, 0}, &err2);
    QImage img2 = b.render_graphics(1000, 700, &rerr);
    QString out2 = out;
    out2.replace(".png", "_niddrop.png");
    img2.save(out2);
    printf("saved: %s\n", qPrintable(out2));

    // hover STA-2 (graph center-left column, ~2nd node) -> tooltip shot
    b.handle_graphics_event(GraphicsEvent{GraphicsEventType::MousePress,
                                          900, 600, 1, 0, 0, 0, 0}, &err2); // close dropdown
    b.handle_graphics_event(GraphicsEvent{GraphicsEventType::MouseMove,
                                          320, 212, 0, 0, 0, 0, 0}, &err2);
    QImage img3 = b.render_graphics(1000, 700, &rerr);
    QString out3 = out;
    out3.replace(".png", "_hover.png");
    img3.save(out3);
    printf("saved: %s\n", qPrintable(out3));

    // ---- 历史模式:单击帧 300 -> 冻结在 300(<=800 最新帧,进历史) ----
    // 300 时刻状态: STA4 已入网(代理=STA2);400 的换代理/600 的离网尚未发生
    b.notify_frame_selected(300, false);
    QImage img4 = b.render_graphics(1000, 700, &rerr);
    QString out4 = out;
    out4.replace(".png", "_hist300.png");
    img4.save(out4);
    printf("saved: %s (history @ #300)\n", qPrintable(out4));

    // ---- 双击最新帧 800 -> 强制历史(force=true),即使是最新帧 ----
    b.notify_frame_selected(800, true);
    QImage img5 = b.render_graphics(1000, 700, &rerr);
    QString out5 = out;
    out5.replace(".png", "_hist800force.png");
    img5.save(out5);
    printf("saved: %s (history @ #800 forced)\n", qPrintable(out5));

    // ---- 单击最新帧 800(非强制) -> 回到实时 ----
    b.notify_frame_selected(800, false);
    QImage img6 = b.render_graphics(1000, 700, &rerr);
    QString out6 = out;
    out6.replace(".png", "_backlive.png");
    img6.save(out6);
    printf("saved: %s (back to live)\n", qPrintable(out6));

    // ---- 历史模式下点 "Back to Live" 按钮(1000x700 时 x=880..990,y=5..25) ----
    // 先渲染一次(按钮热区由上次 render 确定,与真实面板 800ms 定时刷新一致)
    b.notify_frame_selected(300, false);
    QImage img7a = b.render_graphics(1000, 700, &rerr);
    b.handle_graphics_event(GraphicsEvent{GraphicsEventType::MousePress,
                                          935, 15, 1, 0, 0, 0, 0}, &err2);
    QImage img7 = b.render_graphics(1000, 700, &rerr);
    QString out7 = out;
    out7.replace(".png", "_btnlive.png");
    img7.save(out7);
    printf("saved: %s (Back to Live button)\n", qPrintable(out7));
    return 0;
}
