/// @file sofparser.cpp
/// @brief SOF 帧解析实现(独立帧解析模块,sofparser.pri)
/// @details HDC 2.0:SOF 按载波表7-表13/无线表21 分介质解析 FCH 可变区域,
///          载波再按帧子类型(表7 byte7 bit0-2)分派:数据帧/信道探测帧(表9)/
///          OFDMA 下行帧(表10/11)/TDA 下行帧(表12/13);链路标识符已移至
///          MAC 帧头 byte15(表2)。逐 PB 块 CRC24 校验与 MSDU 重组
///          (PB 头 START/END/seq 语义)。公共位域/CRC 工具见 fieldspec.h。
#include "gw_2026_sof_parser.h"
#include "gw_2026_pb_table.h"
#include "gw_2026_tree.h"
#include "crc.h"
#include "pb_assembler.h"
#include "i18n.h"
#include "fieldtools.h"
#include <algorithm>

namespace {

// SOF PB 块长:TMI 主表 + TMI_EXT 扩展(与 Python MPDU_Class 一致;
// HDC 2.0 载波分集拷贝模式的调制参数仍为 XXX.41 TMI 编号,表7 byte10 bit2-6)
int sof_pb_size(quint8 tmi, quint8 tmi_ext) {
    if (tmi == 0 || tmi == 1)                          return 520;
    if (tmi >= 2 && tmi <= 6)                          return 136;
    if (tmi >= 7 && tmi <= 10)                         return 520;
    if (tmi == 11 || tmi == 12)                        return 264;
    if (tmi == 13 || tmi == 14)                        return 72;
    if (tmi_ext >= 1 && tmi_ext <= 6)                  return 520;
    if (tmi_ext >= 10 && tmi_ext <= 14)                return 136;
    return -1;
}

// 无线 SOF 载荷PB大小(表29):索引 0-5 对应 16/40/72/136/264/520 字节
int sof_rf_pb_size(quint8 idx) {
    static const quint16 table[] = {16, 40, 72, 136, 264, 520};
    return (idx <= 5) ? int(table[idx]) : -1;
}

// 载波 PB 个数 2b 编码 → 块数(表7:0-3 = 1-4 个物理块)
inline int sof_pb_num_decode(quint8 code) { return code + 1; }

// 动态加载模式的调制参数 = 物理块类型编号(表8:0-3 = PB72/136/264/520)
int sof_dynamic_pb_size(quint8 type) {
    static const quint16 table[] = {72, 136, 264, 520};
    return (type <= 3) ? int(table[type]) : -1;
}

}  // namespace

