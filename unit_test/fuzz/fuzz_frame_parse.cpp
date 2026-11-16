/// @file fuzz_frame_parse.cpp
/// @brief libFuzzer 目标:帧解析入口(get_bits / PB 组装 / add_fields)。
/// @details 最小可用目标,只吃 header-only 的协议公共层(fieldtools.h /
///          fieldspec.h / pb_assembler.h / crc.h),另加 src/common/i18n.cpp
///          (fieldspec.h 的截断标记走 trl::L)。不依赖整机解析器与 UI。
///
///          输入即原始帧字节。头 3 字节兼作组装参数(其余字节仍是帧体):
///            data[0] bit0: 0=国网布局(HDC 1B 块头),1=南网布局(SPLC 4B 块头)
///            data[1]:      PB 块数 1..8
///            data[2]:      PB 块大小 16..215B
///          其后字节:随机 (start_byte/start_bit/bit_len) 三元组,驱动
///          QByteArray 版 get_bits(即 H1 加边界检查的那一路;裸指针版
///          get_bits_unchecked 不在此目标里直接调用)。
///          最后对同一份输入跑一遍 add_fields(小 spec 表),覆盖 L3 的
///          截断标记分支(短输入应追加“(字段截断)”节点而非静默丢弃)。
///          单次执行工作量有界:至多 8 块 × 215B + 16 次 get_bits + 1 次
///          add_fields,与输入长度无关(超 64KiB 的输入直接丢弃)。
/// @note 构建见本目录 build.sh,需要带 -fsanitize=fuzzer 的 clang
///       (本机 g++ 不支持该选项,见 README.md)。
#include <cstddef>
#include <cstdint>

#include <QByteArray>

#include "fieldspec.h"
#include "fieldtools.h"
#include "pb_assembler.h"

namespace {

// 国网 HDC 2.0:块 = pb_head 1B + 块体 + CRC24 3B;序列号低 6 位,
// bit6=START、bit7=END;块起点 FCH 16B。
const pb_asm::PbLayout kGwLayout = {
    /*head_len=*/1, /*tail_rsv_len=*/0, /*crc_len=*/3,
    /*agg_flag_byte=*/-1, /*agg_flag_bit=*/-1, /*block_base=*/16,
    /*seq_byte=*/0, /*seq_mask=*/0x3F, /*start_bit=*/6, /*end_bit=*/7,
};

// 南网 SPLC:块 = PB 头 4B(表47) + 块体 + 保留 1B + CRC24 3B;
// 序列号 16bit(LE),聚合标志 = PB 头 byte2 bit0;块起点 FCH 16B。
const pb_asm::PbLayout kNwLayout = {
    /*head_len=*/4, /*tail_rsv_len=*/1, /*crc_len=*/3,
    /*agg_flag_byte=*/2, /*agg_flag_bit=*/0, /*block_base=*/16,
    /*seq_byte=*/0, /*seq_mask=*/0xFFFF, /*start_bit=*/-1, /*end_bit=*/-1,
};

const FieldSpec kFuzzSpec[] = {
    {"F0", 0, 0, 12, Fmt::DEC},
    {"F1", 1, 4, 8,  Fmt::HEX8},
    {"F2", 3, 0, 48, Fmt::MAC},
    {"F3", 9, 0, 125, Fmt::HEX4},  // 宽字段路径(>64 位,不走 get_bits)
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 3 || size > 65536)
        return 0;
    const QByteArray raw(reinterpret_cast<const char*>(data), int(size));

    // (1) PB 组装路径:参数取自头 3 字节并箝制到合法小范围。
    const pb_asm::PbLayout& layout = (data[0] & 1) ? kNwLayout : kGwLayout;
    const int pb_num = 1 + (data[1] % 8);
    const int pb_size = 16 + (data[2] % 200);
    const pb_asm::AssembleResult res =
        pb_asm::assemble_pb_blocks(raw, pb_num, pb_size, layout);
    // 消费结果,防止被优化掉(并覆盖重组/级联统计分支)。
    volatile bool sink = res.truncated || res.reassembly_ok
        || res.any_aggregated || !res.all_crc_ok;
    volatile int sink_n = int(res.blocks.size() + res.msdu_body.size()
                              + res.cascade.size());
    (void)sink;
    (void)sink_n;

    // (2) QByteArray 版 get_bits:三元组来自输入尾部,坐标可任意越界,
    // 越界时应返 0 而不是读出缓冲区(H1 语义)。
    const size_t triples = (size - 3) / 3;
    const size_t n = triples < 16 ? triples : 16;
    volatile quint64 bits_sink = 0;
    for (size_t i = 0; i < n; ++i) {
        const int start_byte = int(data[3 + i * 3]);
        const int start_bit = int(data[3 + i * 3 + 1] % 8);
        const int bit_len = 1 + int(data[3 + i * 3 + 2] % 64);
        bits_sink ^= get_bits(raw, start_byte, start_bit, bit_len);
    }
    (void)bits_sink;

    // (3) add_fields:spec 表需 25B,短输入走 L3 截断标记分支。
    QVector<MsduFieldNode> nodes;
    add_fields(nodes, raw, 0, kFuzzSpec, 4);
    volatile int sink_nodes = int(nodes.size());
    (void)sink_nodes;
    return 0;
}
