// 第 2 批修复回归探针(SPEC-batch2):
//   ① 完整 2 站 OFDMA → Info 不得出现「截断」「重组失败」,应为 CRC OK
//   ② 多站真截断 → 不整帧拒收,已完整站点数据保留,文案为「缺块 k」
//   ③ 南网 SPLC SOF(version=2、raw[4] bit0/bit1 各种组合)→ 不得标成 DL-OFDMA/训练帧
//   ④ learn_tei_mac 4 步序列(1→A、2→A、2→B、1→A)→ MAC 反查 = STA-1、MAC 过滤命中
//   ⑤ 历史条目快照 MAC 不随 TEI 易主漂移;回填值用 [MAC*:..] 标注区分
#include <QCoreApplication>
#include <QByteArray>
#include <QThreadPool>
#include <QVector>
#include <cstdio>
#include <cstring>

#include "bplcframe.h"
#include "packetlistmodel.h"
#include "gw_2026_sof_parser.h"
#include "gw_2026_tree.h"
#include <QTreeWidgetItem>
#include <vector>
#include "gw_2022_sof_parser.h"
#include "nw_2021_parser.h"
#include "fieldtools.h"
#include "crc.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

static void put_crc24(quint8* blk, int len) {
    const quint32 calc = crc24_lsb(blk, len);
    blk[len - 3] = quint8(calc & 0xFF);
    blk[len - 2] = quint8((calc >> 8) & 0xFF);
    blk[len - 1] = quint8((calc >> 16) & 0xFF);
}
static quint64 mac6(quint8 a, quint8 b, quint8 c, quint8 d, quint8 e, quint8 f) {
    return (quint64)a | ((quint64)b << 8) | ((quint64)c << 16)
         | ((quint64)d << 24) | ((quint64)e << 32) | ((quint64)f << 40);
}

// 多站 OFDMA 帧构造(V6 表10/11:每站恒 1 块)。seqs 仅以 size() 定站点数,
// 块头 seq 恒 0、START|END 恒置(0xC0),体填充 = 站点索引。
// cut_last=true 时把最后一站的块从帧里截掉(真截断)
static QByteArray gw_ofdma_seq(const QVector<QVector<int>>& seqs, bool cut_last,
                               int copy_mode = 3) {
    const int sz = (copy_mode == 2) ? 136 : 72;   // 表13 分集拷贝模式:2→136B,3→72B
    const int n_sites = seqs.size();               // 站点数(V6 表10/11 每站恒 1 块,无每站 PB 个数)
    QByteArray f(32 + n_sites * sz, 0);
    quint8* p = reinterpret_cast<quint8*>(f.data());
    set_bits(p, 4, 0, 12, 1);          // src TEI = CCO
    set_bits(p, 5, 4, 12, 27);         // dst TEI = 站0 TEI(表10 (5,4,12))
    set_bits(p, 7, 0, 3, 2);           // 帧子类型 2 = OFDMA 下行(表7)
    set_bits(p, 7, 3, 2, n_sites - 1); // 站点数编码(表10 (7,3,2))
    set_bits(p, 8, 0, 12, 100);        // 帧长
    set_bits(p, 11, 3, 5, copy_mode);  // 站0 分集拷贝模式(表10 (11,3,5));每站恒 1 块
    quint8* e = p + 16;                // eFC(表11)
    if (n_sites > 1) {
        set_bits(e, 0, 0, 12, 42);     // 站1 TEI(表11 (16,0,12))
        set_bits(e, 2, 0, 5, copy_mode);// 站1 分集拷贝模式(表11 (18,0,5))
    }
    // eFC FCS -> bytes 29..31:无 FCS 会被 eFC CRC24 门整帧拒收(877f838 引入;
    // 与 build_gw2026_ofdma_tda_sim.py 的 build_ofdma() 同款 store_crc24(f, 16, 16))
    // 写在 PB 循环前:截断(cut_last)只截 PB 块,头 32B 始终完整。
    put_crc24(p + 16, 16);
    int off = 32;
    for (int i = 0; i < n_sites; ++i) {
        if (cut_last && i == n_sites - 1) {
            f.truncate(off);            // 最后一站块不在帧内
            return f;
        }
        quint8* b = reinterpret_cast<quint8*>(f.data()) + off;
        // 每站单块:seq 恒 0(单块 pb_num=1,seq 只能 0),START|END 恒置(0xC0);
        // 体填充 = 站点索引,供用例按首字节区分站点。
        quint8 h = 0xC0;
        b[0] = h;
        std::memset(b + 1, quint8(i & 0xFF), size_t(sz - 4));  // 体填充 = 站点索引
        put_crc24(b, sz);
        off += sz;
    }
    return f;
}

// 顺序帧构造(旧用例复用):每站 seq 0..blocks[i]-1
// V6 表10/11 每站恒 1 块,blocks[i] 语义已失效——此包装不再使用,直接调 gw_ofdma_seq。

static PacketEntry gw_entry(const MpduInfo& mp, const QByteArray& raw, quint8 version) {
    PacketEntry e;
    e.accepted   = true;
    e.protocol   = kProtocolGw2026;
    e.mpdu       = mp;
    e.mpdu.frame_type = 1;      // SOF
    e.mpdu.net_id = 1;
    e.mpdu.version = version;   // 0=HDC1.0 / 1=HDC2.0(均不得影响分支判据)
    e.meta.is_rf = false;
    e.raw_bytes  = raw;
    return e;
}

static QString info_text(PacketListModel& m, int row) {
    return m.data(m.index(row, PacketListModel::COL_INFO), Qt::DisplayRole).toString();
}

