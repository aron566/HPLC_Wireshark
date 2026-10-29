// L7(a) 探针:分集拷贝模式表(用户 2026-10-28 提供)逐项断言 + 门迁移断言。
// 不依赖 Qt 库,仅需 QtGlobal 类型头。
#include "gw_2026_pb_table.h"
#include <cstdio>

int main() {
    // 用户表原文:模式 0-28 → 物理块类型(字节)
    static const int expect[29] = {
        520, 264, 136, 72,  520, 264, 136, 72,  520, 264, 136, 72,  520, 264, 136,
        72,  520, 264, 136, 72,  520, 264, 136, 72,  520, 264, 136, 72,  520
    };
    int fail = 0;
    for (int m = 0; m <= 28; ++m) {
        int got = gw_2026_diversity_pb_size((quint8)m);
        if (got != expect[m]) { std::printf("FAIL mode %d: got %d expect %d\n", m, got, expect[m]); ++fail; }
    }
    if (gw_2026_diversity_pb_size(29) != -1) { std::puts("FAIL mode 29 should be -1"); ++fail; }
    if (gw_2026_diversity_pb_size(31) != -1) { std::puts("FAIL mode 31 should be -1"); ++fail; }

    // 门迁移:旧表 0-14 列值必须原样保留(4 频段 × 15 列)
    static const quint8 old_gate[4][15] = {
        { 4, 4, 4, 4, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4 },
        { 3, 4, 4, 2, 4, 4, 4, 1, 1, 2, 3, 4, 2, 4, 4 },
        { 2, 4, 4, 1, 2, 3, 4, 0, 1, 1, 2, 2, 1, 4, 4 },
        { 1, 2, 3, 0, 1, 1, 2, 0, 0, 0, 1, 1, 0, 4, 2 },
    };
    for (int b = 0; b < 4; ++b)
        for (int m = 0; m <= 14; ++m)
            if (gw_2026_max_pb_num((quint8)b, (quint8)m) != old_gate[b][m]) {
                std::printf("FAIL gate band %d mode %d\n", b, m); ++fail;
            }
    // 新模式 15-28:附注封顶 4;保留模式 29-31:-1;频段越界:-1
    for (int b = 0; b < 4; ++b)
        for (int m = 15; m <= 28; ++m)
            if (gw_2026_max_pb_num((quint8)b, (quint8)m) != 4) {
                std::printf("FAIL gate-new band %d mode %d\n", b, m); ++fail;
            }
    if (gw_2026_max_pb_num(0, 29) != -1 || gw_2026_max_pb_num(4, 0) != -1) {
        std::puts("FAIL gate invalid"); ++fail;
    }
    // 旧行为对照:旧 TMI 表仅 0/2/6/8 四模式与新表一致(其余皆错或拒收)
    static const int old_tmi[15] = {520,520,136,136,136,136,136,520,520,520,520,264,264,72,72};
    int same = 0;
    for (int m = 0; m <= 14; ++m) if (old_tmi[m] == expect[m]) ++same;
    std::printf("old-table matches new-table on %d/15 modes (expect 4: 0/2/6/8)\n", same);
    if (same != 4) ++fail;

    std::printf(fail ? "PROBE FAIL (%d)\n" : "PROBE PASS\n", fail);
    return fail ? 1 : 0;
}
