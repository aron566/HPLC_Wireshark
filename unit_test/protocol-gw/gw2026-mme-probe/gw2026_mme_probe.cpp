// gw2026_mme_probe.cpp
// GW_2026 MMe 字段树回归:直接测 GW_2026_MsduParser::parse 的三个 V6 修复。
//   ① 表83 无线信道冲突上报(0x0080):每邻居 2B(ch + Option),不是 1B;
//   ② MMType 16 位分发:0x010A(高字节非 0)不得被当 0x000A 深解析;
//   ③ 表46 关联确认路由表:代理条目 rel_start 累加前序代理的子站点字节(变长)。
// 全部合成帧写真实 MSDU CRC32;CRC FAIL 会清空字段树(msdu_crc_gate),故必须正确。
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include "bplcframe.h"
#include "fieldtools.h"
#include "crc.h"
#include "gw_protocol/gw_2026/gw_2026_msdu_parser.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

static QByteArray u32le(quint32 v) {
    QByteArray b(4, 0);
    b[0] = char(v & 0xFF);
    b[1] = char((v >> 8) & 0xFF);
    b[2] = char((v >> 16) & 0xFF);
    b[3] = char((v >> 24) & 0xFF);
    return b;
}

// 标准 16B MSDU 头 + MSDU 数据区(payload) + 尾 4B CRC32。
// msdu_type=0(NET_MANAGEMENT);payload = MMe_BASE(4B: MMType 2B LE + RSV 2B) + MMe 体。
static QByteArray mme_msdu(quint16 mm_type, const QByteArray& mme_body) {
    QByteArray payload(4, 0);
    payload[0] = char(mm_type & 0xFF);        // MMType 低字节
    payload[1] = char((mm_type >> 8) & 0xFF); // MMType 高字节
    payload[2] = 0;                           // RSV
    payload[3] = 0;                           // RSV
    payload.append(mme_body);

    QByteArray hdr(16, 0);
    quint8* h = reinterpret_cast<quint8*>(hdr.data());
    set_bits(h, 0, 4, 12, 1);          // src_tei=1 (CCO)
    set_bits(h, 2, 0, 12, 5);          // dst_tei=5
    set_bits(h, 5, 0, 16, 1);          // seq=1
    h[7] = 0;                          // msdu_type = NET_MANAGEMENT
    set_bits(h, 8, 0, 11, payload.size());

    const quint32 crc = crc32_le(reinterpret_cast<const quint8*>(payload.constData()),
                                 payload.size() + 4);
    return hdr + payload + u32le(crc);
}

