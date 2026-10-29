#include <QCoreApplication>
#include <cstdio>
#include "nw_2021_msdu_parser.h"
#include "common/fieldtools.h"
#include "common/crc.h"
static void put(QByteArray& d, int off, int bits, quint64 v) {
    set_bits(reinterpret_cast<quint8*>(d.data()), off, 0, bits, v);
}
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QByteArray mme_data(6, char(0));
    mme_data[0] = char(255); mme_data[1] = char(0x99); mme_data[2] = char(0x99);
    mme_data[3] = char(0xa1); mme_data[4] = char(0xb2); mme_data[5] = char(0xc3);
    QByteArray payload(18, char(0));
    payload.replace(0, 6, QByteArray::fromHex("102030405060"));
    payload.replace(6, 6, QByteArray::fromHex("aabbccddeeff"));
    put(payload, 12, 32, 0x8100); put(payload, 16, 16, 0x88e1);
    payload += mme_data;
    QByteArray header(32, char(0));
    header[0] = 2; put(header, 2, 16, quint64(payload.size()));
    QByteArray crc(4, char(0));
    put(crc, 0, 32, crc32_le(reinterpret_cast<const quint8*>(payload.constData()), int(payload.size()) + 4));
    const auto info = NW_2021_MsduParser::parse(header + payload + crc);
    std::printf("summary: %s\ntree.size=%d\n", qPrintable(info.summary), int(info.tree.size()));
    for (const auto& n : info.tree) std::printf("  node: %s = %s\n", qPrintable(n.name), qPrintable(n.value));
    return 0;
}