// ── ①/② OFDMA 多站(V6 表10/11 每站恒 1 块) ──
static void test_ofdma(PacketListModel& model, int& row) {
    // V6 表10/11 明文「每个站点载荷数据只支持 1 个物理块」:OFDMA 每站恒 1 块。
    // 站内多块乱序/uniform/oob 场景由单站数据帧(表7 (11,0,2))承载,见 pb-reorder-probe。
    struct Case { QVector<QVector<int>> seqs; bool cut; const char* tag; };
    const Case cases[] = {
        {QVector<QVector<int>>{{0}, {0}}, false, "1blk"},
        {QVector<QVector<int>>{{0}, {0}}, false, "2blk"},   // 2 站各 1 块(=「2blk」历史名)
        {QVector<QVector<int>>{{0}, {0}}, true,  "cut"},
    };

    for (const Case& c : cases) {
        const QByteArray raw = gw_ofdma_seq(c.seqs, c.cut);
        MpduInfo info; MsduState msdu; QByteArray complete;
        const QString err = gw_2026_sof::assemble(raw, info, msdu, complete, 0, false);
        const bool full = QString::fromLatin1(c.tag) != QLatin1String("cut");
        char buf[160];
        std::snprintf(buf, sizeof(buf), "ofdma/%s: not rejected (accept)", c.tag);
        check(buf, err.isEmpty());
        std::snprintf(buf, sizeof(buf), "ofdma/%s: 2 stations parsed", c.tag);
        check(buf, info.sof_stations.size() == 2);
        std::snprintf(buf, sizeof(buf), "ofdma/%s: station0 data preserved", c.tag);
        check(buf, !info.sof_stations.isEmpty()
                   && info.sof_stations[0].pb_crc_oks.size() == 1);   // 每站恒 1 块
        std::snprintf(buf, sizeof(buf), "ofdma/%s: frame pb_crc_ok == %d", c.tag, full ? 1 : 0);
        check(buf, info.pb_crc_ok == full);

        for (quint8 v : {quint8(0), quint8(1)}) {
            model.append_packets(QVector<PacketEntry>{gw_entry(info, raw, v)});
            const QString t = info_text(model, row++);
            std::snprintf(buf, sizeof(buf), "ofdma/%s v%u: no 'PB block exceeds'/truncated", c.tag, v);
            check(buf, !t.contains(QStringLiteral("PB 块超出帧长"))
                       && !t.contains(QStringLiteral("（截断）")));
            if (full) {
                std::snprintf(buf, sizeof(buf), "ofdma/%s v%u: no reassembly-failed", c.tag, v);
                check(buf, !t.contains(QStringLiteral("重组失败")));
                std::snprintf(buf, sizeof(buf), "ofdma/%s v%u: shows CRC OK", c.tag, v);
                check(buf, t.contains(QStringLiteral("CRC OK")));
                std::snprintf(buf, sizeof(buf), "ofdma/%s v%u: labeled DL-OFDMA", c.tag, v);
                check(buf, t.contains(QStringLiteral("DL-OFDMA")));
            } else {
                std::snprintf(buf, sizeof(buf), "ofdma/%s v%u: shows missing-PB text", c.tag, v);
                check(buf, t.contains(QStringLiteral("缺块")));
            }
        }
    }
}

// ── ③ 南网 SPLC SOF 不得被标成 DL-OFDMA/训练帧 ──
static void test_nw_not_ofdma(PacketListModel& model, int& row) {
    for (int b4 = 0; b4 < 4; ++b4) {
        PacketEntry e;
        e.accepted = true;
        e.protocol = kProtocolNw2026;
        e.mpdu.frame_type = 1;          // SOF
        e.mpdu.version = 2;             // 旧判据陷阱值(南网 SPLC)
        e.mpdu.net_id = 1;
        e.mpdu.src_tei = 5;
        e.mpdu.dst_tei = 1;
        e.mpdu.pb_num = 2;
        e.mpdu.pb_size = 136;
        e.mpdu.tmi = 3;
        e.mpdu.pb_crc_ok = true;
        e.mpdu.pb_heads = QVector<quint8>{0x00, 0x01};
        e.mpdu.pb_crc_oks = QVector<bool>{true, true};
        e.meta.is_rf = false;
        QByteArray raw(64, '\0');
        raw[4] = char(quint8(b4));      // bit0/bit1 各种组合
        e.raw_bytes = raw;
        e.msdu_body = QByteArray(64, 'x');
        model.append_packets(QVector<PacketEntry>{e});
        const QString t = info_text(model, row++);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "nw/raw[4]=0x%02x: not DL-OFDMA", b4);
        check(buf, !t.contains(QStringLiteral("DL-OFDMA")));
        std::snprintf(buf, sizeof(buf), "nw/raw[4]=0x%02x: not training frame", b4);
        check(buf, !t.contains(QStringLiteral("训练帧")));
        std::snprintf(buf, sizeof(buf), "nw/raw[4]=0x%02x: shows CRC OK", b4);
        check(buf, t.contains(QStringLiteral("CRC OK")));
    }
}

