// pb0_gate_probe: NW_2021 PBNum=0 门归一验证探针。
// 直链真实 NW_2021_Parser，对 4 帧实证文件（0x3C 封装 + 0x3D 转义）
// 逐帧 parse，打印 pb_num/pb_size/pb_crc_ok/pb_crc_oks/msdu_body。
// 预期（修门后）：_full 帧 pb_num=1、pb_size=520、pb_crc_ok=true、
// oks=[true]、msdu_body=512B；截断帧（.bin/c）循环 break、msdu 空；
// b 帧为真实 CRC 失败（oks=[false]，body 仍组装）。
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <cstdio>
#include "nw_2021_parser.h"
#include "bplcframe.h"

static QByteArray unescape_wire(const QByteArray &raw) {
    QByteArray inner;
    for (int i = 1; i + 1 < raw.size();) {
        quint8 b = static_cast<quint8>(raw[i]);
        if (b == 0x3D) {
            ++i;
            inner.append(char(0xFF ^ static_cast<quint8>(raw[i])));
        } else {
            inner.append(char(b));
        }
        ++i;
    }
    return inner;
}

static int run_one(const char *path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("%s: OPEN FAIL\n", path);
        return 1;
    }
    QByteArray raw = f.readAll();
    QByteArray inner = unescape_wire(raw);
    BplcFrame in;
    in.meta.from_raw = false;
    in.meta.has_time_tag = false;
    in.data = inner;
    in.raw_wire = raw;
    NW_2021_Parser parser;
    MsduState msdu;
    NW_2021_Parser::Filter flt;
    NW_2021_Parser::Result r = parser.parse(in, msdu, flt);
    std::printf("%s: accept=%d pb_num=%d pb_size=%d pb_crc_ok=%d oks=[",
                path, r.accept ? 1 : 0, r.mpdu.pb_num, r.mpdu.pb_size,
                r.mpdu.pb_crc_ok ? 1 : 0);
    for (int i = 0; i < r.mpdu.pb_crc_oks.size(); ++i)
        std::printf("%s%d", i ? "," : "", r.mpdu.pb_crc_oks[i] ? 1 : 0);
    std::printf("] msdu_body=%lld msdu.present=%d",
                (long long)r.msdu_body.size(), r.msdu.present ? 1 : 0);
    if (!r.accept)
        std::printf(" reject=%s", qPrintable(r.reject_reason));
    std::printf("\n");
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int rc = 0;
    for (int i = 1; i < argc; ++i)
        rc |= run_one(argv[i]);
    return rc;
}
