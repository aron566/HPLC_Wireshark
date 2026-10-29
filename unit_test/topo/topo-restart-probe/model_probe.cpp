/// 探针:PacketListModel —— 8 项 body_tei 反查 + 9 项反向索引(评审 8/9 项修复验证)
/// 用法:./model_probe ; exit 0 = 全部断言通过
#include "packetlistmodel.h"
#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else { std::printf("ok: %s\n", msg); } } while (0)

static const quint64 MAC_A = 0xAABBCCDDEEFFULL; // 主体 STA
static const quint64 MAC_C = 0x001122334455ULL; // 接管 TEI=78 的新 STA
static const quint64 MAC_X = 0x66778899AABBULL; // 无关节点
static const quint64 MAC_RELAY = 0x1234567890ABULL; // 本跳发送方(中继)

static PacketEntry learn_entry(int idx, int tei, quint64 mac) {
    PacketEntry e;
    e.index = idx; e.accepted = true;
    e.mpdu.net_id = 11;
    e.msdu.present = true;
    e.msdu.msdu_src_tei = tei;
    e.msdu.msdu_src_mac = mac;
    return e;
}

static PacketEntry display_entry(int idx, quint64 sta_mac) {
    PacketEntry e;
    e.index = idx; e.accepted = true;
    e.mpdu.net_id = 11;
    e.mpdu.src_tei = 2;
    e.msdu.present = true;
    e.msdu.summary = QStringLiteral("MMeAssocInd");
    e.msdu.sta_mac = sta_mac;
    e.msdu.msdu_src_mac = MAC_RELAY; // ≠主体 ⇒ 触发中继后缀展示
    e.msdu.topo_event.nodes.append({2, MAC_X}); // 首个非零 TEI=2,不是主体
    return e;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    PacketListModel m;

    // P1: 学习 78→A 后,主体为 A 的多节点帧应显示 STA-78(而非首个非零 STA-2)
    m.append_packet(learn_entry(1, 78, MAC_A));
    m.append_packet(display_entry(2, MAC_A));
    QString s1 = m.data(m.index(1, PacketListModel::COL_MSDU_TYPE)).toString();
    std::printf("row1 MSDU_TYPE: %s\n", qPrintable(s1));
    CHECK(s1.contains(QStringLiteral("STA-78")), "P1 主体反查显示 STA-78");
    CHECK(!s1.contains(QStringLiteral("STA-2 ")), "P1 不再取首个非零 STA-2");

    // P2: TEI=78 易主给 C 后,主体 A 的反查须失效(旧反向条目已清),回落首个非零 STA-2
    m.append_packet(learn_entry(3, 78, MAC_C));
    m.append_packet(display_entry(4, MAC_A));
    QString s2 = m.data(m.index(3, PacketListModel::COL_MSDU_TYPE)).toString();
    std::printf("row3 MSDU_TYPE: %s\n", qPrintable(s2));
    CHECK(s2.contains(QStringLiteral("STA-2")), "P2 TEI 易主后旧 MAC 反查失效回落 STA-2");
    CHECK(!s2.contains(QStringLiteral("STA-78")), "P2 不再显示 stale 的 STA-78");

    // P3: MAC_A 易 TEI 到 79 后,反查跟随最新值 STA-79
    m.append_packet(learn_entry(5, 79, MAC_A));
    m.append_packet(display_entry(6, MAC_A));
    QString s3 = m.data(m.index(5, PacketListModel::COL_MSDU_TYPE)).toString();
    std::printf("row5 MSDU_TYPE: %s\n", qPrintable(s3));
    CHECK(s3.contains(QStringLiteral("STA-79")), "P3 MAC 易 TEI 后反查跟随最新 STA-79");

    CHECK(m.rowCount() == 6, "P4 行数 6 行");
    std::printf(failures ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
