/// 探针:M6 GW_2026 信标 RF 分支(合成帧,非真实抓包帧)
/// RF:载荷 PB 大小码在 FCH (10,0,4),查表29;载波:TMI 在 (9,4,4),查载波表
#include "gw_2026_beacon_parser.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(name, cond) do { \
    std::printf("%s %s\n", (cond) ? "PASS" : "FAIL", name); \
    if (!(cond)) ++g_fail; } while (0)

// 构造信标帧:FCH 16B + PB(pbsize);gb 为标准信标头(20B)+ItemNum=0+零填充+尾 4B
// CRC 值不影响 present(present 在 CRC 校验前置位,CRC 只影响摘要文案),填 0 即可
static QByteArray build_beacon(int pbsize, quint8 fch9, quint8 fch10) {
    QByteArray payload(16 + pbsize, 0);
    payload[9] = (char)fch9;
    payload[10] = (char)fch10;
    // gb 标准头:byte0=0(发现信标/标准),byte1 NetSN=7,bytes2-7 CCO MAC,bytes8-11 周期计数=1
    char* gb = payload.data() + 16;
    gb[1] = 7;
    const quint8 mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    std::memcpy(gb + 2, mac, 6);
    gb[8] = 1;
    gb[12] = 36;   // 本网络无线信道编号
    gb[20] = 0;    // ItemNum=0,无管理条目
    return payload;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // RF1:码 2(72B)+MCS 5 -> present;旧逻辑 TMI=(9,4,4)=5 查载波表得 136B,88<152 拒收
    {
        QByteArray f = build_beacon(72, 0x50, 0x02);
        MsduInfo r = GW_2026_BeaconParser::parse_beacon(f, true);
        CHECK("M6 RF code=2 MCS=5 -> present", r.present && !r.tree.isEmpty());
    }
    // RF2:码 0(16B)非信标载荷尺寸 -> 拒收
    {
        QByteArray f = build_beacon(16, 0x50, 0x00);
        MsduInfo r = GW_2026_BeaconParser::parse_beacon(f, true);
        CHECK("M6 RF code=0 (16B) -> rejected", !r.present);
    }
    // RF3:码 6 保留值 -> 拒收
    {
        QByteArray f = build_beacon(72, 0x50, 0x06);
        MsduInfo r = GW_2026_BeaconParser::parse_beacon(f, true);
        CHECK("M6 RF code=6 reserved -> rejected", !r.present);
    }
    // CAR1:载波 TMI=2(136B) -> present,无回归
    {
        QByteArray f = build_beacon(136, 0x20, 0x00);
        MsduInfo r = GW_2026_BeaconParser::parse_beacon(f, false);
        CHECK("M6 carrier TMI=2 -> present", r.present && !r.tree.isEmpty());
    }
    // CAR2:载波 TMI=15 无效 -> 拒收
    {
        QByteArray f = build_beacon(136, 0xF0, 0x00);
        MsduInfo r = GW_2026_BeaconParser::parse_beacon(f, false);
        CHECK("M6 carrier TMI=15 -> rejected", !r.present);
    }
    std::printf(g_fail ? "RESULT: %d FAIL\n" : "RESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