// ── ④ learn_tei_mac 早退修复 ──
static PacketEntry learn_entry(quint16 tei, quint64 mac) {
    PacketEntry e;
    e.accepted = true;
    e.protocol = kProtocolNw2026;
    e.mpdu.frame_type = 1;
    e.mpdu.net_id = 1;
    e.msdu.present = true;
    TeiMacPair p; p.tei = tei; p.mac = mac;
    e.msdu.tei_mac_pairs.append(p);
    return e;
}
static void test_learn_rev_index() {
    const quint64 A = mac6(0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa);
    PacketListModel model;
    model.append_packets(QVector<PacketEntry>{
        learn_entry(1, A), learn_entry(2, A), learn_entry(2, mac6(2,2,2,2,2,2)),
        learn_entry(1, A),
    });
    // 观察点 A:原始目的 TEI=0 + 报文体 sta_mac → 反向索引反查显示 STA-N
    PacketEntry obs;
    obs.accepted = true;
    obs.protocol = kProtocolNw2026;
    obs.mpdu.frame_type = 1;
    obs.mpdu.net_id = 1;
    obs.mpdu.dst_tei = 0;
    obs.msdu.present = true;
    obs.msdu.msdu_dst_tei = 0;
    obs.msdu.sta_mac = A;
    // 观察点 B:仅 TEI 的原始源(TEI=1,无 MAC)——MAC 过滤经反向索引展开为
    // sta-1 等值谓词后应命中;早退缺陷下 rev[A] 缺失 → 展开为空 → 0 命中。
    PacketEntry obs_filt;
    obs_filt.accepted = true;
    obs_filt.protocol = kProtocolNw2026;
    obs_filt.mpdu.frame_type = 1;
    obs_filt.mpdu.net_id = 1;
    obs_filt.msdu.present = true;
    obs_filt.msdu.msdu_src_tei = 0;
    TeiMacPair only_tei; only_tei.tei = 1; only_tei.mac = 0;
    obs_filt.msdu.orig_srcs.append(only_tei);
    model.append_packets(QVector<PacketEntry>{obs, obs_filt});
    const QString t = model.data(model.index(4, PacketListModel::COL_ORIG_DST),
                                 Qt::DisplayRole).toString();
    check("learn: MAC A reverse lookup == STA-1* (inferred, starred)",
          t.startsWith(QStringLiteral("STA-1*")));
    check("learn: MAC A shown with mac", t.contains(QStringLiteral("aa:aa:aa:aa:aa:aa")));
    model.set_display_filter(QStringLiteral("aa:aa:aa:aa:aa:aa"));
    check("learn: MAC pattern filter matches (rev index expands to sta-1)",
          model.passes_filter(obs_filt));
    model.set_display_filter(QStringLiteral("sta-1"));
    check("learn: sta-1 filter matches same frame", model.passes_filter(obs_filt));
}

// ── ⑤ 历史条目 MAC 快照 vs 回填标注 ──
static PacketEntry dst_entry(quint16 tei, quint64 mac) {
    PacketEntry e;
    e.accepted = true;
    e.protocol = kProtocolNw2026;
    e.mpdu.frame_type = 1;
    e.mpdu.net_id = 1;
    e.msdu.present = true;
    e.msdu.msdu_dst_tei = 0;        // 帧级无原始目的 → 走多目的分支
    TeiMacPair p; p.tei = tei; p.mac = mac;
    e.msdu.orig_dsts.append(p);
    return e;
}
static void test_snapshot_vs_backfill() {
    const quint64 A = mac6(0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa);
    const quint64 B = mac6(0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb);
    PacketListModel model;
    // 行 0:快照(解析时已带 MAC);行 1:TEI-only(仅 TEI,绘制时回填)
    model.append_packets(QVector<PacketEntry>{dst_entry(7, A), dst_entry(7, 0)});
    auto col = [&](int row) {
        return model.data(model.index(row, PacketListModel::COL_ORIG_DST),
                          Qt::DisplayRole).toString();
    };
    model.append_packets(QVector<PacketEntry>{learn_entry(7, A)});
    check("snap: row0 snapshot MAC", col(0) == QStringLiteral("STA-7 [aa:aa:aa:aa:aa:aa]"));
    check("snap: row1 backfilled marked [MAC*:...]",
          col(1).startsWith(QStringLiteral("STA-7 [MAC*:aa:aa:aa:aa:aa:aa]")));
    // TEI7 易主给 B
    model.append_packets(QVector<PacketEntry>{learn_entry(7, B)});
    check("snap: row0 does NOT drift after TEI takeover",
          col(0) == QStringLiteral("STA-7 [aa:aa:aa:aa:aa:aa]"));
    check("snap: row1 (backfilled) tracks current mapping, still marked",
          col(1).startsWith(QStringLiteral("STA-7 [MAC*:bb:bb:bb:bb:bb:bb]")));
}

// ── ⑥ 跳数列 COL_SOURCE / COL_DEST 的推断 MAC 标注 ──
static void test_hop_columns() {
    const quint64 A = mac6(0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa);
    PacketListModel model;
    model.append_packets(QVector<PacketEntry>{learn_entry(7, A)});
    PacketEntry hop;
    hop.accepted = true;
    hop.protocol = kProtocolNw2026;
    hop.mpdu.frame_type = 1;
    hop.mpdu.net_id = 1;
    hop.mpdu.src_tei = 7;
    hop.mpdu.dst_tei = 7;
    model.append_packets(QVector<PacketEntry>{hop});
    const QString src = model.data(model.index(1, PacketListModel::COL_SOURCE),
                                   Qt::DisplayRole).toString();
    const QString dst = model.data(model.index(1, PacketListModel::COL_DEST),
                                   Qt::DisplayRole).toString();
    check("hop: COL_SOURCE STA-N [MAC*:..]",
          src == QStringLiteral("STA-7 [MAC*:aa:aa:aa:aa:aa:aa]"));
    check("hop: COL_DEST STA-N [MAC*:..]",
          dst == QStringLiteral("STA-7 [MAC*:aa:aa:aa:aa:aa:aa]"));
}

// ── ⑦ MAC→TEI 推断标注:STA-N* / STA-X ──
static void test_rev_marks() {
    const quint64 A = mac6(0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa);
    const quint64 Z = mac6(0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a);
    PacketListModel model;
    model.append_packets(QVector<PacketEntry>{learn_entry(1, A)});
    PacketEntry known;      // 反查命中 → N 是推断
    known.accepted = true;
    known.protocol = kProtocolNw2026;
    known.mpdu.frame_type = 1;
    known.mpdu.net_id = 1;
    known.msdu.present = true;
    known.msdu.msdu_dst_tei = 0;
    known.msdu.sta_mac = A;
    PacketEntry unknown = known;   // 反查落空 → STA-X(不加星)
    unknown.msdu.sta_mac = Z;
    model.append_packets(QVector<PacketEntry>{known, unknown});
    const QString k = model.data(model.index(1, PacketListModel::COL_ORIG_DST),
                                 Qt::DisplayRole).toString();
    const QString u = model.data(model.index(2, PacketListModel::COL_ORIG_DST),
                                 Qt::DisplayRole).toString();
    check("rev: STA-N* when reverse lookup hits",
          k == QStringLiteral("STA-1* [aa:aa:aa:aa:aa:aa]"));
    check("rev: STA-X (no star) when lookup misses",
          u == QStringLiteral("STA-X [5a:5a:5a:5a:5a:5a]"));
}

