// GW_2026 multi-station topo-event merge regression probe.
// Usage: gw_topo_merge_probe <records.bin> <out.txt>
// Records: [u32 LE len][isRF 1B][MPDU], from_raw=true.
// For each frame it prints, per station, the DIRECT MsduParser::parse result
// (what merge_msdu_learn receives) and the MERGED frame-level result, then
// checks: (1) merged topo == first station topo with kind != Other
// (first-non-Other wins); (2) merged pairs ==
// concatenation of all station pairs PLUS each station's head pairs
// (merge converts hdr src/dst into pair form; full merge, no dedup);
// (3) merged present == false; (4) merged summary empty;
// (5) merged topo_extra_events == station events after the first
// non-Other, in order (Shape J: first in topo_event, rest in extras);
// (6) TopoState applied with first + extras contains every station's nodes.
// Fresh MsduState per frame (frame independence); default ParseFilter.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <QSet>
#include "bplcframe.h"
#include "iprotocolparser.h"
#include "gw_protocol/gw_2026/gw_2026_parser.h"
#include "gw_protocol/gw_2026/gw_2026_msdu_parser.h"
#include "topo_state.h"

static QString macstr(quint64 m) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x",
                  (unsigned)((m >> 40) & 0xFF), (unsigned)((m >> 32) & 0xFF),
                  (unsigned)((m >> 24) & 0xFF), (unsigned)((m >> 16) & 0xFF),
                  (unsigned)((m >> 8) & 0xFF), (unsigned)(m & 0xFF));
    return QString::fromLatin1(buf);
}

static QString pairs_str(const QVector<TeiMacPair>& v) {
    QString s;
    for (const auto& p : v) {
        if (!s.isEmpty()) s += ",";
        s += QString("%1:%2").arg((int)p.tei).arg(macstr(p.mac));
    }
    return s.isEmpty() ? QString("-") : s;
}

static QString routes_str(const QVector<QPair<quint16, quint16>>& v) {
    QString s;
    for (const auto& r : v) {
        if (!s.isEmpty()) s += ",";
        s += QString("%1->%2").arg((int)r.first).arg((int)r.second);
    }
    return s.isEmpty() ? QString("-") : s;
}

static const char* kind_name(TopoEventKind k) {
    switch (k) {
        case TopoEventKind::DiscoverList: return "DiscoverList";
        case TopoEventKind::AssocReq: return "AssocReq";
        case TopoEventKind::AssocCnf: return "AssocCnf";
        case TopoEventKind::AssocGatherInd: return "AssocGatherInd";
        case TopoEventKind::AssocInd: return "AssocInd";
        case TopoEventKind::ChangeProxyReq: return "ChangeProxyReq";
        case TopoEventKind::ChangeProxyCnf: return "ChangeProxyCnf";
        case TopoEventKind::LeaveInd: return "LeaveInd";
        case TopoEventKind::SuccessRate: return "SuccessRate";
        case TopoEventKind::CcoRestart: return "CcoRestart";
        case TopoEventKind::StaRestart: return "StaRestart";
        default: return "Other";
    }
}

static QString topo_str(const TopoEvent& t) {
    return QString("kind=%1 nodes=[%2] routes=[%3] cco=%4 desc=[%5] link=%6 is_rf=%7 known=%8")
        .arg(kind_name(t.kind)).arg(pairs_str(t.nodes)).arg(routes_str(t.routes))
        .arg(macstr(t.cco_mac)).arg(t.desc).arg(t.link_type)
        .arg(t.is_rf ? 1 : 0).arg(t.is_rf_known ? 1 : 0);
}

static bool topo_equal(const TopoEvent& a, const TopoEvent& b) {
    if (a.kind != b.kind || a.cco_mac != b.cco_mac || a.desc != b.desc) return false;
    if (a.link_type != b.link_type || a.is_rf != b.is_rf || a.is_rf_known != b.is_rf_known) return false;
    if (a.nodes.size() != b.nodes.size() || a.routes.size() != b.routes.size()) return false;
    for (int i = 0; i < a.nodes.size(); ++i)
        if (a.nodes[i].tei != b.nodes[i].tei || a.nodes[i].mac != b.nodes[i].mac) return false;
    for (int i = 0; i < a.routes.size(); ++i)
        if (a.routes[i] != b.routes[i]) return false;
    return true;
}

