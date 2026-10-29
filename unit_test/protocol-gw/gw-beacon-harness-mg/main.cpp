// 国网信标分介质验证:解析器断言 + 字段树渲染
#include <QApplication>
#include <QTreeWidget>
#include <cstdio>
#include "bplcframe.h"
#include "gw_2022_parser.h"
#include "gw_2022_tree.h"
#include "gw_2022_pb_table.h"
#include "common/fieldtools.h"
#include "crc.h"

static void fix_fch_crc(quint8* p) {
    p[13] = p[14] = p[15] = 0;
    quint32 c = crc24_lsb(p, 16);
    p[13] = (quint8)(c & 0xFF); p[14] = (quint8)((c >> 8) & 0xFF); p[15] = (quint8)((c >> 16) & 0xFF);
}
static void dump(QTreeWidgetItem* it, int depth) {
    std::printf("%*s%s | %s\n", depth * 2, "", qPrintable(it->text(0)), qPrintable(it->text(1)));
    for (int i = 0; i < it->childCount(); ++i) dump(it->child(i), depth + 1);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    int fails = 0;
    GW_2022_Parser parser;
    // --- RF 信标(表27):MCS=5、PB 大小码=3 ---
    ParseResult rf;
    {
        QByteArray mpdu(16 + 120, 0);
        quint8* p = reinterpret_cast<quint8*>(mpdu.data());
        p[0] = 0x00;                                   // BEACON
        set_bits(p, 8, 0, 12, 1);                      // 源 TEI
        set_bits(p, 9, 4, 4, 5);                       // MCS=5
        p[10] = 0xF3;                                  // PB 大小码=3,保留高4=F
        p[11] = 0xAB;                                  // 保留
        p[12] = 0x19;                                  // 保留低4=9,版本=1
        fix_fch_crc(p);
        BplcFrame in; in.meta.from_raw = true;
        in.data = QByteArray(1, (char)1) + mpdu;
        MsduState st; ParseFilter f;
        rf = parser.parse(in, st, f);
        bool ok = rf.accept && rf.mpdu.mcs == 5 && rf.mpdu.pb_num_field == 3
                  && rf.mpdu.pb_size == 136 && rf.mpdu.symbol_num == 0 && rf.mpdu.beacon_line == 0;
        std::printf("[RF BEACON parser] accept=%d mcs=%d code=%d pbsize=%d sym=%d line=%d => %s\n",
                    rf.accept, rf.mpdu.mcs, rf.mpdu.pb_num_field, rf.mpdu.pb_size,
                    rf.mpdu.symbol_num, rf.mpdu.beacon_line, ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    }
    // --- 载波信标(表17):TMI=3、符号数=300、相线=2 ---
    ParseResult plc;
    {
        QByteArray mpdu(16 + 120, 0);
        quint8* p = reinterpret_cast<quint8*>(mpdu.data());
        p[0] = 0x00;
        set_bits(p, 8, 0, 12, 1);
        set_bits(p, 9, 4, 4, 3);                       // TMI=3
        set_bits(p, 10, 0, 9, 300);                    // 符号数
        set_bits(p, 11, 1, 2, 2);                      // 相线
        p[11] |= 0xF8;                                 // 保留区置垃圾
        p[12] = 0x15;                                  // 保留低4=5,版本=1
        fix_fch_crc(p);
        BplcFrame in; in.meta.from_raw = true;
        in.data = QByteArray(1, (char)0) + mpdu;
        MsduState st; ParseFilter f;
        plc = parser.parse(in, st, f);
        bool ok = plc.accept && plc.mpdu.pb_size == gw_2022_pb_size(3)
                  && plc.mpdu.symbol_num == 300 && plc.mpdu.beacon_line == 2;
        std::printf("[PLC BEACON parser] accept=%d pbsize=%d sym=%d line=%d => %s\n",
                    plc.accept, plc.mpdu.pb_size, plc.mpdu.symbol_num, plc.mpdu.beacon_line,
                    ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    }
    // --- 树渲染 ---
    GW_2022_TreeBuilder builder;
    auto render = [&](const ParseResult& r, const QByteArray& raw, const char* tag) {
        PacketEntry e;
        e.meta = r.meta; e.mpdu = r.mpdu; e.beacon = r.beacon; e.raw_bytes = raw;
        QTreeWidget tw; auto* root = new QTreeWidgetItem(&tw);
        builder.build(root, e);
        std::printf("=== %s ===\n", tag); dump(root, 0);
    };
    {
        QByteArray raw(16, 0);
        quint8* p = reinterpret_cast<quint8*>(raw.data());
        set_bits(p, 9, 4, 4, 5); p[10] = 0xF3; p[11] = 0xAB; p[12] = 0x19;
        render(rf, raw, "RF BEACON tree");
    }
    {
        QByteArray raw(16, 0);
        quint8* p = reinterpret_cast<quint8*>(raw.data());
        set_bits(p, 9, 4, 4, 3); set_bits(p, 10, 0, 9, 300); set_bits(p, 11, 1, 2, 2);
        p[11] |= 0xF8; p[12] = 0x15;
        render(plc, raw, "PLC BEACON tree");
    }
    std::printf("RESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
    return fails;
}