// ── ⑧ MMe 主体:TEI 来源决定是否加星 ──
static void test_mme_body_marks() {
    const quint64 A = mac6(0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa);
    const quint64 C = mac6(0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0);
    const quint64 S = mac6(0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
    PacketListModel model;
    model.append_packets(QVector<PacketEntry>{learn_entry(1, A)});
    // ① 主体 TEI 来自映射表反查 → STA-N*
    PacketEntry by_lookup;
    by_lookup.accepted = true;
    by_lookup.protocol = kProtocolNw2026;
    by_lookup.mpdu.frame_type = 1;
    by_lookup.mpdu.net_id = 1;
    by_lookup.msdu.present = true;
    by_lookup.msdu.sta_mac = A;          // 主体 MAC(帧内实证)
    by_lookup.msdu.msdu_src_mac = S;     // 本跳发送方不同 → 判代理转发
    // ② 主体 TEI 来自帧内 nodes → STA-N(实证,不加星)
    PacketEntry by_nodes = by_lookup;
    by_nodes.msdu.sta_mac = C;           // 映射表里没有的 MAC
    TeiMacPair node; node.tei = 7; node.mac = C;
    by_nodes.msdu.topo_event.nodes.append(node);
    model.append_packets(QVector<PacketEntry>{by_lookup, by_nodes});
    const QString t1 = model.data(model.index(1, PacketListModel::COL_MSDU_TYPE),
                                  Qt::DisplayRole).toString();
    const QString t2 = model.data(model.index(2, PacketListModel::COL_MSDU_TYPE),
                                  Qt::DisplayRole).toString();
    check("mme: body TEI from lookup -> STA-N*", t1.contains(QStringLiteral("STA-1*")));
    check("mme: empirical body MAC stays [MAC:..] (no star)",
          t1.contains(QStringLiteral("[MAC:aa:aa:aa:aa:aa:aa]"))
          && !t1.contains(QStringLiteral("[MAC*:aa:aa:aa:aa:aa:aa]")));
    check("mme: body TEI from in-frame nodes -> STA-N (empirical)",
          t2.contains(QStringLiteral("STA-7"))
          && !t2.contains(QStringLiteral("STA-7*")));
}

// ── ⑨ 落盘/重载后标注一致 ──
static void test_flush_reload() {
    const quint64 A = mac6(0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa);
    PacketListModel model;
    model.append_packets(QVector<PacketEntry>{dst_entry(7, A), dst_entry(7, 0)});
    model.append_packets(QVector<PacketEntry>{learn_entry(7, A)});
    // 追加超过一个盘块(kBlockSize=1000)的填充条目,促头部条目 flush 落盘
    QVector<PacketEntry> filler;
    filler.reserve(PacketListModel::kBlockSize + 8);
    for (int i = 0; i < PacketListModel::kBlockSize + 8; ++i) {
        PacketEntry f;
        f.accepted = true;
        f.protocol = kProtocolNw2026;
        f.mpdu.frame_type = 1;
        f.mpdu.net_id = 1;
        filler.append(f);
    }
    model.append_packets(filler);
    // 落盘在后台线程池执行(见 flush_hot_block):等待写完再读回,避免读到空块
    QThreadPool::globalInstance()->waitForDone();
    auto col = [&](int row) {
        return model.data(model.index(row, PacketListModel::COL_ORIG_DST),
                          Qt::DisplayRole).toString();
    };
    check("reload: snapshotted row still unstarred",
          col(0) == QStringLiteral("STA-7 [aa:aa:aa:aa:aa:aa]"));
    if (col(0) != QStringLiteral("STA-7 [aa:aa:aa:aa:aa:aa]"))
        std::printf("  (actual row0=[%s] row1=[%s] total=%lld)\n",
                    qPrintable(col(0)), qPrintable(col(1)),
                    (long long)model.total_count());
    check("reload: backfilled row still starred",
          col(1).startsWith(QStringLiteral("STA-7 [MAC*:aa:aa:aa:aa:aa:aa]")));
}

// ── ⑩ 南网解析闸门:重组不可信时不解析 MSDU(只留摘要) ──
// NW_2021 载波 SOF:byte7 = TMI(高 4b) | PBNum(低 4b);TMI=2 → 136B(表121)
// 块 = PB头4B(表133,序列号 byte0-1 16bit LE)+ 体 128B + 保留 1B + CRC24 3B
static QByteArray nw_sof_frame(const QVector<int>& seqs, int declared) {
    const int pbsz = 136;
    QByteArray f(16 + seqs.size() * pbsz, 0);
    quint8* p = reinterpret_cast<quint8*>(f.data());
    p[0] = 0x01;                                       // SOF
    p[7] = quint8((3 << 4) | (declared & 0x0F));       // TMI=3 → 136B;PBNum(band0 上限 4)
    set_bits(p, 8, 0, 12, 100);                        // 帧长
    put_crc24(p, 16);                                  // FCH CRC24(表19:末 3B)
    for (int i = 0; i < seqs.size(); ++i) {
        quint8* b = reinterpret_cast<quint8*>(f.data()) + 16 + i * pbsz;
        b[0] = quint8(seqs[i] & 0xFF);                 // 序列号低 8 位(表133)
        b[1] = quint8((seqs[i] >> 8) & 0x0F);          // 序列号高 4 位
        b[2] = 0x00;                                   // 非聚合
        std::memset(b + 4, quint8(seqs[i] & 0xFF), size_t(pbsz - 8));
        put_crc24(b, pbsz);
    }
    return f;
}

static void test_nw_parse_gate() {
    auto run = [](const QByteArray& raw, NW_2021_Parser::Result& out) {
        NW_2021_Parser parser;
        ParseFilter filt;
        BplcFrame fr;
        fr.meta.from_raw = true;
        fr.meta.is_rf = false;
        fr.data = QByteArray(1, char(0)) + raw;   // 裸 hex 形态:1B 媒介头
        MsduState msdu;
        out = parser.parse(fr, msdu, filt);
    };
    struct Case { QVector<int> seqs; int declared; const char* tag; bool ok; };
    const QVector<Case> cases{
        {{0, 1, 2, 3}, 4, "ordered", true},
        {{0, 1, 1, 3}, 4, "dup-seq", false},
        {{0, 1, 2, 9}, 4, "oob-seq", false},
        {{0, 2, 3}, 4, "missing", false},
    };
    // 让候选 MSDU 载荷非空(msdu_len=8)且 CRC32 必然 FAIL:填充载荷(0x00×8)之上的
    // 存储 CRC 区也是 0x00,crc32_le 遍历 8B 得非零 ⇒ stored(0)!=calc(非零) ⇒ FAIL。
    // (修了生产 payload_len<=0 假通过后,此处为真实校验,不再靠空载荷"碰巧"得结果。)
    auto force_msdu_crc_fail = [](QByteArray& raw) {
        // 块0 体起始 = 帧偏移 base16 + PB头4 = 20;msdu_len 在体[2..3] = 帧[22..23](16bit LE)
        raw[16 + 4 + 2] = char(0x08);   // msdu_len = 8
        raw[16 + 4 + 3] = char(0x00);
    };
    for (const Case& c : cases) {
        QByteArray raw = nw_sof_frame(c.seqs, c.declared);
        if (!c.ok) force_msdu_crc_fail(raw);   // 序号异常帧:候选 MSDU CRC32 确实 FAIL
        NW_2021_Parser::Result r;
        run(raw, r);
        if (!r.accept || r.mpdu.pb_num != c.declared)
            std::printf("  [dbg %s] accept=%d reason=[%s] pb_num=%u pb_size=%u oks=%d\n",
                        c.tag, r.accept ? 1 : 0, qPrintable(r.reject_reason),
                        unsigned(r.mpdu.pb_num), unsigned(r.mpdu.pb_size),
                        int(r.mpdu.pb_crc_oks.size()));
        char buf[150];
        std::snprintf(buf, sizeof(buf), "nw-gate/%s: parsed frames accepted", c.tag);
        check(buf, r.accept);
        std::snprintf(buf, sizeof(buf), "nw-gate/%s: pb_crc_ok == %d", c.tag, c.ok ? 1 : 0);
        check(buf, r.mpdu.pb_crc_ok == c.ok);
        if (c.ok) {
            std::snprintf(buf, sizeof(buf), "nw-gate/%s: MSDU parsed (no 'not parsed' summary)", c.tag);
            check(buf, !r.msdu.summary.contains(QStringLiteral("未解析")));
        } else {
            // 用户 2026-11 裁决:MSDU CRC 正确才显示原摘要,否则为空并只留 CRC 证据节点。
            // 序号异常(缺块/重复冲突/越界)时候选体仍产出 `MSDU CRC32 … FAIL` ⇒
            // msdu_crc_gate() 清空摘要、树只留证据节点、present=true(内容不可采信)。
            std::snprintf(buf, sizeof(buf), "nw-gate/%s: MSDU summary cleared (CRC FAIL)", c.tag);
            check(buf, r.msdu.summary.isEmpty());
            std::snprintf(buf, sizeof(buf), "nw-gate/%s: tree only CRC FAIL evidence", c.tag);
            check(buf, r.msdu.tree.size() == 1
                      && r.msdu.tree.first().name == QStringLiteral("MSDU CRC32")
                      && r.msdu.tree.first().value.contains(QStringLiteral("FAIL")));
            std::snprintf(buf, sizeof(buf), "nw-gate/%s: MSDU present (content untrusted)", c.tag);
            check(buf, r.msdu.present);
        }
    }

    // 兜底 A:序号异常 + msdu_len=0(空载荷)⇒ 生产已修 payload_len<=0 → N/A(不产 OK/FAIL)
    //   ⇒ crc_state==N/A && body_parseable==false ⇒ 仍走「PB 序号异常未解析」+ 树空(老路径)。
    {
        QByteArray raw = nw_sof_frame({0, 0, 2, 3}, 4);   // dup-seq:reassembly_ok=false,msdu_len 保持 0
        NW_2021_Parser::Result r;
        run(raw, r);
        char buf[150];
        std::snprintf(buf, sizeof(buf), "nw-gate/no-crc-zero: MSDU stays 'not parsed'");
        check(buf, r.msdu.summary == QStringLiteral("PB 序号异常未解析"));
        std::snprintf(buf, sizeof(buf), "nw-gate/no-crc-zero: tree empty");
        check(buf, r.msdu.tree.isEmpty());
    }
    // 兜底 B:序号异常 + msdu_len=0xFFFF(越过体尾)⇒ append_msdu_crc 判「校验数据缺失」→ N/A ⇒ 同上。
    {
        QByteArray raw = nw_sof_frame({0, 0, 2, 3}, 4);   // dup-seq
        // 块0 体起始 = 帧偏移 base16 + PB头4 = 20;msdu_len 在体[2..3] = 帧[22..23]
        raw[16 + 4 + 2] = char(0xFF);
        raw[16 + 4 + 3] = char(0xFF);
        NW_2021_Parser::Result r;
        run(raw, r);
        char buf[150];
        std::snprintf(buf, sizeof(buf), "nw-gate/no-crc-overflow: MSDU stays 'not parsed'");
        check(buf, r.msdu.summary == QStringLiteral("PB 序号异常未解析"));
        std::snprintf(buf, sizeof(buf), "nw-gate/no-crc-overflow: tree empty");
        check(buf, r.msdu.tree.isEmpty());
    }
}

// ── ⑪ GW 多站逐站序列号落位(P2 修复;V6 表10/11 每站恒 1 块) ──
static void test_gw_multi_reorder() {
    auto asm_one = [](const QByteArray& raw, MpduInfo& info) {
        MsduState msdu;
        QByteArray cp;
        return gw_2026_sof::assemble(raw, info, msdu, cp, 0, false);
    };
    // V6 表10/11 明文「每个站点载荷只支持 1 个物理块」:多站 OFDMA 每站恒 1 块,
    // 站内不再有「乱序/重复/越界」概念(单块 seq 恒 0)。站内多块乱序/uniform/oob
    // 由单站数据帧(表7 (11,0,2),1-4 块)承载,见 pb-reorder-probe(test_reorder_core
    // /test_oob/test_uniform)。本节验证多站每站单块的核心行为。
    {   // 2 站各 1 块:站0 体=0x00,站1 体=0x01(经块体首字节区分)
        const QByteArray raw = gw_ofdma_seq(QVector<QVector<int>>{{0}, {1}}, false);
        MpduInfo info;
        const QString err = asm_one(raw, info);
        check("gw-multi/2sta: accept", err.isEmpty());
        check("gw-multi/2sta: 2 stations", info.sof_stations.size() == 2);
        const int blen = 72 - 4;   // 块体 = pb_size - PB头1B - CRC24 3B
        const bool st0_ok = info.sof_stations.size() == 2
                            && info.sof_stations[0].msdu.size() == blen
                            && quint8(info.sof_stations[0].msdu[0]) == 0;
        check("gw-multi/2sta: st0 body (seq0 fill)", st0_ok);
        const bool st1_ok = info.sof_stations.size() == 2
                            && info.sof_stations[1].msdu.size() == blen
                            && quint8(info.sof_stations[1].msdu[0]) == 1;
        check("gw-multi/2sta: st1 body (seq1 fill)", st1_ok);
        check("gw-multi/2sta: frame pb_crc_ok true", info.pb_crc_ok);
        PacketListModel model;
        model.append_packets(QVector<PacketEntry>{gw_entry(info, raw, 1)});
        const QString t = info_text(model, 0);
        check("gw-multi/2sta: Info shows CRC OK", t.contains(QStringLiteral("CRC OK")));
    }
    // 截断:站1 块缺失 → 帧级 pb_crc_ok false(空站1 已 append,故 2 站含空站)
    {
        const QByteArray raw = gw_ofdma_seq(QVector<QVector<int>>{{0}, {0}}, true);
        MpduInfo info;
        const QString err = asm_one(raw, info);
        check("gw-multi/cut: accept (st0 kept)", err.isEmpty());
        check("gw-multi/cut: 2 stations (st1 empty)", info.sof_stations.size() == 2);
        check("gw-multi/cut: frame pb_crc_ok false", !info.pb_crc_ok);
    }
}

// ── 国网单站 SOF 构帧(2 块 × 72B,PB 头 1B + 体 68B + CRC24 3B) ──
// bad0/bad1 = 破坏对应块 CRC(翻转 CRC 末字节),用于验证 span 门
static QByteArray gw_single_fill(quint8* f, int sz, int n, const QByteArray* bs,
                                 const bool* bads) {
    int off = 16;
    for (int i = 0; i < n; ++i) {
        quint8* b = f + off;
        quint8 h = quint8(i & 0x3F);
        if (i == 0) h |= 0x40;              // START
        if (i == n - 1) h |= 0x80;          // END
        b[0] = h;
        std::memcpy(b + 1, bs[i].constData(), size_t(qMin(bs[i].size(), sz - 4)));
        put_crc24(b, sz);
        if (bads[i]) b[sz - 1] = quint8(b[sz - 1] ^ 0xFF);
        off += sz;
    }
    return QByteArray();
}

// GW2026 单站数据帧(表7 子类型=0,分集拷贝模式 3 → 72B,band0,2 块)
static QByteArray gw_single_2blk_2026(const QByteArray& b0, bool bad0,
                                      const QByteArray& b1, bool bad1) {
    const int sz = 72, n = 2;
    QByteArray f(16 + n * sz, 0);
    quint8* p = reinterpret_cast<quint8*>(f.data());
    set_bits(p, 4, 0, 12, 1);          // src TEI
    set_bits(p, 5, 4, 12, 27);         // dst TEI
    set_bits(p, 7, 0, 3, 0);           // 帧子类型 0 = 数据帧(表7)
    set_bits(p, 8, 0, 12, 100);        // 帧长
    set_bits(p, 10, 0, 2, 0);          // 载荷子载波加载模式 0 = 静态(表7 (10,0,2))
    set_bits(p, 10, 2, 5, 3);          // 载荷调制参数 3 = 分集拷贝模式 → 72B(表13)
    set_bits(p, 11, 0, 2, 1);          // PB 个数 2bit(表7 (11,0,2)),1 = 2 块
    set_bits(p, 11, 4, 3, 0);          // band 0(gw_2026_pb_num_table[0][3]=4)
    const QByteArray bs[2] = {b0, b1};
    const bool bads[2] = {bad0, bad1};
    gw_single_fill(p, sz, n, bs, bads);
    return f;
}

// GW2022 单站载波 SOF(表19:PB 个数(9,4,4)、TMI(11,4,4)=13 → 72B,band0,2 块)
static QByteArray gw_single_2blk_2022(const QByteArray& b0, bool bad0,
                                      const QByteArray& b1, bool bad1) {
    const int sz = 72, n = 2;
    QByteArray f(16 + n * sz, 0);
    quint8* p = reinterpret_cast<quint8*>(f.data());
    set_bits(p, 4, 0, 12, 1);          // src TEI
    set_bits(p, 5, 4, 12, 27);         // dst TEI
    set_bits(p, 7, 0, 8, 5);           // 链路标识符
    set_bits(p, 8, 0, 12, 100);        // 帧长
    set_bits(p, 9, 4, 4, 2);           // 物理块个数 = 2
    set_bits(p, 11, 4, 4, 13);         // TMI 13 → 72B
    set_bits(p, 12, 0, 4, 0);          // TMI_EXT = 0
    const QByteArray bs[2] = {b0, b1};
    const bool bads[2] = {bad0, bad1};
    gw_single_fill(p, sz, n, bs, bads);
    return f;
}

// ── ⑫ 国网 MSDU span 口径回归(P1) ──
// 曾误用南网公式(长度取体[2..3] 16bit LE、标准头 32B/12B),两向都错:
//   简头帧 span 被算长 → 误拒;标准头帧拿体[2..3] 垃圾当长度 → 误放行坏块字节。
// 用例一律用**国网头布局**构造(83d3efc 用南网布局造体流,所以错口径也全 PASS)。
static void test_gw_span_caliber() {
    // 国网简头:体[0] 低 nibble = 0x01,len 在体[2..3] 低 11bit
    auto simple = [](int len) {
        QByteArray b(68, 0);
        b[0] = 0x01;
        b[2] = char(len & 0xFF);
        b[3] = char((len >> 8) & 0x07);
        return b;
    };
    // 国网标准头:len 在体[8..9] 低 11bit;体[11] bit3 = MACAddrFlag(0→16B,1→28B)
    auto std_hdr = [](int len, bool flag28) {
        QByteArray b(68, 0);
        b[0] = 0x02;                       // 低 nibble != 0x01 → 标准头
        b[8] = char(len & 0xFF);
        b[9] = char((len >> 8) & 0x07);
        b[11] = char(flag28 ? 0x08 : 0x00);
        return b;
    };
    // 交付口径(用户 2026-11-13 定):assemble 只要求序列号重组完整就交付体流,
    // 首个 MSDU span 是否避开坏块不再是交付门——span 是否干净经出参透传调用方,
    // 仅在 MSDU CRC32 不可用时作 PB 回退(见 msdu_crc_allows_learning)。
    // 故本节断言两层:体流一律交付(重组完整),span_clean 按 span/坏块位置分档。
    {   // GW2026
        QByteArray out;
        MpduInfo info;
        MsduState msdu;
        bool span_clean = false;
        // A. 简头 len=60 → span 4+60+4=68,正好落在块0(68B)内,坏块在块1 → 交付且 span 干净
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2026_sof::assemble(gw_single_2blk_2026(simple(60), false, simple(0), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span26/A simple-head span 68 in good blk0 -> delivered, span clean",
              out.size() == 136 && span_clean
              && info.pb_crc_oks.size() == 2 && !info.pb_crc_ok);
        // A'. 同上但坏块换成块0 → span 68 > 0 → 交付但 span 不干净
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2026_sof::assemble(gw_single_2blk_2026(simple(60), true, simple(0), false),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span26/A' simple-head span into bad blk0 -> delivered, span dirty",
              out.size() == 136 && !span_clean);
        // B. 标准头 len=60 → span 16+60+4=80 > 68 → 跨入坏块1 → 交付但 span 不干净
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2026_sof::assemble(gw_single_2blk_2026(std_hdr(60, false), false,
                                                  std_hdr(0, false), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span26/B std-head span 80 into bad blk1 -> delivered, span dirty",
              out.size() == 136 && !span_clean);
        // B'. 标准头 len=20 → span 40 ≤ 68 → 落在块0 → 交付且 span 干净
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2026_sof::assemble(gw_single_2blk_2026(std_hdr(20, false), false,
                                                  std_hdr(0, false), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span26/B' std-head span 40 in good blk0 -> delivered, span clean",
              out.size() == 136 && span_clean);
        // C. 28B 头 + len=40 → span 28+40+4=72 > 68 → 跨入坏块1 → 交付但 span 不干净
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2026_sof::assemble(gw_single_2blk_2026(std_hdr(40, true), false,
                                                  std_hdr(0, true), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span26/C std-head28 span 72 into bad blk1 -> delivered, span dirty",
              out.size() == 136 && !span_clean);
    }
    {   // GW2022(同款口径)
        QByteArray out;
        MpduInfo info;
        MsduState msdu;
        bool span_clean = false;
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2022_sof::assemble(gw_single_2blk_2022(simple(60), false, simple(0), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span22/A simple-head span 68 in good blk0 -> delivered, span clean",
              out.size() == 136 && span_clean);
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2022_sof::assemble(gw_single_2blk_2022(simple(60), true, simple(0), false),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span22/A' simple-head span into bad blk0 -> delivered, span dirty",
              out.size() == 136 && !span_clean);
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2022_sof::assemble(gw_single_2blk_2022(std_hdr(60, false), false,
                                                  std_hdr(0, false), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span22/B std-head span 80 into bad blk1 -> delivered, span dirty",
              out.size() == 136 && !span_clean);
        out.clear();
        info = MpduInfo();
        span_clean = false;
        gw_2022_sof::assemble(gw_single_2blk_2022(std_hdr(20, false), false,
                                                  std_hdr(0, false), true),
                              info, msdu, out, 0, false, &span_clean);
        check("gw-span22/B' std-head span 40 in good blk0 -> delivered, span clean",
              out.size() == 136 && span_clean);
    }
}

// ── ⑬ GW 多站学习门:到达序 CRC × seq 序体流**配对**回归 ──
// V6 表10/11 每站恒 1 块:站内不再有「乱序到达」,配对门退化为平凡语义
// (单块 CRC 好 → 可学;坏 → 不可学)。站内多块乱序配对由单站数据帧承载,
// 见 pb-reorder-probe test_single_bad_crc_span_gate(单站 2 块坏 CRC span 门)。
static void test_gw_multi_station_pairing() {
    const int sz = 72;                     // 分集拷贝模式 3 → 72B(表13)
    const auto make_frame = [&](bool bad_st0) {
        QByteArray f = gw_ofdma_seq(QVector<QVector<int>>{{0}, {0}}, false, 3);
        quint8* p = reinterpret_cast<quint8*>(f.data());
        // 站0 块体首字节写简头(0x01),证明 station_msdu_parseable 走国网口径
        quint8* body_st0 = p + 32 + 1;
        body_st0[0] = 0x01;                // 简头标记
        body_st0[1] = 0;
        body_st0[2] = 8;                   // len=8 → span 4+8+4=16 ≤ 68 块体
        body_st0[3] = 0;
        put_crc24(p + 32, sz);             // 站0 重算 CRC(体改过)
        if (bad_st0) p[32 + sz - 1] = quint8(p[32 + sz - 1] ^ 0xFF);  // 坏站0 块
        return f;
    };
    const auto run = [&](bool bad_st0, bool want_parseable, const char* label) {
        const QByteArray raw = make_frame(bad_st0);
        MpduInfo info; MsduState msdu; QByteArray cp;
        const QString err = gw_2026_sof::assemble(raw, info, msdu, cp, 0, false);
        const QByteArray lbl(label);
        check((lbl + ": assemble ok").constData(), err.isEmpty());
        check((lbl + ": 2 stations").constData(), info.sof_stations.size() == 2);
        if (info.sof_stations.size() != 2) return;
        const SofStationPb st0 = info.sof_stations.value(0);
        check((lbl + ": st0 single block").constData(),
              st0.pb_heads.size() == 1 && st0.pb_crc_oks.size() == 1);
        check((lbl + ": st0 head START|END single block").constData(),
              quint8(st0.pb_heads[0]) == 0xC0);
        // 单块:CRC 好 → 可学,坏 → 不可学(配对门平凡语义)
        check((lbl + (want_parseable ? ": station_msdu_parseable == true (learn)"
                                     : ": station_msdu_parseable == false (no learn)")).constData(),
              gw_2026_sof::station_msdu_parseable(st0) == want_parseable);
        check((lbl + ": frame pb_crc_ok follows st0").constData(),
              info.pb_crc_ok == !bad_st0);
        // 站1 单块全好 → 始终可学
        check((lbl + ": station1 (all good) parseable").constData(),
              gw_2026_sof::station_msdu_parseable(info.sof_stations.value(1)));
    };
    run(true,  false, "pairing/bad=st0");
    run(false, true,  "pairing/good");
}

// ── H1:OFDMA/TDA eFC 截断(帧长 < 32B)不得越界读 ──
// 帧放在**恰好等长**的堆缓冲(QByteArray::fromRawData),ASan 下任何越界读都会报错。
static void test_gw_efc_truncated() {
    for (quint8 dt : {quint8(2), quint8(3)}) {          // 2=OFDMA 3=TDA
        for (quint8 sn = 1; sn <= 3; ++sn) {             // 站点数码 1-3(读 eFC 站点 1-3)
            bool all_rej = true, no_sta = true;
            for (int len = 16; len < 32; ++len) {
                std::vector<quint8> buf(size_t(len), 0xFF);   // 0xFF:字段全 1,最大化读取范围
                quint8* p = buf.data();
                set_bits(p, 7, 0, 3, dt);
                set_bits(p, 7, 3, 2, sn);
                set_bits(p, 11, 3, 5, 3);                     // 站0 分集拷贝 3 → 72B
                const QByteArray raw = QByteArray::fromRawData(
                    reinterpret_cast<const char*>(p), len);
                MpduInfo info; MsduState msdu; QByteArray complete;
                const QString err = gw_2026_sof::assemble(raw, info, msdu, complete, 0, false);
                if (err.isEmpty()) all_rej = false;
                if (!info.sof_stations.isEmpty()) no_sta = false;
                // 字段树(被拒帧同样会在 UI 中渲染)
                PacketEntry e = gw_entry(info, raw, 1);
                e.accepted = false;
                e.mpdu.sof_data_type = dt;
                e.mpdu.sof_tf_symbol_pos = sn;
                QTreeWidgetItem root;
                GW_2026_TreeBuilder().build(&root, e);
            }
            char name[96];
            std::snprintf(name, sizeof(name), "efc-trunc/%s sn=%u: rejected len 16..31",
                          dt == 2 ? "ofdma" : "tda", sn);
            check(name, all_rej);
            std::snprintf(name, sizeof(name), "efc-trunc/%s sn=%u: no stations", dt == 2 ? "ofdma" : "tda", sn);
            check(name, no_sta);
        }
    }
    // 恰好 32B(eFC 完整、无 PB)不应被 eFC 门拒绝为「截断」
    {
        std::vector<quint8> buf(32, 0);
        set_bits(buf.data(), 7, 0, 3, 2);
        set_bits(buf.data(), 7, 3, 2, 1);
        set_bits(buf.data(), 11, 3, 5, 3);
        set_bits(buf.data() + 16, 2, 0, 5, 3);
        // eFC FCS -> bytes 29..31:上游 877f838 起 eFC CRC24 错整帧拒收,完整 eFC 须带 FCS
        put_crc24(buf.data() + 16, 16);
        const QByteArray raw = QByteArray::fromRawData(reinterpret_cast<const char*>(buf.data()), 32);
        MpduInfo info; MsduState msdu; QByteArray complete;
        const QString err = gw_2026_sof::assemble(raw, info, msdu, complete, 0, false);
        check("efc-trunc/ofdma len=32: not rejected as eFC-truncated",
              !err.contains(QStringLiteral("eFC")));
        // 同一帧翻 eFC FCS 一位 ⇒ eFC CRC24 门整帧拒收,且不留站点
        buf[29] ^= 0x01;
        MpduInfo info2; MsduState msdu2; QByteArray complete2;
        const QString err2 = gw_2026_sof::assemble(raw, info2, msdu2, complete2, 0, false);
        check("efc-crc/ofdma len=32 bad eFC FCS: rejected, no stations",
              err2.contains(QStringLiteral("CRC24")) && info2.sof_stations.isEmpty());
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    {
        PacketListModel model;
        int row = 0;
        test_ofdma(model, row);
        test_nw_not_ofdma(model, row);
    }
    test_learn_rev_index();
    test_snapshot_vs_backfill();
    test_hop_columns();
    test_rev_marks();
    test_mme_body_marks();
    test_flush_reload();
    test_nw_parse_gate();
    test_gw_multi_reorder();
    test_gw_span_caliber();
    test_gw_multi_station_pairing();
    test_gw_efc_truncated();
    std::printf("\nRESULT: %s (%d failures)\n",
                g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
