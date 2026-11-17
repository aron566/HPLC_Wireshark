/// 探针:路由变更表 event_mac_text 的 mac_to_tei 回退(修法 fix/topo-asof-mac-fallback)
/// 背景:ChangeProxyCnf 体里无 MAC,事件不学 TEI=1;信标/逐帧路径 note_restart_origin
/// 学到的 TEI→MAC 只进 TopoState::mac_to_tei,从不进渲染用的时点映射 asof,故路由表
/// 里 TEI=1 显示"未知"。修法:asof 查不到时回退查 mac_to_tei,查到写回 asof 一次。
/// 覆盖:
///   T0 前置:逐帧学习只进 mac_to_tei、不建 nodes(修法的前提);nodes/mac_of() 查不到,
///      故回退必须扫反向表,不能走 mac_of()
///   T1 asof 为空、mac_to_tei 里有 CCO 映射时,换代理事件行显示该 CCO MAC(修前显示"未知")
///   T2 回退后 asof 里有了该条目,后续同 TEI 行直接用 asof(不再回退也同文)
///   T3 两边都没有的 TEI 仍显示"未知"(不谎报)
///   T4 帧内直带 MAC 优先于回退(不被实时映射覆盖);event_mac_text 本身不学帧内映射进 asof
///   T5 回退写回的条目参与后续行的 leaves 反查(离线指示只给 MAC 也能解析出 TEI)
///   T6 st 为空时不回退、不崩溃,仍显示"未知"
/// 用法:./build.sh 编译;QT_QPA_PLATFORM=offscreen ./build/asof_mac_fallback_probe
///      exit 0 = 全部断言通过
#include <QApplication>
#include <cstdio>

#include "topo_window.cpp"  // 匿名命名空间里的 event_mac_text / advance_mac_asof

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else { std::printf("ok: %s\n", msg); } } while (0)

// MAC u64 约定(与仓库一致):低字节存首个帧内字节;format_mac 按低字节在前打印
static quint64 mac_u64(int a, int b, int c, int d, int e, int f) {
    const quint8 byte[6] = { (quint8)a, (quint8)b, (quint8)c,
                             (quint8)d, (quint8)e, (quint8)f };
    quint64 v = 0;
    for (int i = 0; i < 6; ++i) v |= quint64(byte[i]) << (8 * i);
    return v;
}

