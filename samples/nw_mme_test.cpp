#include <QCoreApplication>
#include <QTextStream>
#include <cstdlib>
#include "nw_2021_msdu_parser.h"
#include "common/fieldtools.h"
#include "common/crc.h"

static int checks = 0;

static void check(bool ok, const QString& context) {
    ++checks;
    if (!ok) {
        QTextStream(stderr) << "FAIL: " << context << Qt::endl;
        std::exit(1);
    }
}

static void put(QByteArray& data, int offset, int bits, quint64 value) {
    check(offset >= 0 && offset + (bits + 7) / 8 <= data.size(), "fixture write bounds");
    set_bits(reinterpret_cast<quint8*>(data.data()), offset, 0, bits, value);
}

static QByteArray mme(quint16 type, int length) {
    QByteArray data(length, char(0));
    if (length > 0) data[0] = 1;
    if (length > 1) data[1] = char(type & 0xff);
    if (length > 2) data[2] = char(type >> 8);
    if (length > 3) data[3] = char(0xa1);
    if (length > 4) data[4] = char(0xb2);
    if (length > 5) data[5] = char(0xc3);
    return data;
}

static QByteArray frame(const QByteArray& mme_data) {
    QByteArray payload(18, char(0));
    payload.replace(0, 6, QByteArray::fromHex("102030405060"));
    payload.replace(6, 6, QByteArray::fromHex("aabbccddeeff"));
    put(payload, 12, 32, 0x8100);
    put(payload, 16, 16, 0x88e1);
    payload += mme_data;
    QByteArray header(32, char(0));
    header[0] = 2; // 标准帧、长 MAC 头
    put(header, 2, 16, quint64(payload.size()));
    QByteArray crc(4, char(0));
    put(crc, 0, 32, crc32_le(reinterpret_cast<const quint8*>(payload.constData()),
                            int(payload.size()) + 4));
    return header + payload + crc;
}

static const MsduFieldNode* node(const MsduInfo& info, const QString& name) {
    for (const auto& field : info.tree)
        if (field.name == name) return &field;
    return nullptr;
}

static void check_header(const MsduInfo& info, const QByteArray& data) {
    const auto* version = node(info, "MMe Version [8b]");
    const auto* type = node(info, "MMe Type [16b]");
    const auto* reserved = node(info, "RSV [24b]");
    const quint16 mm_type = quint16(get_bits(data, 1, 0, 16));
    check(info.mme_type == mm_type && version && type && reserved,
          "complete common header retained");
    check(version->rel_start == 50 && version->rel_len == 1 && version->value == "1"
          && type->rel_start == 51 && type->rel_len == 2
          && reserved->rel_start == 53 && reserved->rel_len == 3
          && reserved->value == "0xA1B2C3", "common header value/range unchanged");
}

static MsduInfo parse(const QByteArray& data) {
    const MsduInfo info = NW_2021_MsduParser::parse(frame(data));
    check(info.present, "outer MSDU declaration is complete");
    const auto* crc = node(info, "MSDU CRC32");
    check(crc && crc->value.endsWith("OK"), "CRC retained even for truncated MMe");
    if (data.size() >= 6) check_header(info, data);
    return info;
}

static void truncated(const QByteArray& data) {
    const auto info = parse(data);
    const auto& event = info.topo_event;
    const QString context = QString("type=0x%1 length=%2")
        .arg(data.size() >= 3 ? get_bits(data, 1, 0, 16) : 0, 4, 16, QChar('0'))
        .arg(data.size());
    check(info.summary.endsWith(" (truncated)"), "truncation marked: " + context);
    check(event.kind == TopoEventKind::Other && event.nodes.isEmpty()
          && event.routes.isEmpty() && event.up_routes.isEmpty()
          && event.leaves.isEmpty() && event.comm_rates.isEmpty()
          && event.neighbor_teis.isEmpty() && event.discover_src_tei == 0
          && event.cco_mac == 0 && event.restart_count == -1
          && event.desc.isEmpty() && info.tei_mac_pairs.isEmpty(),
          "no partial topology or TEI/MAC learning: " + context);
}

static MsduInfo complete(const QByteArray& data) {
    const auto info = parse(data);
    check(!info.summary.contains("truncated"), "complete MMe accepted");
    return info;
}

