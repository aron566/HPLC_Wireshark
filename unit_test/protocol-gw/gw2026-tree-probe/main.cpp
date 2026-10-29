// GW_2026 OFDMA/TDA 模拟帧探针:裸 MPDU -> GW_2026_Parser(from_raw) -> ProtocolTree
// 打印 C++ 解析结果全量 + 字段树全部节点,供与模拟帧期望表逐项核对。
#include <QApplication>
#include <QFile>
#include <QByteArray>
#include <QTreeWidgetItem>
#include <cstdio>
#include "bplcframe.h"
#include "gw_2026_parser.h"
#include "protocoltree.h"

static void dump_item(QTreeWidgetItem* it, int depth) {
    std::printf("%*s%s = %s\n", depth * 2, "",
                qPrintable(it->text(0)), qPrintable(it->text(1)));
    for (int i = 0; i < it->childCount(); ++i)
        dump_item(it->child(i), depth + 1);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    if (argc < 2) { std::printf("usage: probe <mpdu.bin>\n"); return 1; }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) { std::printf("cannot open\n"); return 1; }
    QByteArray mpdu = file.readAll();
    std::printf("mpdu bytes: %d\n", (int)mpdu.size());

    // from_raw 模式:行首字节 = isRF(0=载波),其后为纯 MPDU
    BplcFrame in;
    in.data = QByteArray(1, char(0)) + mpdu;
    in.meta.from_raw = true;
    in.meta.has_time_tag = false;
    in.meta.frame_time = QDateTime::currentDateTime();

    GW_2026_Parser parser;
    MsduState st;
    ParseFilter f;
    auto r = parser.parse(in, st, f);
    std::printf("accept=%d reject_reason=%s\n",
                (int)r.accept, qPrintable(r.reject_reason));
    std::printf("meta: is_rf=%d channel=%d option=%d phr_mcs=%d\n",
                (int)r.meta.is_rf, r.meta.channel, r.meta.option, r.meta.phr_mcs);
    const MpduInfo& m = r.mpdu;
    std::printf("mpdu: frame_type=%d net_type=%d net_id=0x%x version=%d "
                "src_tei=%d dst_tei=%d frame_len=%d\n",
                m.frame_type, m.net_type, m.net_id, m.version,
                m.src_tei, m.dst_tei, m.frame_len);
    std::printf("sof: data_type=%d tf_pos=%d tf_num=%d fch_sym_num=%d "
                "mod_mode=%d mod_param=%d\n",
                m.sof_data_type, m.sof_tf_symbol_pos, m.sof_tf_symbol_num,
                m.sof_fch_sym_num, m.sof_mod_mode, m.sof_mod_param);
    std::printf("pb: num=%d num_field=%d size=%d band=%d gain=%d load_table_id=%d "
                "bc=%d resend=%d enc=%d\n",
                m.pb_num, m.pb_num_field, m.pb_size, m.sof_band, m.sof_gain,
                m.sof_load_table_id, (int)m.bc_flag, (int)m.re_send_flag,
                (int)m.encryp_flag);
    std::printf("crc: fch_checked=%d fch_ok=%d pb_ok=%d pb_heads=%d pb_crc_oks=",
                (int)m.fch_crc_checked, (int)m.fch_crc_ok, (int)m.pb_crc_ok,
                (int)m.pb_heads.size());
    for (int i = 0; i < m.pb_crc_oks.size(); ++i)
        std::printf("%d", (int)m.pb_crc_oks[i]);
    std::printf(" pb_head_bytes=");
    for (int i = 0; i < m.pb_heads.size(); ++i)
        std::printf("0x%02x ", m.pb_heads[i]);
    std::printf("\n");
    std::printf("msdu_body=%d present=%d summary=%s msdu_raw_base=%d\n",
                (int)r.msdu_body.size(), (int)r.msdu.present,
                qPrintable(r.msdu.summary), r.msdu_raw_base);
    std::printf("payload_for_log=%d\n", (int)r.payload_for_log.size());

    PacketEntry e;
    e.index = 1;
    e.accepted = r.accept;
    e.reason = r.reject_reason;
    e.raw_bytes = r.payload_for_log;
    e.meta = r.meta;
    e.mpdu = r.mpdu;
    e.msdu_body = r.msdu_body;
    e.msdu = r.msdu;
    e.msdu_raw_base = r.msdu_raw_base;
    e.protocol = kProtocolGw2026;

    ProtocolTree tree;
    tree.show_packet(e);
    std::printf("--- field tree ---\n");
    for (int i = 0; i < tree.topLevelItemCount(); ++i)
        dump_item(tree.topLevelItem(i), 0);
    return 0;
}