static const quint32 kNid = 0x0E;
static const quint64 kCcoMac = mac_u64(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
static const quint64 kStaMac = mac_u64(0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb);
static const quint64 kOtherMac = mac_u64(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);

static TopoEvent change_proxy(quint16 child_tei, quint16 proxy_tei, qint64 frame_idx) {
    TopoEvent e;
    e.kind = TopoEventKind::ChangeProxyCnf;
    e.nid = kNid;
    e.restart_count = -1;
    e.routes.append({child_tei, proxy_tei});
    e.epoch_ms = 2000 + frame_idx;
    e.frame_index = frame_idx;
    return e;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // T0:逐帧路径学映射(与 MainWindow 逐帧块同一调用):首帧只建基线,不报重启。
    TopoState st;
    st.nid = kNid;
    st.note_restart_origin(kNid, 1, kCcoMac, 0, 1000, 1, false, true);
    st.note_restart_origin(kNid, 5, kStaMac, 0, 1001, 2, false, true);
    CHECK(st.mac_to_tei.value(TopoState::nid_mac_key(kNid, kCcoMac), 0) == 1,
          "T0 mac_to_tei 有 (NID,CCO MAC)->1");
    CHECK(st.mac_to_tei.value(TopoState::nid_mac_key(kNid, kStaMac), 0) == 5,
          "T0 mac_to_tei 有 (NID,STA MAC)->5");
    CHECK(!st.nodes.contains(1) && !st.nodes.contains(5),
          "T0 逐帧学习不建 nodes(TEI=1/5 均无节点)");
    CHECK(st.mac_of(1) == 0 && st.mac_of(5) == 0,
          "T0 mac_of() 查不到(回退不能走 nodes,必须扫 mac_to_tei)");

    const QString cco_txt = QStringLiteral("TEI=1 ") + format_mac(kCcoMac);
    const QString sta_txt = QStringLiteral("TEI=5 ") + format_mac(kStaMac);

    // T1:asof 为空,换代理事件行(子 5,代理 1)回退显示 CCO 与 STA 的 MAC。
    QHash<quint16, quint64> asof;
    const QString t1 = event_mac_text(change_proxy(5, 1, 10), &st, asof);
    CHECK(t1 == cco_txt + QStringLiteral("; ") + sta_txt, "T1 回退显示 CCO/STA MAC");
    // T2:回退写回了 asof,后续行直接用 asof。
    CHECK(asof.value(1, 0) == kCcoMac && asof.value(5, 0) == kStaMac,
          "T2 回退后 asof 里有了 TEI=1/5 条目");
    const QString t2 = event_mac_text(change_proxy(5, 1, 11), &st, asof);
    CHECK(t2 == t1, "T2 后续行文案不变");

    // T3:两边都没有的 TEI 仍显示"未知"。
    const QString t3 = event_mac_text(change_proxy(9, 1, 12), &st, asof);
    CHECK(t3 == cco_txt + QStringLiteral("; TEI=9 ") + trl::L("未知"),
          "T3 未知 TEI 仍显示未知");

    // T4:帧内直带 MAC 优先于回退;event_mac_text 本身不把帧内映射学进 asof。
    TopoEvent e4 = change_proxy(1, 5, 13);
    e4.nodes.append({1, kOtherMac});
    const QString t4 = event_mac_text(e4, &st, asof);
    CHECK(t4 == QStringLiteral("TEI=1 ") + format_mac(kOtherMac)
              + QStringLiteral("; ") + sta_txt, "T4 帧内直带 MAC 优先于回退");
    CHECK(asof.value(1, 0) == kCcoMac,
          "T4 event_mac_text 不把帧内映射学进 asof(仍为回退值)");

    // T5:回退写回的条目参与后续行的 leaves 反查。
    QHash<quint16, quint64> asof2;
    event_mac_text(change_proxy(5, 1, 20), &st, asof2);
    TopoEvent e5;
    e5.kind = TopoEventKind::LeaveInd;
    e5.nid = kNid;
    e5.restart_count = -1;
    e5.leaves.append(kCcoMac);
    e5.epoch_ms = 2020;
    e5.frame_index = 21;
    const QString t5 = event_mac_text(e5, &st, asof2);
    CHECK(t5 == cco_txt, "T5 离线指示只给 MAC 也能解析出 TEI=1");

    // T6:st 为空时不回退、不崩溃。
    QHash<quint16, quint64> asof3;
    const QString t6 = event_mac_text(change_proxy(5, 1, 30), nullptr, asof3);
    CHECK(t6 == QStringLiteral("TEI=1 ") + trl::L("未知") + QStringLiteral("; TEI=5 ")
              + trl::L("未知"), "T6 st 为空时显示未知");
    CHECK(asof3.isEmpty(), "T6 st 为空时 asof 不写回");

    // T7:同一 NID、同一 TEI、两个不同 MAC 且不建节点 → 回退多义返回 0、显示"未知"、恒定。
    //     index_mac 的旧键清理在 `if (nit != nodes.end())` 之内,无节点时旧键残留,
    //     故两次 note_restart_origin(同 TEI 不同 MAC)会留下 twins=2 个 mac_to_tei 键。
    TopoState st2;
    st2.nid = kNid;
    st2.note_restart_origin(kNid, 5, kStaMac, 0, 2000, 1, false, true);
    st2.note_restart_origin(kNid, 5, kOtherMac, 0, 2001, 2, false, true);
    CHECK(st2.mac_to_tei.value(TopoState::nid_mac_key(kNid, kStaMac), 0) == 5,
          "T7 mac_to_tei (NID,STA MAC)->5");
    CHECK(st2.mac_to_tei.value(TopoState::nid_mac_key(kNid, kOtherMac), 0) == 5,
          "T7 mac_to_tei (NID,OTHER MAC)->5");
    CHECK(!st2.nodes.contains(5), "T7 同 TEI 两 MAC 不建节点(twins=2 nodes=0)");
    CHECK(mac_from_state_index(&st2, kNid, 5) == 0, "T7 多义回退返回 0");
    for (int i = 0; i < 8; ++i)
        CHECK(mac_from_state_index(&st2, kNid, 5) == 0, "T7 连续调用返回值恒定(0)");
    QHash<quint16, quint64> asof7;
    const QString t7 = event_mac_text(change_proxy(5, 1, 40), &st2, asof7);
    CHECK(t7.contains(QStringLiteral("TEI=5 ") + trl::L("未知")), "T7 多义 TEI 显示未知");

    // T7b:另一个 NID 下同号 TEI 的存在不得影响本 NID 的判定(NID 隔离)。
    TopoState st3;
    st3.nid = kNid;
    st3.note_restart_origin(kNid, 5, kStaMac, 0, 3000, 1, false, true);
    st3.note_restart_origin(0x0F, 5, kOtherMac, 0, 3001, 2, false, true);
    CHECK(mac_from_state_index(&st3, kNid, 5) == kStaMac, "T7b NID 隔离:本 NID 唯一仍返回");
    CHECK(mac_from_state_index(&st3, 0x0F, 5) == kOtherMac, "T7b 另一 NID 唯一返回其 MAC");

    // T8:单一映射时仍必须回退成功(防止为消歧义一刀切返回 0)。
    TopoState st4;
    st4.nid = kNid;
    st4.note_restart_origin(kNid, 1, kCcoMac, 0, 4000, 1, false, true);
    CHECK(mac_from_state_index(&st4, kNid, 1) == kCcoMac, "T8 唯一映射仍回退成功");

    if (failures) {
        std::printf("FAILED: %d\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
