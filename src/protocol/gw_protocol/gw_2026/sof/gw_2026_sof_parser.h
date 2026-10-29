/// @file sofparser.h
/// @brief SOF 帧解析模块声明(HDC 2.0 SOF:FCH 字段 + PB 块/MSDU 重组)
/// @details 负责 SOF 帧 FCH 内业务字段(源/目的 TEI、帧长、载波表7 帧子类型/
///          TF/调制方式…、无线表19 载荷PB大小/MCS…)与多 PB 块的 MSDU 跨帧
///          重组(与 Python MPDU_Class 重组逻辑一致)。
///          重组状态 MsduState 定义于 bplcframe.h(由调用方持有,跨帧保活)。
#ifndef SOFPARSER_H
#define SOFPARSER_H

#include "bplcframe.h"

namespace gw_2026_sof {

/// @brief SOF FCH 字段解析 + 多 PB 重组
/// @param body      payload_for_log(自 FrameType 起,含 16B FCH)
/// @param info      [out] 填充 src/dst TEI、pb_size、pb_heads/pb_crc_oks 等
/// @param msdu      跨帧重组状态(多帧分段时由调用方在帧间保留)
/// @param complete_msdu_body [out] 重组完成的 MSDU body(未完成则为空)
/// @param band      PLC 频段号(0-3),用于查 gw_2026_pb_num_table 校验 PB 块数;
///                  0xFF(默认)= 非 PLC(HRF 等)
/// @param is_rf     是否无线信道帧:无线 SOF 走 HDC2.0 4-2 表19 字段格式——固定 1 个 PB 块,
///                  字节9 bit4-7 为载荷PB大小(表19)定 PB 大小、字节11 bit4-7 为 MCS,
///                  字节10-11 bit0/字节12 bit0-3 为保留;载波走表7(帧子类型/TF/调制
///                  方式与参数,链路标识符在 MAC 帧头 byte15)
/// @return 空字符串 = 成功;非空 = 错误文案(调用方拒收该帧)
QString assemble(const QByteArray& body, MpduInfo& info, MsduState& msdu,
                 QByteArray& complete_msdu_body, quint8 band = 0xFF,
                 bool is_rf = false);

/// @brief 本站 MSDU span 是否避开全部 CRC 失败块(用户 2026-10-29 定,NW 同款
/// 多块补定在 GW 的落点:多站点 OFDMA/TDA 逐站判定;单站点已由 assemble 的
/// seq 链等价覆盖,此处不另设门)。
/// @details span 口径与 GW MSDU 解析器一致(gw_2026_msdu_parser.cpp):
/// 简头(体[0]低 nibble==0x01):span=4+len+4,len 为体[2..3]低 11bit;
/// 标准头:头 16B,MACAddrFlag(体[11] bit3)置位时 28B,len 为体[8..9]低 11bit,
/// span=头+len+4。站体流为各块体依序拼接,坏块 j 区间为
/// [j*(pb_size-4),(j+1)*(pb_size-4));span 自 0 起,相交当且仅当
/// span > j*(pb_size-4)。体 <4B 或 pb_size<=4 时无法判定,返回 false。
inline bool msdu_span_avoids_bad_blocks(const QByteArray& body,
                                        const QVector<bool>& crc_oks,
                                        int pb_size) {
    const int body_len = pb_size - 4;  // PB 头 1B + CRC24 3B
    if (body.size() < 4 || body_len <= 0) return false;
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());
    int span = 0;
    if ((p[0] & 0x0F) == 0x01) {
        const int len = (int)p[2] | (((int)p[3] & 0x07) << 8);
        span = 4 + len + 4;
    } else {
        if (body.size() < 12) return false;
        const int head = ((p[11] >> 3) & 0x01) ? 28 : 16;
        const int len = (int)p[8] | (((int)p[9] & 0x07) << 8);
        span = head + len + 4;
    }
    for (int j = 0; j < crc_oks.size(); ++j) {
        if (crc_oks[j]) continue;
        if (span > j * body_len) return false;
    }
    return true;
}

}  // namespace gw_2026_sof

#endif // SOFPARSER_H
