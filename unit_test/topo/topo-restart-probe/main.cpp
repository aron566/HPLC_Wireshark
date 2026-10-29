/// 探针:TopoState STA 重启检测 —— sender_mac==0 回落主体 MAC(评审 7 项修复验证)
/// 用法:./probe ; exit 0 = 全部断言通过
#include "topo_state.h"
#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else { std::printf("ok: %s\n", msg); } } while (0)

static TopoEvent assoc(quint64 sender, quint64 subject_mac, int restart, qint64 idx) {
    TopoEvent e;
    e.kind = TopoEventKind::AssocReq;
    e.restart_count = restart;
    e.sender_mac = sender;
    if (subject_mac) e.nodes.append({0, subject_mac});
    e.epoch_ms = 1000 + idx;
    e.frame_index = idx;
    return e;
}

static int count_restart_events(const TopoState& st) {
    int n = 0;
    for (const TopoEvent& ev : st.events)
        if (ev.kind == TopoEventKind::StaRestart) ++n;
    return n;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const quint64 MAC_A = 0xAABBCCDDEEFFULL; // 主体 STA,显示为 ff:ee:dd:cc:bb:aa
    const quint64 MAC_B = 0x112233445566ULL; // 发送方(中继),显示为 66:55:44:33:22:11

    // A: sender_mac==0 回落主体 MAC —— 基线建立 + 变化检出
    {
        TopoState st;
        st.apply(assoc(0, MAC_A, 2, 1));
        CHECK(count_restart_events(st) == 0, "A1 sender=0 首帧建基线不报重启");
        CHECK(st.pending.contains(MAC_A), "A2 sender=0 仍正常建 pending 节点");
        st.apply(assoc(0, MAC_A, 3, 2));
        CHECK(count_restart_events(st) == 1, "A3 sender=0 重启次数变化报 1 次 StaRestart");
        bool desc_ok = false;
        for (const TopoEvent& ev : st.events)
            if (ev.kind == TopoEventKind::StaRestart)
                desc_ok = ev.desc.contains(QStringLiteral("ff:ee:dd:cc:bb:aa"))
                       && ev.desc.contains(QStringLiteral("2→3"));
        CHECK(desc_ok, "A4 StaRestart 描述用主体 MAC 且次数 2→3");
        st.apply(assoc(0, MAC_A, 3, 3));
        CHECK(count_restart_events(st) == 1, "A5 次数不变不重复报");
    }
    // B: sender_mac 非零时仍按发送方建基线(52bd2eb0 口径不退化)
    {
        TopoState st;
        st.apply(assoc(MAC_B, MAC_A, 2, 1));
        st.apply(assoc(MAC_B, MAC_A, 2, 2));
        CHECK(count_restart_events(st) == 0, "B1 发送方基线次数不变不报");
        st.apply(assoc(MAC_B, MAC_A, 5, 3));
        CHECK(count_restart_events(st) == 1, "B2 发送方次数变化报 1 次");
        bool desc_ok = false;
        for (const TopoEvent& ev : st.events)
            if (ev.kind == TopoEventKind::StaRestart)
                desc_ok = ev.desc.contains(QStringLiteral("66:55:44:33:22:11"));
        CHECK(desc_ok, "B3 中继情形描述仍用发送方 MAC 而非主体");
    }
    // C: sender_mac==0 且 nodes 为空 —— 无键可建,静默跳过且不崩
    {
        TopoState st;
        st.apply(assoc(0, 0, 2, 1));
        st.apply(assoc(0, 0, 3, 2));
        CHECK(count_restart_events(st) == 0, "C 无源无主体时跳过且不崩");
    }

    std::printf(failures ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
