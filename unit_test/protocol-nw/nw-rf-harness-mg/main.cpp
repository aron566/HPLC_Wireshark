// 南网 RF SOF 后段坐标验证 + 抓包影响扫描
// 模式1(无参):合成帧断言  模式2(argv[1]=bin):全量扫描统计
#include <QCoreApplication>
#include <QFile>
#include <QByteArray>
#include <cstdio>
#include "bplcframe.h"
#include "nw_2021_parser.h"
#include "nw_2021_tree.h"
#include "nw_2021_tree.h"
#include "nw_2021_pb_table.h"
#include "common/fieldtools.h"
#include "crc.h"

static void fix_fch_crc(quint8* p) {
    p[13] = p[14] = p[15] = 0;
    quint32 c = crc24_lsb(p, 16);
    p[13] = (quint8)(c & 0xFF); p[14] = (quint8)((c >> 8) & 0xFF); p[15] = (quint8)((c >> 16) & 0xFF);
}

static int synth() {
    int fails = 0;
    // --- RF SOF(表131):重传在 (7,5);字节10 bit6=0 作鉴别位(旧坐标会读成 0) ---
    {
        QByteArray mpdu(16 + 136, 0);
        quint8* p = reinterpret_cast<quint8*>(mpdu.data());
        p[0] = 0x01;                                   // SOF
        set_bits(p, 1, 0, 12, 1);                      // 源 TEI
        set_bits(p, 2, 4, 12, 2);                      // 目的 TEI
        set_bits(p, 5, 0, 12, 0x123);                  // 帧长
        set_bits(p, 6, 4, 4, 3);                       // 载荷PB大小=3 -> 136B
        p[7] = 0xF2;                                   // MCS=2,TEI过滤=1,重传=1,保留=3
        p[10] = 0x3E; p[11] = 0xEF;                    // 保留区垃圾值(bit6=0 鉴别)
        p[12] = 0x19;                                  // 保留低4=9,版本=1
        p[16] = 0xC0;                                  // PB 头
        fix_fch_crc(p);
        BplcFrame in; in.meta.from_raw = true;
        in.data = QByteArray(1, (char)1) + mpdu;       // is_rf=1
        NW_2021_Parser parser; MsduState st; ParseFilter f;
        auto r = parser.parse(in, st, f);
        bool ok = r.accept && r.mpdu.re_send_flag && r.mpdu.symbol_num == 0
                  && r.mpdu.tmi_ext == 0x9 && r.mpdu.frame_len == 0x123
                  && r.mpdu.pblen == 3 && r.mpdu.pb_size == 136 && r.mpdu.pb_num == 1;
        std::printf("[RF SOF] accept=%d resend=%d sym=%d ext=%d flen=0x%x pblen=%d pbsize=%d pbnum=%d => %s\n",
                    r.accept, r.mpdu.re_send_flag, r.mpdu.symbol_num, r.mpdu.tmi_ext,
                    r.mpdu.frame_len, r.mpdu.pblen, r.mpdu.pb_size, r.mpdu.pb_num,
                    ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    }
    // --- 载波 SOF 回归(表16):字段坐标不变 ---
    {
        QByteArray mpdu(16 + 136, 0);
        quint8* p = reinterpret_cast<quint8*>(mpdu.data());
        p[0] = 0x01;
        set_bits(p, 1, 0, 12, 1);
        set_bits(p, 2, 4, 12, 2);
        set_bits(p, 7, 0, 4, 1);                       // PB Num=1
        set_bits(p, 7, 4, 4, 3);                       // TMI=3
        set_bits(p, 8, 0, 12, 0x100);                  // 帧长
        set_bits(p, 10, 6, 1, 1);                      // 重传
        set_bits(p, 10, 7, 9, 123);                    // 符号数
        p[12] = 0x10;                                  // TMI_EXT=0,版本=1
        p[16] = 0xC0;
        fix_fch_crc(p);
        BplcFrame in; in.meta.from_raw = true;
        in.data = QByteArray(1, (char)0) + mpdu;       // is_rf=0
        NW_2021_Parser parser; MsduState st; ParseFilter f;
        auto r = parser.parse(in, st, f);
        const int exp_pb = nw_2021_pb_size(3, 0);
        bool ok = r.accept && r.mpdu.re_send_flag && r.mpdu.symbol_num == 123
                  && r.mpdu.tmi_ext == 0 && r.mpdu.pb_num == 1 && r.mpdu.tmi == 3
                  && r.mpdu.pb_size == exp_pb;
        std::printf("[PLC SOF] accept=%d resend=%d sym=%d ext=%d pbnum=%d tmi=%d pbsize=%d(exp %d) => %s\n",
                    r.accept, r.mpdu.re_send_flag, r.mpdu.symbol_num, r.mpdu.tmi_ext,
                    r.mpdu.pb_num, r.mpdu.tmi, r.mpdu.pb_size, exp_pb,
                    ok ? "PASS" : "FAIL");
        if (!ok) ++fails;
    }
    std::printf("SYNTH RESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
    return fails;
}

static bool extract_frame(QByteArray& buf, QByteArray& out) {
    while (!buf.isEmpty()) {
        int idx = buf.indexOf(char(0x3C));
        if (idx < 0) { buf.clear(); return false; }
        buf.remove(0, idx + 1);
        idx = buf.indexOf(char(0x3E));
        if (idx < 0) return false;
        QByteArray esc = buf.left(idx);
        buf.remove(0, idx + 1);
        QByteArray unesc;
        for (int i = 0; i < esc.size(); ++i) {
            quint8 b = (quint8)esc[i];
            if (b == 0x3D && i + 1 < esc.size()) { ++i; unesc.append((char)(0xFF ^ (quint8)esc[i])); }
            else unesc.append((char)b);
        }
        out = unesc;
        return true;
    }
    return false;
}

static int scan(const QString& path, bool time_tag) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { std::printf("cannot open %s\n", qPrintable(path)); return 1; }
    QByteArray buf = file.readAll();
    NW_2021_Parser parser;
    long total = 0, accepted = 0, rf_sof = 0, resend_diff = 0, sym_nonzero_old = 0;
    QByteArray raw;
    while (extract_frame(buf, raw)) {
        ++total;
        BplcFrame in; in.data = raw; in.meta.has_time_tag = time_tag;
        MsduState st; ParseFilter f;
        auto r = parser.parse(in, st, f);
        if (!r.accept) continue;
        ++accepted;
        if (r.meta.is_rf && r.mpdu.frame_type == (quint8)NW_2021_FrameType::SOF) {
            ++rf_sof;
            const quint8* p = reinterpret_cast<const quint8*>(r.payload_for_log.constData());
            if (r.payload_for_log.size() > 11) {
                int new_bit = (p[7] >> 5) & 1, old_bit = (p[10] >> 6) & 1;
                if (new_bit != old_bit) ++resend_diff;
                int old_sym = (int)get_bits(p, 10, 7, 9);
                if (old_sym != 0) ++sym_nonzero_old;
            }
        }
    }
    std::printf("SCAN(time_tag=%d): frames=%ld accepted=%ld rf_sof=%ld resend_diff=%ld old_sym_nonzero=%ld\n",
                time_tag, total, accepted, rf_sof, resend_diff, sym_nonzero_old);
    return 0;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc > 1) {
        scan(QString::fromLocal8Bit(argv[1]), false);
        scan(QString::fromLocal8Bit(argv[1]), true);
        return 0;
    }
    return synth();
}