static void emit_line(FILE* out, const QString& s) {
    QByteArray b = s.toUtf8();
    std::fwrite(b.constData(), 1, (size_t)b.size(), out);
    std::fputs("\n", out);
    std::fwrite(b.constData(), 1, (size_t)b.size(), stdout);
    std::fputs("\n", stdout);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) { std::fprintf(stderr, "usage: %s <records.bin> <out.txt>\n", argv[0]); return 2; }
    FILE* in = std::fopen(argv[1], "rb");
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    FILE* out = std::fopen(argv[2], "wb");
    if (!out) { std::fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }

    GW_2026_Parser parser;
    ParseFilter filt;
    long idx = 0;
    int n_pass = 0, n_fail = 0;

    for (;;) {
        unsigned char lb[4];
        if (std::fread(lb, 1, 4, in) != 4) break;
        uint32_t len = (uint32_t)lb[0] | ((uint32_t)lb[1] << 8) |
                       ((uint32_t)lb[2] << 16) | ((uint32_t)lb[3] << 24);
        QByteArray body((int)len, 0);
        if (len > 0 && std::fread(body.data(), 1, len, in) != len) break;
        ++idx;
        BplcFrame fr;
        fr.meta.from_raw = true;
        fr.data = body;  // [isRF 1B][MPDU]
        fr.meta.is_rf = (len > 0 && body[0] != 0);
        MsduState msdu_state;  // fresh per frame
        ParseResult r = parser.parse(fr, msdu_state, filt);

        emit_line(out, QString("FRAME %1 accept=%2 ft=%3 sofdt=%4 nsta=%5 fch_crc_ok=%6 pb_crc_ok=%7")
            .arg(idx).arg(r.accept ? 1 : 0).arg(r.mpdu.frame_type).arg(r.mpdu.sof_data_type)
            .arg(r.mpdu.sof_stations.size()).arg(r.mpdu.fch_crc_ok ? 1 : 0).arg(r.mpdu.pb_crc_ok ? 1 : 0));

        // Direct per-station parses: exactly what merge_msdu_learn receives.
        QVector<MsduInfo> station_infos;
        for (int si = 0; si < r.mpdu.sof_stations.size(); ++si) {
            const auto& st = r.mpdu.sof_stations[si];
            QString crcs;
            for (bool ok : st.pb_crc_oks) crcs += ok ? "1" : "0";
            MsduInfo si_info = GW_2026_MsduParser::parse(st.msdu);
            station_infos.append(si_info);
            emit_line(out, QString("  STATION %1 tei=%2 pb=%3 size=%4 msdu_len=%5 crc=[%6]")
                .arg(si).arg((int)st.tei).arg((int)st.pb_num).arg((int)st.pb_size)
                .arg(st.msdu.size()).arg(crcs));
            emit_line(out, QString("    direct: present=%1 sum=[%2] pairs=[%3] hdr=(%4:%5,%6:%7) topo: %8")
                .arg(si_info.present ? 1 : 0).arg(si_info.summary)
                .arg(pairs_str(si_info.tei_mac_pairs))
                .arg(si_info.msdu_src_tei).arg(macstr(si_info.msdu_src_mac))
                .arg(si_info.msdu_dst_tei).arg(macstr(si_info.msdu_dst_mac))
                .arg(topo_str(si_info.topo_event)));
        }

        emit_line(out, QString("  MERGED: present=%1 sum=[%2] pairs=[%3] hdr=(%4:%5,%6:%7) topo: %8")
            .arg(r.msdu.present ? 1 : 0).arg(r.msdu.summary)
            .arg(pairs_str(r.msdu.tei_mac_pairs))
            .arg(r.msdu.msdu_src_tei).arg(macstr(r.msdu.msdu_src_mac))
            .arg(r.msdu.msdu_dst_tei).arg(macstr(r.msdu.msdu_dst_mac))
            .arg(topo_str(r.msdu.topo_event)));

        // CHECK 1: merged topo == first station topo with kind != Other.
        const TopoEvent* first_event = nullptr;
        for (const auto& si_info : station_infos)
            if (si_info.topo_event.kind != TopoEventKind::Other) { first_event = &si_info.topo_event; break; }
        bool topo_ok = false;
        if (first_event) topo_ok = topo_equal(r.msdu.topo_event, *first_event);
        else topo_ok = (r.msdu.topo_event.kind == TopoEventKind::Other);
        // CHECK 2: merged pairs == concatenation of station pairs PLUS each
        // station's head pairs (merge converts hdr src/dst into pair form,
        // with the same guards as merge_msdu_learn). Direct station parses
        // keep head pairs in hdr fields only, NOT in tei_mac_pairs.
        std::map<std::pair<int, quint64>, long> want, got;
        long want_n = 0;
        for (const auto& si_info : station_infos) {
            for (const auto& p : si_info.tei_mac_pairs) {
                want[std::make_pair((int)p.tei, (quint64)p.mac)]++;
                ++want_n;
            }
            if (si_info.msdu_src_tei > 0 && si_info.msdu_src_mac != 0) {
                want[std::make_pair((int)si_info.msdu_src_tei, (quint64)si_info.msdu_src_mac)]++;
                ++want_n;
            }
            if (si_info.msdu_dst_tei > 0 && si_info.msdu_dst_mac != 0) {
                want[std::make_pair((int)si_info.msdu_dst_tei, (quint64)si_info.msdu_dst_mac)]++;
                ++want_n;
            }
        }
        for (const auto& p : r.msdu.tei_mac_pairs) got[std::make_pair((int)p.tei, (quint64)p.mac)]++;
        bool pairs_ok = (want == got);
        // CHECK 3/4: display fields untouched.
        bool present_ok = !r.msdu.present;
        bool summary_ok = r.msdu.summary.isEmpty();
        // CHECK 5: merged extras == station events after the first non-Other, in order.
        QVector<const TopoEvent*> ordered_events;
        for (const auto& si_info : station_infos)
            if (si_info.topo_event.kind != TopoEventKind::Other) ordered_events.append(&si_info.topo_event);
        const int want_extras = ordered_events.isEmpty() ? 0 : ordered_events.size() - 1;
        bool extras_ok = (r.msdu.topo_extra_events.size() == want_extras);
        if (extras_ok) {
            for (int k = 1; k < ordered_events.size(); ++k)
                if (!topo_equal(r.msdu.topo_extra_events[k - 1], *ordered_events[k])) { extras_ok = false; break; }
        }
        QString extras_str;
        for (const auto& ex : r.msdu.topo_extra_events) extras_str += QString("[%1] ").arg(topo_str(ex));
        emit_line(out, QString("  EXTRAS: %1").arg(extras_str.isEmpty() ? QStringLiteral("(none)") : extras_str));
        // CHECK 6: TopoState applied with first + extras contains every station's nodes.
        TopoState ts;
        auto apply_ev = [&](TopoEvent e) { e.nid = 1; e.frame_index = idx; ts.apply(e); };
        if (r.msdu.topo_event.kind != TopoEventKind::Other) apply_ev(r.msdu.topo_event);
        for (const auto& ex : r.msdu.topo_extra_events)
            if (ex.kind != TopoEventKind::Other) apply_ev(ex);
        QSet<quint16> want_teis;
        for (const auto& ev : ordered_events)
            for (const auto& nd : ev->nodes)
                if (nd.tei) want_teis.insert(nd.tei);
        bool state_ok = true;
        for (quint16 tei : want_teis)
            if (!ts.nodes.contains(tei)) { state_ok = false; break; }
        bool frame_ok = r.accept && topo_ok && pairs_ok && present_ok && summary_ok && extras_ok && state_ok;
        if (frame_ok) ++n_pass; else ++n_fail;
        emit_line(out, QString("  CHECK topo_first_non_other=%1 pairs_full_merge=%2 (want %3 got %4) present_false=%5 summary_empty=%6 extras_in_order=%7 topo_state_nodes=%8 => %9")
            .arg(topo_ok ? "PASS" : "FAIL").arg(pairs_ok ? "PASS" : "FAIL")
            .arg(want_n).arg(r.msdu.tei_mac_pairs.size())
            .arg(present_ok ? "PASS" : "FAIL").arg(summary_ok ? "PASS" : "FAIL")
            .arg(extras_ok ? "PASS" : "FAIL").arg(state_ok ? "PASS" : "FAIL")
            .arg(frame_ok ? "PASS" : "FAIL"));
    }
    emit_line(out, QString("AGG frames=%1 PASS=%2 FAIL=%3").arg(idx).arg(n_pass).arg(n_fail));
    std::fclose(in);
    std::fclose(out);
    return n_fail == 0 ? 0 : 1;
}
