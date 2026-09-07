/// @file sofparser.cpp
/// @brief SOF 帧解析实现(独立帧解析模块,sofparser.pri)
/// @details 移植自 BPLCMonitor/MPDU_Class.py 的 MPDU_SOF 逻辑:
///          FCH 内业务字段位域 + 逐 PB 块 CRC24 校验与 MSDU 重组
///          (PB 头 START/END/seq 语义)。公共位域/CRC 工具见 fieldspec.h。
#include "sofparser.h"
#include "fieldspec.h"
#include "i18n.h"
#include <algorithm>

namespace {

// SOF PB 块长:TMI 主表 + TMI_EXT 扩展(与 Python MPDU_Class 一致)
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

}  // namespace

namespace sof {

QString assemble(const QByteArray& body, MpduInfo& info, MsduState& msdu,
                 QByteArray& complete_msdu_body) {
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    // FCH 业务字段(相对 FCH 起点)
    info.src_tei      = (quint16)get_bits(p, 4, 0, 12);
    info.dst_tei      = (quint16)get_bits(p, 5, 4, 12);
    info.link_id      = (quint8) get_bits(p, 7, 0, 8);
    info.frame_len    = (quint16)get_bits(p, 8, 0, 12);
    info.pb_num       = (quint8) get_bits(p, 9, 4, 4);
    info.symbol_num   = (quint16)get_bits(p, 10, 0, 9);
    info.bc_flag      = (bool)   get_bits(p, 11, 1, 1);
    info.re_send_flag = (bool)   get_bits(p, 11, 2, 1);
    info.encryp_flag  = (bool)   get_bits(p, 11, 3, 1);
    info.tmi          = (quint8) get_bits(p, 11, 4, 4);
    info.tmi_ext      = (quint8) get_bits(p, 12, 0, 4);
    info.pb_size      = (quint16)sof_pb_size(info.tmi, info.tmi_ext);

    if (info.pb_size <= 0 || info.pb_num == 0 || info.pb_num > 4) {
        return QString("PB 配置非法(tmi=%1 ext=%2 num=%3)")
                   .arg(info.tmi).arg(info.tmi_ext).arg(info.pb_num);
    }

    int block_offset = 16;
    bool all_pb_ok = true;
    for (int i = 0; i < info.pb_num; ++i) {
        int block_start = block_offset + i * info.pb_size;
        if (block_start + info.pb_size > body.size()) {
            msdu = MsduState{};
            return trl::L("PB 块超出帧长");
        }
        const quint8* blk =
            reinterpret_cast<const quint8*>(body.constData()) + block_start;
        quint32 calc = crc24_lsb(blk, info.pb_size);
        quint32 rx   = (quint32)blk[info.pb_size - 3]
                     | ((quint32)blk[info.pb_size - 2] << 8)
                     | ((quint32)blk[info.pb_size - 1] << 16);
        if (calc != rx) {
            all_pb_ok = false;
            // 失败块同样记录,供 UI 逐块展示(头仍可读,CRC 状态 FAIL)
            info.pb_heads.append(blk[0]);
            info.pb_crc_oks.append(false);
            continue;
        }
        quint8 pb_head = blk[0];
        bool   is_start = (pb_head & 0x40) != 0;
        bool   is_end   = (pb_head & 0x80) != 0;
        quint8 seq     = pb_head & 0x3F;
        // 保存首块(起始块)的 PB 头原始字节供 UI 显示;多块时后块不覆盖
        if (i == 0) info.pb_head = pb_head;
        info.pb_heads.append(pb_head);       // 按块序保存(UI 逐块展示)
        info.pb_crc_oks.append(true);        // 各块 CRC24 结果(此处已通过)

        int body_len = info.pb_size - 4;
        QByteArray pb_body(reinterpret_cast<const char*>(blk + 1), body_len);

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

}  // namespace sof

// ===== i18n:SOF 模块 中→英 词典 =====
namespace {
struct I18nRegSof {
    I18nRegSof() {
        trl::register_en("PB 块超出帧长", "PB block exceeds frame length");
    }
};
const I18nRegSof g_i18n_reg_sof;
}  // namespace