static void all_prefixes(const QByteArray& data) {
    for (int length = 0; length < data.size(); ++length)
        truncated(data.left(length));
    complete(data);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    struct FixedCase { quint16 type; int minimum; TopoEventKind kind; };
    const FixedCase fixed[] = {
        {0x0030, 74, TopoEventKind::AssocReq},
        {0x0031, 50, TopoEventKind::AssocCnf},
        {0x0032, 46, TopoEventKind::Other},
        {0x0034, 70, TopoEventKind::AssocInd},
        {0x0037, 38, TopoEventKind::ChangeProxyCnf},
        {0x003a, 34, TopoEventKind::AssocGatherInd},
        {0x003b, 154, TopoEventKind::ChangeProxyCnf},
        {0x0049, 26, TopoEventKind::LeaveInd},
        {0x0051, 145, TopoEventKind::Other},
        {0x0055, 176, TopoEventKind::DiscoverList},
        {0x005d, 22, TopoEventKind::LeaveInd},
        {0x005e, 10, TopoEventKind::SuccessRate},
        {0x005f, 15, TopoEventKind::Other},
        {0x0062, 10, TopoEventKind::Other},
        {0x0063, 18, TopoEventKind::Other},
        {0x0064, 8, TopoEventKind::Other},
        {0x0070, 13, TopoEventKind::Other},
    };
    for (const auto& test : fixed) {
        all_prefixes(mme(test.type, test.minimum));
        const auto info = complete(mme(test.type, test.minimum));
        check(info.topo_event.kind == test.kind, "valid topology classification unchanged");
        complete(mme(test.type, test.minimum + 1));
    }

    struct ListCase { quint16 type; int base; int count_offset; int count_bits; int item_size; };
    const ListCase lists[] = {
        {0x0031, 50, 42, 16, 2},
        {0x0037, 38, 16, 16, 2},
        {0x003a, 34, 17, 8, 8},
        {0x0055, 176, 30, 16, 1},
        {0x005d, 22, 8, 16, 6},
        {0x005e, 10, 8, 16, 4},
        {0x0063, 18, 15, 8, 2},
        {0x0063, 18, 16, 8, 2},
        {0x0063, 18, 17, 8, 2},
        {0x0070, 13, 12, 8, 2},
    };
    for (const auto& test : lists) {
        auto data = mme(test.type, test.base + 2 * test.item_size);
        put(data, test.count_offset, test.count_bits, 2);
        all_prefixes(data);
        put(data, test.count_offset, test.count_bits, 3);
        truncated(data); // 已存在完整条目也不得部分更新拓扑
        put(data, test.count_offset, test.count_bits, test.count_bits == 8 ? 255 : 65535);
        truncated(data);
    }

    // 两个 PCO 的嵌套路由:第二个子列表不足时,不得保留第一个事件。
    auto routes = mme(0x0031, 64);
    put(routes, 42, 16, 1);
    put(routes, 44, 16, 2);
    put(routes, 50, 16, 7);
    put(routes, 52, 16, 8);
    put(routes, 54, 16, 2);
    put(routes, 56, 16, 9);
    put(routes, 58, 16, 10);
    put(routes, 60, 16, 11);
    put(routes, 62, 16, 0);
    all_prefixes(routes);
    put(routes, 62, 16, 1);
    truncated(routes);
    routes.append(QByteArray(2, char(0)));
    put(routes, 64, 16, 12);
    all_prefixes(routes);
    put(routes, 62, 16, 65535);
    truncated(routes);
    auto missing_pco = mme(0x0031, 50);
    put(missing_pco, 44, 16, 65535);
    truncated(missing_pco);

    auto gather = mme(0x003a, 50);
    put(gather, 14, 16, 3);
    put(gather, 17, 8, 2);
    put(gather, 34, 48, 0x112233445566);
    put(gather, 40, 12, 5);
    put(gather, 42, 48, 0x223344556677);
    put(gather, 48, 12, 6);
    all_prefixes(gather);
    const auto gathered = complete(gather);
    check(gathered.topo_event.nodes.size() == 2 && gathered.topo_event.routes.size() == 2,
          "complete association list keeps both nodes/routes");
    put(gather, 17, 8, 3);
    truncated(gather);

    auto discover = mme(0x0055, 185);
    put(discover, 6, 16, 5);
    put(discover, 10, 48, 0x112233445566);
    put(discover, 30, 16, 3);
    put(discover, 34, 16, 2);
    put(discover, 48, 12, 1);
    put(discover, 50, 8, 3);
    put(discover, 51, 12, 2);
    put(discover, 53, 8, 3);
    put(discover, 54, 8, 0x0e);
    all_prefixes(discover);
    const auto discovered = complete(discover);
    check(discovered.topo_event.up_routes.size() == 2
          && discovered.topo_event.neighbor_teis.size() == 3
          && discovered.tei_mac_pairs.size() == 1,
          "complete discovery retains routes/neighbors/TEI-MAC learning");
    put(discover, 34, 16, 65535);
    truncated(discover);

    auto phases = mme(0x0063, 30);
    put(phases, 15, 8, 1);
    put(phases, 16, 8, 2);
    put(phases, 17, 8, 3);
    all_prefixes(phases);
    put(phases, 17, 8, 4);
    truncated(phases);

    for (int length = 0; length < 6; ++length) truncated(mme(0x9999, length));
    const auto unknown = complete(mme(0x9999, 6));
    check(unknown.topo_event.kind == TopoEventKind::Other, "unknown MMe stays non-topological");
    complete(mme(0x9999, 7));
    const auto full_frame = frame(mme(0x0031, 50));
    for (int length = 0; length < full_frame.size() - 4; ++length) {
        const auto info = NW_2021_MsduParser::parse(full_frame.left(length));
        check(!info.present && info.topo_event.kind == TopoEventKind::Other,
              "physically truncated outer MSDU rejected");
    }
    QTextStream(stdout) << "PASS: " << checks << " NW MMe checks" << Qt::endl;
    return 0;
}
