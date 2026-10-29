// GW_2026 TEI-MAC learning probe: scan a [u32 LE len][isRF 1B][MPDU] records
// file through GW_2026_Parser and dump per-frame learning outputs.
// Usage: gw_learn_probe <records.bin> <out.txt>
// Only frames with msdu/beacon/pairs/topo/OFDMA-TDA get a line; aggregates
// for all frames go to stdout and to the tail of out.txt.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include "bplcframe.h"
#include "iprotocolparser.h"
#include "gw_protocol/gw_2026/gw_2026_parser.h"

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
        s += QString("%1:%2").arg(p.tei).arg(macstr(p.mac));
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

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) { std::fprintf(stderr, "usage: %s <records.bin> <out.txt>\n", argv[0]); return 2; }
    FILE* in = std::fopen(argv[1], "rb");
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    FILE* out = std::fopen(argv[2], "wb");
    if (!out) { std::fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }

    GW_2026_Parser parser;
    MsduState msdu_state;
    ParseFilter filt;
    long idx = 0, accepted = 0;
    std::map<int, long> ftype_cnt, sofdt_cnt, proto_cnt;
    std::map<std::string, long> summary_cnt, topo_kind_cnt;
    long total_msdu_pairs = 0, total_beacon_pairs = 0, total_topo_nodes = 0;
    long total_topo_events = 0, ofdma_frames = 0, tda_frames = 0, ofdma_stations = 0;
    long gather_frames = 0, assoc_cnf_frames = 0, discover_frames = 0;

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
        ParseResult r = parser.parse(fr, msdu_state, filt);
        if (r.accept) ++accepted;
        proto_cnt[(int)r.protocol]++;
        ftype_cnt[r.mpdu.frame_type]++;
        if (r.mpdu.frame_type == 1) {
            sofdt_cnt[r.mpdu.sof_data_type]++;
            if (r.mpdu.sof_data_type == 2) { ++ofdma_frames; ofdma_stations += r.mpdu.sof_stations.size(); }
            if (r.mpdu.sof_data_type == 3) { ++tda_frames; ofdma_stations += r.mpdu.sof_stations.size(); }
        }
        bool interesting = false;
        QString line;
        if (r.msdu.present) {
            summary_cnt[r.msdu.summary.toStdString()]++;
            if (r.msdu.summary.contains("关联汇总")) ++gather_frames;
            if (r.msdu.summary.contains("关联确认")) ++assoc_cnf_frames;
            if (r.msdu.summary.contains("发现列表")) ++discover_frames;
            interesting = true;
        }
        if (r.beacon.present) interesting = true;
        if (!r.msdu.tei_mac_pairs.isEmpty() || !r.beacon.tei_mac_pairs.isEmpty()) interesting = true;
        if (r.msdu.topo_event.kind != TopoEventKind::Other) interesting = true;
        if (r.mpdu.frame_type == 1 && (r.mpdu.sof_data_type == 2 || r.mpdu.sof_data_type == 3)) interesting = true;
        total_msdu_pairs += r.msdu.tei_mac_pairs.size();
        total_beacon_pairs += r.beacon.tei_mac_pairs.size();
        total_topo_nodes += r.msdu.topo_event.nodes.size();
        if (r.msdu.topo_event.kind != TopoEventKind::Other) {
            ++total_topo_events;
            topo_kind_cnt[kind_name(r.msdu.topo_event.kind)]++;
        }
        if (interesting) {
            line = QString("FRAME %1 accept=%2 proto=%3 ft=%4 sofdt=%5 nsta=%6 msdu=%7 sum=[%8] "
                           "pairs=[%9] hdr=(%10:%11,%12:%13) topo=%14 nodes=[%15] "
                           "bcon_pairs=[%16] bcon_cco=%17\n")
                       .arg(idx).arg(r.accept ? 1 : 0).arg((int)r.protocol).arg(r.mpdu.frame_type)
                       .arg(r.mpdu.sof_data_type).arg(r.mpdu.sof_stations.size())
                       .arg(r.msdu.present ? 1 : 0).arg(r.msdu.summary)
                       .arg(pairs_str(r.msdu.tei_mac_pairs))
                       .arg(r.msdu.msdu_src_tei).arg(macstr(r.msdu.msdu_src_mac))
                       .arg(r.msdu.msdu_dst_tei).arg(macstr(r.msdu.msdu_dst_mac))
                       .arg(kind_name(r.msdu.topo_event.kind))
                       .arg(pairs_str(r.msdu.topo_event.nodes))
                       .arg(pairs_str(r.beacon.tei_mac_pairs))
                       .arg(macstr(r.mpdu.beacon_cco_mac));
            std::fwrite(line.toUtf8().constData(), 1, (size_t)line.toUtf8().size(), out);
        }
    }
    std::fprintf(out, "AGG total=%ld accepted=%ld msdu_pairs=%ld beacon_pairs=%ld topo_events=%ld topo_nodes=%ld\n",
                 idx, accepted, total_msdu_pairs, total_beacon_pairs, total_topo_events, total_topo_nodes);
    std::fprintf(out, "AGG ofdma_frames=%ld tda_frames=%ld ofdma_tda_stations=%ld gather=%ld assoc_cnf=%ld discover=%ld\n",
                 ofdma_frames, tda_frames, ofdma_stations, gather_frames, assoc_cnf_frames, discover_frames);
    for (auto& kv : ftype_cnt) std::fprintf(out, "AGG ftype %d = %ld\n", kv.first, kv.second);
    for (auto& kv : proto_cnt) std::fprintf(out, "AGG proto %d = %ld\n", kv.first, kv.second);
    for (auto& kv : sofdt_cnt) std::fprintf(out, "AGG sofdt %d = %ld\n", kv.first, kv.second);
    for (auto& kv : topo_kind_cnt) std::fprintf(out, "AGG topo_kind %s = %ld\n", kv.first.c_str(), kv.second);
    for (auto& kv : summary_cnt) std::fprintf(out, "AGG summary [%s] = %ld\n", kv.first.c_str(), kv.second);
    std::fclose(in);
    std::fclose(out);
    std::printf("total=%ld accepted=%ld msdu_pairs=%ld beacon_pairs=%ld topo_events=%ld topo_nodes=%ld\n",
                idx, accepted, total_msdu_pairs, total_beacon_pairs, total_topo_events, total_topo_nodes);
    std::printf("ofdma=%ld tda=%ld stations=%ld gather=%ld assoc_cnf=%ld discover=%ld\n",
                ofdma_frames, tda_frames, ofdma_stations, gather_frames, assoc_cnf_frames, discover_frames);
    return 0;
}
