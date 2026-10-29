// NW_2026 OFDMA type-1 probe: feed bare MPDU via from_raw to the production
// NW_2026_Parser (38852670 tree) + run the production pb_assembler on the
// trigger-derived per-station layout for the UL frame.
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
#include "bplcframe.h"
#include "nw_2026_parser.h"
#include "pb_assembler.h"

static void run_parser(const char* name, const QByteArray& mpdu) {
    BplcFrame in;
    in.data = QByteArray(1, char(0)) + mpdu;   // isRF = 0
    in.meta.from_raw = true;
    NW_2026_Parser parser;
    MsduState st;
    ParseFilter f;
    auto r = parser.parse(in, st, f);
    const MpduInfo& m = r.mpdu;
    std::printf("[%s] len=%d accept=%d reject='%s'\n", name, (int)mpdu.size(),
                (int)r.accept, qPrintable(r.reject_reason));
    std::printf("  fch_checked=%d fch_ok=%d ver=%d frame_type=%d src=%d dst=%d frame_len=%d\n",
                (int)m.fch_crc_checked, (int)m.fch_crc_ok, m.version, m.frame_type,
                m.src_tei, m.dst_tei, m.frame_len);
    std::printf("  pb_num=%d pb_size=%d pb_crc_ok=%d msdu.present=%d summary='%s'\n",
                m.pb_num, m.pb_size, (int)m.pb_crc_ok, (int)r.msdu.present,
                qPrintable(r.msdu.summary));
    std::printf("  pb_crc_oks=");
    for (bool ok : m.pb_crc_oks) std::printf("%d", ok ? 1 : 0);
    std::printf(" (n=%d)\n", (int)m.pb_crc_oks.size());
}

static void run_asm(const char* name, const QByteArray& mpdu, int num, int size, int base) {
    pb_asm::PbLayout layout{4, 1, 3, 2, 0, base};
    auto res = pb_asm::assemble_pb_blocks(mpdu, num, size, layout);
    std::printf("[asm %s] base=%d num=%d size=%d truncated=%d all_crc_ok=%d blocks=",
                name, base, num, size, (int)res.truncated, (int)res.all_crc_ok);
    for (const auto& b : res.blocks) std::printf("%d", b.crc_ok ? 1 : 0);
    std::printf(" msdu_body=%dB\n", (int)res.msdu_body.size());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    auto load = [](const char* path) {
        QFile f(QString::fromLocal8Bit(path));
        if (!f.open(QIODevice::ReadOnly)) { std::printf("cannot open %s\n", path); exit(1); }
        return f.readAll();
    };
    QByteArray trig = load(argv[1]);
    QByteArray ul = load(argv[2]);
    QByteArray sack = load(argv[3]);
    run_parser("trigger type2", trig);
    run_parser("ul_data type1", ul);
    run_parser("dl_sack type1", sack);
    // Trigger-derived station layout for the UL frame (table 28 eFC):
    // sta0: 1 x 136B @16 ; sta1: 2 x 72B @152
    run_asm("ul sta0", ul, 1, 136, 16);
    run_asm("ul sta1", ul, 2, 72, 152);
    // DL-SACK: single 16B PB @16, fixed TMI 26
    run_asm("sack", sack, 1, 16, 16);
    // DL-SACK body decode (table 49/50)
    const quint8* b = reinterpret_cast<const quint8*>(sack.constData()) + 20;
    std::printf("[sack body] MsgID=%d SrcTEI=%d RxStatus=0b%d%d\n",
                b[0] & 0x0F, ((b[0] >> 4) & 0xF) | (b[1] << 4),
                (b[2] >> 1) & 1, b[2] & 1);
    return 0;
}
