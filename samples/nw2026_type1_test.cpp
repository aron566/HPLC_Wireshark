// NW_2026 OFDMA 类型1 三帧断言测试(trigger 上下文 / UL-OFDMA 数据 / DL-SACK / 无上下文)
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
#include "bplcframe.h"
#include "nw_2026_parser.h"

static int fails = 0;
static void check(bool ok, const char* what) {
    if (!ok) { printf("FAIL: %s\n", what); ++fails; }
    else printf("OK:   %s\n", what);
}

static BplcFrame load(const char* fn) {
    QFile f(QString::fromLocal8Bit(fn));
    f.open(QIODevice::ReadOnly);
    QByteArray bin = f.readAll();
    BplcFrame fr;
    fr.data = QByteArray(1, '\x00') + bin;   // isRF=0 前缀 + MPDU(FCH byte0 起)
    fr.meta.from_raw = true;
    fr.meta.has_time_tag = false;
    return fr;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    MsduState msdu;
    NW_2026_Parser::Filter flt;

    // 同一实例:trigger 先喂建立上下文
    NW_2026_Parser parser;
    auto rt = parser.parse(load("wireshark_support_plugins/nw2026_trigger.bin"), msdu, flt);
    check(rt.accept && rt.mpdu.pb_num == 0 && rt.mpdu.pb_size == 0, "trigger pb_num=0 pb_size=0");
    check(rt.mpdu.src_tei == 1 && rt.mpdu.dst_tei == 0xFFF, "trigger src=1 dst=0xFFF");

    auto ru = parser.parse(load("wireshark_support_plugins/nw2026_ul_ofdma_data.bin"), msdu, flt);
    check(ru.mpdu.sof_stations.size() == 2, "ul_data sof_stations=2");
    if (ru.mpdu.sof_stations.size() == 2) {
        const auto& s0 = ru.mpdu.sof_stations[0];
        const auto& s1 = ru.mpdu.sof_stations[1];
        check(s0.tei == 2 && s0.pb_num == 1 && s0.pb_size == 136 && s0.block_base == 16, "ul_data sta0 tei=2 1x136@16");
        check(s0.pb_crc_oks.size() == 1 && s0.pb_crc_oks[0], "ul_data sta0 crc=[true]");
        check(s1.tei == 3 && s1.pb_num == 2 && s1.pb_size == 72 && s1.block_base == 152, "ul_data sta1 tei=3 2x72@152");
        check(s1.pb_crc_oks.size() == 2 && s1.pb_crc_oks[0] && s1.pb_crc_oks[1], "ul_data sta1 crc=[true,true]");
    }

    auto rs = parser.parse(load("wireshark_support_plugins/nw2026_dl_sack.bin"), msdu, flt);
    check(rs.mpdu.sof_stations.size() == 1, "dl_sack sof_stations=1");
    if (rs.mpdu.sof_stations.size() == 1) {
        const auto& s0 = rs.mpdu.sof_stations[0];
        check(s0.tei == 2 && s0.pb_num == 1 && s0.pb_size == 16 && s0.block_base == 16, "dl_sack tei=2 1x16@16");
        check(s0.pb_crc_oks.size() == 1 && s0.pb_crc_oks[0], "dl_sack crc=[true]");
        check(s0.msdu.size() == 8, "dl_sack msdu 8B");
    }

    {   // 无上下文:新 parser 只喂 ul_data
        NW_2026_Parser p2;
        auto r = p2.parse(load("wireshark_support_plugins/nw2026_ul_ofdma_data.bin"), msdu, flt);
        check(r.mpdu.pb_num == 0 && r.mpdu.sof_stations.isEmpty(), "nocontext pb_num=0 sof_stations=0");
        check(r.msdu.present && r.msdu.summary.contains(QStringLiteral("需 trigger")), "nocontext summary 含「需 trigger」");
    }

    printf(fails ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", fails);
    return fails ? 1 : 0;
}
