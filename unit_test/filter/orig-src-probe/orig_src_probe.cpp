// Orig Src multi-source probe.
// Usage: orig_src_probe <gw_topo_merge.bin>
//  A. Parser invariant on GW multi-station frames: merged orig_srcs must equal
//     dedup(direct station MSDU header sources, in station order).
//  B. Model column: multi-source display format, dedup, single-frame fallback,
//     rejected-frame empty, CCO naming.
//  C. Filter: sta- word match (incl. sta-1 vs sta-11 boundary), MAC-pattern
//     expansion to orig TEIs, substring path, negatives.
// Exit 0 iff every check passes.
#include <cstdio>
#include <cstdint>
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <QVector>
#include "bplcframe.h"
#include "iprotocolparser.h"
#include "gw_protocol/gw_2026/gw_2026_parser.h"
#include "gw_protocol/gw_2026/gw_2026_msdu_parser.h"
#include "packetlistmodel.h"

static int g_fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fails;
}
static quint64 mac6(quint8 b0, quint8 b1, quint8 b2, quint8 b3, quint8 b4, quint8 b5) {
    return quint64(b0) | (quint64(b1) << 8) | (quint64(b2) << 16)
         | (quint64(b3) << 24) | (quint64(b4) << 32) | (quint64(b5) << 40);
}
static QString srcs_str(const QVector<TeiMacPair>& v) {
    QStringList parts;
    for (const auto& p : v)
        parts << QString("%1:%2").arg(p.tei).arg(p.mac, 12, 16, QChar('0'));
    return parts.join(" ");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::fprintf(stderr, "usage: %s <gw_topo_merge.bin>\n", argv[0]); return 2; }
    FILE* in = std::fopen(argv[1], "rb");
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }

    // ---- A. Parser invariant on GW multi-station frames ----
    {
        GW_2026_Parser parser;
        ParseFilter filt;
        long idx = 0;
        for (;;) {
            unsigned char lb[4];
            if (std::fread(lb, 1, 4, in) != 4) break;
            uint32_t len = (uint32_t)lb[0] | ((uint32_t)lb[1] << 8)
                         | ((uint32_t)lb[2] << 16) | ((uint32_t)lb[3] << 24);
            QByteArray body((int)len, 0);
            if (len > 0 && std::fread(body.data(), 1, len, in) != len) break;
            ++idx;
            BplcFrame fr;
            fr.meta.from_raw = true;
            fr.data = body;
            fr.meta.is_rf = (len > 0 && body[0] != 0);
            MsduState msdu_state;
            ParseResult r = parser.parse(fr, msdu_state, filt);
            // Expected: dedup of direct station header sources, in station order.
            QVector<TeiMacPair> want;
            for (int si = 0; si < r.mpdu.sof_stations.size(); ++si) {
                const auto& st = r.mpdu.sof_stations[si];
                MsduInfo si_info = GW_2026_MsduParser::parse(st.msdu);
                if (si_info.msdu_src_tei > 0 && si_info.msdu_src_mac != 0) {
                    bool dup = false;
                    for (const auto& p : want)
                        if (p.tei == si_info.msdu_src_tei && p.mac == si_info.msdu_src_mac) { dup = true; break; }
                    if (!dup) want.append({(quint16)si_info.msdu_src_tei, si_info.msdu_src_mac});
                }
            }
            bool ok = (r.msdu.orig_srcs.size() == want.size());
            if (ok) {
                for (int k = 0; k < want.size(); ++k)
                    if (r.msdu.orig_srcs[k].tei != want[k].tei || r.msdu.orig_srcs[k].mac != want[k].mac) { ok = false; break; }
            }
            std::printf("FRAME %ld accept=%d merged_orig=[%s] want=[%s]\n", idx, r.accept ? 1 : 0,
                        qPrintable(srcs_str(r.msdu.orig_srcs)), qPrintable(srcs_str(want)));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "parser frame %ld orig_srcs invariant", idx);
            check(r.accept && ok, buf);
        }
        check(idx == 3, "parser read 3 frames");
    }
    std::fclose(in);

    // ---- B. Model column ----
    const quint64 macA = mac6(0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0x0b);  // "aa:bb:cc:dd:ee:0b"
    const quint64 macB = mac6(0x01, 0x02, 0x03, 0x04, 0x05, 0x06);  // "01:02:03:04:05:06"
    const quint64 macC = mac6(0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f);  // "0a:0b:0c:0d:0e:0f"
    const quint64 macCco = mac6(0x00, 0x11, 0x22, 0x33, 0x44, 0x55);
    PacketListModel model;
    {
        PacketEntry m;  // merged-style: present=false, dup source
        m.accepted = true;
        m.msdu.present = false;
        m.msdu.simple_head = false;
        m.msdu.msdu_src_tei = -1;
        m.msdu.orig_srcs.append({11, macA});
        m.msdu.orig_srcs.append({12, macB});
        m.msdu.orig_srcs.append({11, macA});
        PacketEntry s;  // single-frame fallback
        s.accepted = true;
        s.msdu.present = true;
        s.msdu.simple_head = false;
        s.msdu.msdu_src_tei = 5;
        s.msdu.msdu_src_mac = macC;
        PacketEntry rj;  // rejected: empty even with orig_srcs
        rj.accepted = false;
        rj.msdu.orig_srcs.append({11, macA});
        PacketEntry cco;  // CCO naming
        cco.accepted = true;
        cco.msdu.present = false;
        cco.msdu.simple_head = false;
        cco.msdu.msdu_src_tei = -1;
        cco.msdu.orig_srcs.append({1, macCco});
        model.append_packets(QVector<PacketEntry>{m, s, rj, cco});
        model.set_display_filter(QString());
        const auto col = [&](int row) {
            return model.data(model.index(row, PacketListModel::COL_ORIG_SRC), Qt::DisplayRole).toString();
        };
        check(col(0) == QStringLiteral("STA-11 [aa:bb:cc:dd:ee:0b],STA-12 [01:02:03:04:05:06]"),
              "column multi format + dedup");
        check(col(1) == QStringLiteral("STA-5 [0a:0b:0c:0d:0e:0f]"),
              "column single fallback unchanged");
        check(col(2).isEmpty(), "column rejected empty");
        check(col(3) == QStringLiteral("CCO [00:11:22:33:44:55]"), "column CCO naming");
    }

    // ---- C. Filter ----
    {
        // Learn MAC->TEI table via the public append path (as mac-filter-probe does).
        PacketEntry l;
        l.accepted = true;
        l.mpdu.net_id = 1;
        l.msdu.tei_mac_pairs.append({11, macA});
        l.msdu.tei_mac_pairs.append({12, macB});
        model.append_packets(QVector<PacketEntry>{l});

        PacketEntry m;  // merged-style entry, hop TEIs 0 to isolate the word path
        m.accepted = true;
        m.msdu.present = false;
        m.msdu.simple_head = false;
        m.msdu.msdu_src_tei = -1;
        m.msdu.orig_srcs.append({11, macA});
        m.msdu.orig_srcs.append({12, macB});
        m.search_text = make_search_text(m);
        PacketEntry s;  // single frame, hop TEIs 0, frame src 5
        s.accepted = true;
        s.msdu.present = true;
        s.msdu.simple_head = false;
        s.msdu.msdu_src_tei = 5;
        s.msdu.msdu_src_mac = macC;
        s.search_text = make_search_text(s);
        auto t = [&](const char* name, const QString& expr, const PacketEntry& e, bool want) {
            model.set_display_filter(expr);
            const bool got = model.passes_filter(e);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "filter %s '%s' -> %d want %d",
                          name, qPrintable(expr), got ? 1 : 0, want ? 1 : 0);
            check(got == want, buf);
        };
        t("sta-word", "sta-12", m, true);
        t("sta-word", "sta-11", m, true);
        t("sta-word-boundary", "sta-1", m, false);
        t("sta-word-neg", "sta-99", m, false);
        t("mac-pattern", "01:02", m, true);
        t("mac-pattern", "aa:bb", m, true);
        t("mac-pattern-neg", "ff:ff", m, false);
        t("substring", "dd:ee:0", m, true);
        t("single-frame-src-word", "sta-5", s, true);
        t("single-frame-src-word-neg", "sta-6", s, false);
    }

    std::printf("RESULT: %s (%d failures)\n", g_fails == 0 ? "ALL PASS" : "FAIL", g_fails);
    return g_fails == 0 ? 0 : 1;
}