// 递归收集树中 name 前缀匹配的节点引用(按遍历序)。
static void collect(const QVector<MsduFieldNode>& nodes, const QString& prefix,
                    QVector<const MsduFieldNode*>& out) {
    for (const MsduFieldNode& n : nodes) {
        if (n.name.startsWith(prefix)) out.append(&n);
        collect(n.children, prefix, out);
    }
}
static const MsduFieldNode* find_first(const QVector<MsduFieldNode>& nodes,
                                       const QString& prefix) {
    for (const MsduFieldNode& n : nodes) {
        if (n.name.startsWith(prefix)) return &n;
        if (const MsduFieldNode* r = find_first(n.children, prefix)) return r;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ── ① 表83 无线信道冲突上报(0x0080):每邻居 2B(ch + Option) ──
    // MMe 体: CCO MAC(6B) + 邻居数(1B) + 每邻居 2B。
    // 邻居0 ch=5/opt=1,邻居1 ch=9/opt=2。旧代码每邻居 1B → 邻居1 ch 会错读成 opt0。
    {
        QByteArray body(11, 0);
        body[6] = 2;                    // NeighbourNetWorkCount = 2
        body[7] = 5;  body[8]  = 1;     // 邻居0: ch=5 opt=1
        body[9] = 9;  body[10] = 2;     // 邻居1: ch=9 opt=2
        const MsduInfo m = GW_2026_MsduParser::parse(mme_msdu(0x0080, body));
        check("t83: present", m.present);
        check("t83: summary 无线信道冲突上报", m.summary.contains(QStringLiteral("无线信道冲突上报")));
        QVector<const MsduFieldNode*> nbs;
        collect(m.tree, QStringLiteral("Neighbour["), nbs);
        check("t83: 2 neighbours parsed", nbs.size() == 2);
        check("t83: neighbour[0] ch=5 opt=1",
              nbs.size() == 2 && nbs[0]->value == QStringLiteral("ch=5 opt=1"));
        check("t83: neighbour[1] ch=9 opt=2 (not Option顶替)",
              nbs.size() == 2 && nbs[1]->value == QStringLiteral("ch=9 opt=2"));
        check("t83: neighbour[1] rel_len==2",
              nbs.size() == 2 && nbs[1]->rel_len == 2);
    }

    // ── ② MMType 16 位分发:0x010A 高字节非 0 → 未知,不得深解析为 0x000A ──
    // 0x000A = MME_NETWORK_CONFLICT_REPORT(网络冲突上报);0x010A = 未定义。
    {
        const MsduInfo m = GW_2026_MsduParser::parse(mme_msdu(0x010A, QByteArray(16, 0)));
        check("mmtype: 0x010A present", m.present);
        check("mmtype: 0x010A mme_type==0x010A (16bit, 非 0x000A)",
              m.mme_type == 0x010A);
        check("mmtype: 0x010A summary Unknown(不深解析为网络冲突上报)",
              m.summary.contains(QStringLiteral("Unknown")));
        // 0x010A 不得产出「网络冲突上报」的深解析字段(CCOMACAddr 等)
        check("mmtype: 0x010A 无网络冲突深解析字段",
              find_first(m.tree, QStringLiteral("NeighbourNetworkNIDList")) == nullptr);

        // 0x000A 正常帧仍正确深解析
        const MsduInfo m2 = GW_2026_MsduParser::parse(mme_msdu(0x000A, QByteArray(16, 0)));
        check("mmtype: 0x000A mme_type==0x000A", m2.mme_type == 0x000A);
        check("mmtype: 0x000A summary 网络冲突上报",
              m2.summary.contains(QStringLiteral("网络冲突上报")));
    }

    // ── ③ 表46 关联确认路由表:代理 rel_start 累加前序代理子站点字节 ──
    // 固定头 40B + RouteInfo(8B 头) + 直连站点(0) + 2 代理。
    // 代理0: child=2(2 子站点,各 2B);代理1: child=0。
    // rel_start 基准 rel0 = head_size(16) + 4(MMe_BASE) + 40(固定头) = 60。
    // 表46 变长: 代理[i] 起点 = rel0 + 8 + 2*N + Σ_{k<i}(4 + 2*Mk)。
    // N=sta_sum=0,M0=2 → PCO[1].rel_start = 60 + 8 + 0 + (4 + 2*2) = 76。
    // 旧代码 i*4 → 60 + 8 + 0 + 4 = 72(漏代理0 的 2×2B 子站点)。
    {
        const int head_size = 16;
        const int rel0 = head_size + 4 + 40;   // = 60
        QByteArray body(40 + 8 + 8 + 4, 0);    // 固定40 + 路由头8 + 代理0(4+2*2) + 代理1(4)
        quint8* b = reinterpret_cast<quint8*>(body.data());
        // RouteInfo 头: sta_sum=0, pco_sum=2, route_sz, rsv
        set_bits(b, 40, 0, 16, 0);   // 直连发现站点数 = 0
        set_bits(b, 42, 0, 16, 2);   // 直连代理站点数 = 2
        // 代理0(off=48): tei=0x20A, plink=0, child=2;子站点 0x301/0x302
        set_bits(b, 48, 0, 12, 0x20A);   // 代理0 TEI
        set_bits(b, 49, 4, 1, 0);        // 链路类型
        set_bits(b, 50, 0, 16, 2);       // 子站点数 = 2
        set_bits(b, 52, 0, 12, 0x301);   // 子站点0 TEI
        set_bits(b, 54, 0, 12, 0x302);   // 子站点1 TEI
        // 代理1(off=56): tei=0x40B, plink=0, child=0
        set_bits(b, 56, 0, 12, 0x40B);   // 代理1 TEI
        set_bits(b, 57, 4, 1, 0);        // 链路类型
        set_bits(b, 58, 0, 16, 0);       // 子站点数 = 0

        const MsduInfo m = GW_2026_MsduParser::parse(mme_msdu(0x0001, body));
        check("t46: present", m.present);
        check("t46: summary 关联确认", m.summary.contains(QStringLiteral("关联确认")));
        QVector<const MsduFieldNode*> pcos;
        collect(m.tree, QStringLiteral("PCO["), pcos);
        check("t46: 2 PCO parsed", pcos.size() == 2);
        if (pcos.size() == 2) {
            const int want_pco1 = rel0 + 8 + 2 * 0 + 4 + 2 * 2;   // = 76
            std::printf("  [rel_start] PCO[0]=%d PCO[1]=%d (期望 PCO[1]=%d)\n",
                        pcos[0]->rel_start, pcos[1]->rel_start, want_pco1);
            check("t46: PCO[1].rel_start == rel0+8+2N+4+2*M0 (变长累加)",
                  pcos[1]->rel_start == want_pco1);
            check("t46: PCO[0].rel_start == rel0+8 (首个代理无前序)",
                  pcos[0]->rel_start == rel0 + 8);
            // 子节点 rel_start 随之正确:代理1 无子站点,代理0 有 2 子站点
            QVector<const MsduFieldNode*> children;
            collect(pcos[0]->children, QStringLiteral("Child["), children);
            check("t46: PCO[0] 有 2 子站点", children.size() == 2);
            if (children.size() == 2) {
                // 子站点0 rel_start = pco0.rel_start + 4 + 0 = 68 + 4 = 72
                check("t46: Child[0].rel_start == pco0.rel_start+4",
                      children[0]->rel_start == pcos[0]->rel_start + 4);
            }
        }
    }

    std::printf(g_fails ? "RESULT: %d FAIL\n" : "RESULT: ALL PASS\n", g_fails);
    return g_fails ? 1 : 0;
}
