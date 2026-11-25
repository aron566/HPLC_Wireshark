// GW OFDMA/TDA 多站逐站 CRC 进 mpdu.pb_crc_oks 回归用例。
// 对应用户 2026-10-30 定:逐站 CRC 进 mpdu.pb_crc_oks,且该字段只针对 PB 块 CRC。
// 直接调用 gw_2026_sof::assemble():载波 SOF(表7 帧子类型 2=OFDMA / 3=TDA),
// 2 站,每站 1 块 136B(分集拷贝模式 2,HDC2.0 4-1 物理层 表18),GW 布局头 1B + 体 + CRC24 3B。
// 站点 0 TEI = FCH 目的 TEI(表10 (5,4,12));站点 1 TEI 在 eFC(表11/13)。
// 断言:OFDMA 全好 [true,true];翻转站 0/站 1/双站对应位 false;
//      TDA 全好 [true,true]、翻转站 1 [true,false];多站汇总 pb_num 为 0。
#include <QCoreApplication>
#include <cstdio>
#include "gw_2026_sof_parser.h"
#include "gw_2026_ack_parser.h"
#include "gw_2026_coord_parser.h"
#include "fieldtools.h"
#include "crc.h"

static void put_crc24(quint8* blk, int len) {
    const quint32 calc = crc24_lsb(blk, len);
    blk[len - 3] = calc & 0xFF;
    blk[len - 2] = (calc >> 8) & 0xFF;
    blk[len - 1] = (calc >> 16) & 0xFF;
}

// GW 单块 136B:头 1B(START+END) + 体 132B + CRC24 3B。
static QByteArray build_gw_block(bool flip_crc) {
    QByteArray blk(136, 0);
    quint8* p = reinterpret_cast<quint8*>(blk.data());
    p[0] = 0xC0;                     // START + END 单块
    put_crc24(p, 136);
    if (flip_crc) p[135] ^= 0xFF;    // 末 CRC 字节翻转
    return blk;
}

// GW 多站帧 304B:FCH 16B + eFC 16B + 站点 0 块 136B + 站点 1 块 136B。
static QByteArray build_gw_multi(bool is_tda, bool flip0, bool flip1) {
    QByteArray f(304, 0);
    quint8* p = reinterpret_cast<quint8*>(f.data());
    p[0] = 0x01;                     // SOF
    set_bits(p, 4, 0, 12, 1);        // 源 TEI = 1 (CCO)
    set_bits(p, 5, 4, 12, 27);       // 目的 TEI = 站点 0 TEI
    set_bits(p, 8, 0, 12, 100);      // 帧长(显示用)
    p[7] = is_tda ? 0x0B : 0x0A;     // 帧子类型 3/2 + 站点数码 1(2 站,表10/表12 (7,3,2))
    set_bits(p, 11, 3, 5, 2);        // 站点 0 分集拷贝模式 2 -> 136B(表10 (11,3,5))
    // 站点 0 PB 数码 (11,2,1)=0 -> 1 块(留 0);TDA 恒 1 块
    quint8* e = p + 16;              // eFC
    set_bits(e, 0, 0, 12, 42);       // 站点 1 TEI(表11/13 (b,0,12),b=16)
    if (is_tda)
        set_bits(e, 3, 0, 5, 2);     // TDA 分集拷贝模式(表13 (b+3,0,5))
    else
        set_bits(e, 2, 0, 5, 2);     // OFDMA 分集拷贝模式(表11 (b+2,0,5))
    // OFDMA 站点 1 PB 数码 (18,5,1)=0 -> 1 块(留 0)
    put_crc24(e, 16);                // eFC FCS -> bytes 29..31(上游 877f838 起 eFC CRC24 错整帧拒收)
    f.replace(32, 136, build_gw_block(flip0));
    f.replace(168, 136, build_gw_block(flip1));
    return f;
}