namespace gw_2026_sof {

QString assemble(const QByteArray& body, MpduInfo& info, MsduState& msdu,
                 QByteArray& complete_msdu_body, quint8 band, bool is_rf) {
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    // FCH 业务字段(相对 FCH 起点;载波/无线共用前段:源/目的 TEI + 帧长)
    info.src_tei      = (quint16)get_bits(p, 4, 0, 12);
    info.dst_tei      = (quint16)get_bits(p, 5, 4, 12);
    info.frame_len    = (quint16)get_bits(p, 8, 0, 12);
    // 链路标识符(HDC 2.0 移至 MAC 帧头 byte15,表2;FCH 内不再有此字段)
    info.link_id      = 0;

    int pb_size;
    if (is_rf) {
        // 无线 SOF(表21,布局同 HDC 1.0 表30):固定 1 块;字节9 bit4-7 为
        // 载荷PB大小(表29),字节11 bit4-7 为 MCS(表28),其余保留(存原始值)
        info.pb_num_field = (quint8) get_bits(p, 9, 4, 4);
        info.bc_flag      = (bool)   get_bits(p, 11, 1, 1);
        info.re_send_flag = (bool)   get_bits(p, 11, 2, 1);
        info.encryp_flag  = (bool)   get_bits(p, 11, 3, 1);
        info.mcs          = (quint8) get_bits(p, 11, 4, 4);
        info.pb_num       = 1;
        info.symbol_num = (quint16)get_bits(p, 10, 0, 9);   // 保留(原始值)
        info.tmi_ext    = (quint8) get_bits(p, 12, 0, 4);   // 保留(原始值)
        pb_size         = sof_rf_pb_size(info.pb_num_field);
    } else {
        // 载波 SOF(表7 数据帧):帧子类型/TF描述/帧长后为 FCH 符号数(跨字节)/
        // 调制方式与参数/2b 编码 PB 个数/广播·加密·重传/频段标识/增益/加载表ID
        info.sof_data_type     = (quint8) get_bits(p, 7, 0, 3);
        info.sof_tf_symbol_pos = (quint8) get_bits(p, 7, 3, 2);
        info.sof_tf_symbol_num = (quint8) get_bits(p, 7, 5, 3);
        // FCH符号数(9,7,2) 跨字节:byte9 bit7 + byte10 bit0(表7)
        info.sof_fch_sym_num   = (quint8)((get_bits(p, 9, 7, 1))
                                       | (get_bits(p, 10, 0, 1) << 1));
        info.sof_mod_mode      = (quint8) get_bits(p, 10, 1, 1);
        info.sof_mod_param     = (quint8) get_bits(p, 10, 2, 5);
        // PB 个数 2b 编码:(10,7)+(11,0) 跨字节 → 0-3 = 1-4 块
        // (68-bit 位流连续排布:…参数(10,2-6)|PB个数(10,7)+(11,0)|广播(11,1)…)
        const quint8 pb_code   = (quint8)((get_bits(p, 10, 7, 1))
                                       | (get_bits(p, 11, 0, 1) << 1));
        info.pb_num_field      = pb_code;
        info.pb_num            = sof_pb_num_decode(pb_code);
        info.bc_flag           = (bool)   get_bits(p, 11, 1, 1);
        info.encryp_flag       = (bool)   get_bits(p, 11, 2, 1);
        info.re_send_flag      = (bool)   get_bits(p, 11, 3, 1);
        info.sof_band          = (quint8) get_bits(p, 11, 4, 3);
        info.sof_gain          = (quint8)((get_bits(p, 11, 7, 1))
                                       | (get_bits(p, 12, 0, 3) << 1));
        info.sof_load_table_id = (quint8) get_bits(p, 12, 3, 1);
        info.symbol_num = 0;
        info.tmi = info.sof_mod_param;   // 分集拷贝模式:调制参数 = TMI 编号(查 PB 大小)
        info.tmi_ext = 0;

        // PB 大小按调制方式分派(枚举驱动):分集拷贝=调制参数查 TMI 表;
        // 动态加载=调制参数查物理块类型表(表8)
        switch (static_cast<GW_2026_ModMode>(info.sof_mod_mode)) {
            case GW_2026_ModMode::DIVERSITY_COPY:
                pb_size = sof_pb_size(info.tmi, info.tmi_ext);
                break;
            case GW_2026_ModMode::DYNAMIC_LOAD:
                pb_size = sof_dynamic_pb_size(info.sof_mod_param);
                break;
            default:
                pb_size = -1;
                break;
        }
        // 帧子类型非数据帧(信道探测/OFDMA/TDA)的字段布局不同(表9-表13),
        // 此处仍按数据帧完成 TEI/帧长提取,载荷区按通用 PB 逐块解析展示;
        // 子类型专属字段由字段树标注(见 gw_2026_tree.cpp SOF 分支)。
    }

    const int max_pb_num = (band < 4)
        ? gw_2026_max_pb_num(band, info.tmi, info.tmi_ext) : 4;  // 非 PLC:1-4 上限
    // 块数合法性只约束载波;RF 生效块数恒 1(见上);PB 大小非法(-1)对载波/无线均拒收
    if (pb_size <= 0
        || (!is_rf && (info.pb_num == 0 || info.pb_num > max_pb_num))) {
        return QString("PB 大小/数量非法(band=%1 mod=%2 param=%3 num=%4 max=%5)")
                   .arg(band).arg(info.sof_mod_mode).arg(info.sof_mod_param)
                   .arg(info.pb_num_field).arg(max_pb_num);
    }
    info.pb_size = (quint16)pb_size;

    int block_offset = 16;
    // 逐块 CRC24 + 块体提取(公共 pb_assembler;国网布局:pb_head 1B + 块体 +
    // CRC24 3B,无聚合标志字段;序列号状态机重组逻辑保持原语义不变)
    const pb_asm::PbLayout layout{/*head*/1, /*rsv*/0, /*crc*/3,
                                  /*agg_byte*/-1, /*agg_bit*/-1,
                                  /*base*/block_offset};
    const pb_asm::AssembleResult res =
        pb_asm::assemble_pb_blocks(body, info.pb_num, info.pb_size, layout);
    if (res.truncated) {
        msdu = MsduState{};
        return trl::L("PB 块超出帧长");
    }
    const bool all_pb_ok = res.all_crc_ok;
    int block_idx = 0;   // 块序(truncated 时仅完整块,但截断已提前报错,序号一致)
    for (const auto& b : res.blocks) {
        info.pb_heads.append(b.head_byte);
        info.pb_crc_oks.append(b.crc_ok);
        if (!b.crc_ok) {   // 失败块同样记录,供 UI 逐块展示(头仍可读,CRC 状态 FAIL)
            ++block_idx;
            continue;
        }
        const quint8 pb_head = b.head_byte;
        const bool   is_start = (pb_head & 0x40) != 0;
        const bool   is_end   = (pb_head & 0x80) != 0;
        const quint8 seq      = pb_head & 0x3F;
        // 保存首块(起始块)的 PB 头原始字节供 UI 显示;多块时后块不覆盖
        if (block_idx == 0) info.pb_head = pb_head;
        const int body_len = b.body_len;
        const QByteArray pb_body = body.mid(b.body_raw_offset, body_len);
        ++block_idx;
        if (is_start && msdu.received_count == 0) {
            msdu.expected_len = body_len;
            msdu.buffer.resize(info.pb_num * body_len);
            std::fill(msdu.buffer.begin(), msdu.buffer.end(), 0);
            msdu.received_count = 1;
            std::copy(pb_body.begin(), pb_body.end(), msdu.buffer.begin());
        } else if (msdu.received_count > 0 && seq == (quint8)msdu.received_count) {
            int dst = msdu.received_count * msdu.expected_len;
            if (dst + pb_body.size() > msdu.buffer.size())
                msdu.buffer.resize(dst + pb_body.size());
            std::copy(pb_body.begin(), pb_body.end(), msdu.buffer.begin() + dst);
            msdu.received_count++;
        } else {
            // 序乱:清重组状态,视为未完成(与原实现一致,不拒收整帧)
            msdu = MsduState{};
            return QString();
        }

        if (is_end) {
            complete_msdu_body =
                msdu.buffer.left(msdu.received_count * msdu.expected_len);
            msdu = MsduState{};
            info.pb_crc_ok = all_pb_ok;
            return QString();
        }
    }
    info.pb_crc_ok = all_pb_ok;
    return QString();
}

}  // namespace gw_2026_sof

// ===== i18n:SOF 模块 中→英 词典 =====
namespace {
struct I18nRegSof {
    I18nRegSof() {
        trl::register_en("PB 块超出帧长", "PB block exceeds frame length");
    }
};
const I18nRegSof g_i18n_reg_sof;
}  // namespace