static bool oks_eq(const QVector<bool>& v, std::initializer_list<bool> want) {
    if (v.size() != (int)want.size()) return false;
    int i = 0;
    for (bool b : want) if (v[i++] != b) return false;
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int fails = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    };
    auto run = [&](const char* name, bool is_tda, bool flip0, bool flip1,
                   std::initializer_list<bool> want) {
        MpduInfo info;
        MsduState msdu;
        QByteArray complete;
        const QString err = gw_2026_sof::assemble(build_gw_multi(is_tda, flip0, flip1),
                                                  info, msdu, complete, 0xFF, false);
        check(name, err.isEmpty());
        if (!err.isEmpty()) {
            std::printf("  assemble err: %s\n", qPrintable(err));
            return;
        }
        check("stations", info.sof_stations.size() == 2);
        if (info.sof_stations.size() == 2) {
            check("sta0 tei", info.sof_stations[0].tei == 27);
            check("sta1 tei", info.sof_stations[1].tei == 42);
            check("sta0 size", info.sof_stations[0].pb_size == 136);
            check("sta1 size", info.sof_stations[1].pb_size == 136);
        }
        check("frame oks", oks_eq(info.pb_crc_oks, want));
        check("pb_num 0", info.pb_num == 0);
    };

    run("ofdma-ok", false, false, false, {true, true});
    run("ofdma-flip0", false, true, false, {false, true});
    run("ofdma-flip1", false, false, true, {true, false});
    run("ofdma-flipboth", false, true, true, {false, false});
    run("tda-ok", true, false, false, {true, true});
    run("tda-flip1", true, false, true, {true, false});

    // eFC FCS 坏 ⇒ 整帧拒收(与 FC CRC 坏同口径),不出站点
    for (bool is_tda : {false, true}) {
        QByteArray f = build_gw_multi(is_tda, false, false);
        f[29] = char(quint8(f[29]) ^ 0x01);
        MpduInfo info; MsduState msdu; QByteArray complete;
        const QString err = gw_2026_sof::assemble(f, info, msdu, complete, 0xFF, false);
        check(is_tda ? "tda-bad-efc rejected" : "ofdma-bad-efc rejected",
              err.contains(QStringLiteral("CRC24")) && info.sof_stations.isEmpty());
    }

    // H2 防回归:eFC 符号数 (11,0,3)=4(bit2=1) 时,旧代码把 (11,2) 当「站点0 PB 个数」
    // 读成 2 块,导致站点0 切 2×136B、站点1 块起点错位、CRC 全挂。V6 无该字段,
    // 每站恒 1 块:断言仍 2 站、每站 pb_num==1、每站 1 块 136B、CRC 全过。
    {
        QByteArray f = build_gw_multi(/*is_tda=*/false, false, false);
        quint8* p = reinterpret_cast<quint8*>(f.data());
        set_bits(p, 11, 0, 3, 4);        // eFC 符号数 = 4(表10 (11,0,3));bit2=1 是旧误读触发位
        MpduInfo info; MsduState msdu; QByteArray complete;
        const QString err = gw_2026_sof::assemble(f, info, msdu, complete, 0xFF, false);
        check("H2 eFC-sym4: no error", err.isEmpty());
        check("H2 eFC-sym4: 2 stations", info.sof_stations.size() == 2);
        bool each_one_block = info.sof_stations.size() == 2
            && info.sof_stations[0].pb_num == 1 && info.sof_stations[1].pb_num == 1
            && info.sof_stations[0].pb_size == 136 && info.sof_stations[1].pb_size == 136;
        check("H2 eFC-sym4: each sta 1 block 136B", each_one_block);
        check("H2 eFC-sym4: crc all ok", oks_eq(info.pb_crc_oks, {true, true}));
    }

    // M2 防回归:无线 SOF(表18) PB 块个数 (10,0,2)=1 → 2 块(旧代码固定 1 块)。
    // 表18:PB 大小码 (9,4,4)=4→264B;2 块 264B;断言 pb_num==2、pb_size==264、
    // 逐块 CRC 全过、体流交付非空。
    {
        QByteArray f(16 + 2 * 264, 0);
        quint8* p = reinterpret_cast<quint8*>(f.data());
        p[0] = 0x01;                          // SOF
        set_bits(p, 4, 0, 12, 1);             // 源 TEI = 1
        set_bits(p, 5, 4, 12, 27);            // 目的 TEI
        set_bits(p, 8, 0, 12, 100);           // 帧长
        set_bits(p, 9, 4, 4, 4);              // 表18 (9,4,4) PB 大小码 4 = 264B
        set_bits(p, 10, 0, 2, 1);             // 表18 (10,0,2) PB 块个数 1 = 2 块
        // 块 0(seq0, START) + 块 1(seq1, END),各 264B
        for (int blk = 0; blk < 2; ++blk) {
            quint8* b = p + 16 + blk * 264;
            b[0] = (blk == 0) ? 0x40 : 0x81;  // START / END
            put_crc24(b, 264);
        }
        MpduInfo info; MsduState msdu; QByteArray complete;
        const QString err = gw_2026_sof::assemble(f, info, msdu, complete, 0xFF, /*is_rf=*/true);
        check("M2 rf-2pb: no error", err.isEmpty());
        check("M2 rf-2pb: pb_num==2", info.pb_num == 2);
        check("M2 rf-2pb: pb_size==264", info.pb_size == 264);
        check("M2 rf-2pb: crc all ok", oks_eq(info.pb_crc_oks, {true, true}));
        check("M2 rf-2pb: body delivered", !complete.isEmpty());
    }

    // L2 防回归:无线 SACK(表19) 与载波 SACK(表14) 分介质解析。
    // 表19:无 BRU/SAT ID 标志((4,0,2)保留),RxPBNum(4,2,2)/RxStatus(4,4,4)
    //   SrcTEI(5,0,12)/DstTEI(6,4,12)/(8,0,8)保留/信道质量=信号强度(9,0,8)/负载(10,0,8)/(11,0,8)保留。
    // 表14:BRU(4,0,1)/SATID(4,1,1)/RxPBNum(4,2,2)/RxStatus(4,4,4)/.../(8,0,2)剩余站点/(8,2,6)RSV。
    {
        quint8 f[13] = {0};
        f[0] = 0x02;                       // FrameType = ACK(表1 (0,0,3)=2 选择确认)
        set_bits(f, 4, 0, 2, 3);           // 无线表19 (4,0,2) 保留 = 3(非 BRU/SAT 标志)
        set_bits(f, 4, 2, 2, 1);           // RxPBNum = 1 → 2 块
        set_bits(f, 4, 4, 4, 0x5);         // RxStatus = 0x5 (PB1/PB3 CRC ok)
        set_bits(f, 5, 0, 12, 0x321);      // SrcTEI
        set_bits(f, 6, 4, 12, 0x654);      // DstTEI
        f[8]  = 0xAB;                      // 表19 (8,0,8) 保留
        f[9]  = 0x3C;                      // 表19 (9,0,8) 信道质量 = 信号强度 0x3C=60
        f[10] = 0x11;                      // 站点负载
        f[11] = 0x22;                      // 保留
        f[12] = 0x00;                      // ExtFrameType
        MpduInfo mrf; gw_2026_ackp::parse_fch(f, mrf, /*is_rf=*/true);
        check("L2 rf-sack: bru_snr_flag==0", mrf.ack_bru_snr_flag == 0);
        check("L2 rf-sack: sat_id_flag==0", mrf.ack_sat_id_flag == 0);
        check("L2 rf-sack: rx_pb_num==1", mrf.ack_rx_pb_num == 1);
        check("L2 rf-sack: src_tei==0x321", mrf.src_tei == 0x321);
        check("L2 rf-sack: dst_tei==0x654", mrf.dst_tei == 0x654);
        check("L2 rf-sack: rsv0(byte8)==0xAB", mrf.ack_rsv0 == 0xAB);
        check("L2 rf-sack: signal(byte9)==0x3C", mrf.ack_channel_quality == 0x3C);

        // 同帧按载波表14 解析:BRU(4,0)=1 时会走 SNR 分支;这里 (4,0,2)=3 → bit0=1
        // 载波口径把 bit0 当 BRU 标志=1,会进表15 分支,ack_rsv0 恒 0,证明分介质必要。
        MpduInfo mpl; gw_2026_ackp::parse_fch(f, mpl, /*is_rf=*/false);
        check("L2 carrier-sack: bru_snr_flag==1 (bit0 set)", mpl.ack_bru_snr_flag == 1);
        check("L2 carrier-sack: rsv0==0 (表15无 byte8)", mpl.ack_rsv0 == 0);
    }

    // M4 防回归:COORD 邻网号(表16 (8,0,16)) 16bit,旧代码读 24bit 会把 byte10(保留)
    // 读进高位。构造 byte8-9=0x3456,byte10=0xFF(保留),断言 neighbour_nid==0x3456
    // 而非旧 24bit 的 0xFF3456;RF 信道(11,0,8)、byte12 bit0-3(NetRfOption/RSV)。
    {
        quint8 f[13] = {0};
        f[0] = 0x03;                       // FrameType = COORD
        set_bits(f, 4, 0, 16, 0x100);      // TimeDuration
        set_bits(f, 6, 0, 16, 0x200);      // BandStartOffset
        set_bits(f, 8, 0, 16, 0x3456);     // NeighbourNID (byte8=0x56,byte9=0x34)
        f[10] = 0xFF;                      // 表16 (10,0,8) 保留(旧 24bit 会读成高位)
        f[11] = 0x12;                      // NetRfChannel
        f[12] = 0x05;                      // NetRfOption(bit0-1)=1, RSV(bit2-3)=1
        MpduInfo m; gw_2026_coordp::parse_fch(f, m);
        check("M4 coord: neighbour_nid==0x3456 (16bit, not 0xFF3456)",
              m.coord_neighbour_nid == 0x3456);
        check("M4 coord: rf_channel==0x12", m.coord_rf_channel == 0x12);
        check("M4 coord: rsv0(byte12 low4)==0x05", m.coord_rsv0 == 0x05);
    }

    std::printf("RESULT: %s\n", fails == 0 ? "ALL PASS" : "FAILURES");
    return fails == 0 ? 0 : 1;
}
