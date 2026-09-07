/// @file sofparser.h
/// @brief SOF 帧解析模块声明(51242 SOF:FCH 字段 + PB 块/MSDU 重组)
/// @details 负责 SOF 帧 FCH 内业务字段(源/目的 TEI、LinkID、PBNum、TMI…)
///          与多 PB 块的 MSDU 跨帧重组(与 Python MPDU_Class 重组逻辑一致)。
///          重组状态 MsduState 定义于 bplcframe.h(由调用方持有,跨帧保活)。
#ifndef SOFPARSER_H
#define SOFPARSER_H

#include "bplcframe.h"

namespace sof {

/// @brief SOF FCH 字段解析 + 多 PB 重组
/// @param body      payload_for_log(自 FrameType 起,含 16B FCH)
/// @param info      [out] 填充 src/dst TEI、pb_size、pb_heads/pb_crc_oks 等
/// @param msdu      跨帧重组状态(多帧分段时由调用方在帧间保留)
/// @param complete_msdu_body [out] 重组完成的 MSDU body(未完成则为空)
/// @return 空字符串 = 成功;非空 = 错误文案(调用方拒收该帧)
QString assemble(const QByteArray& body, MpduInfo& info, MsduState& msdu,
                 QByteArray& complete_msdu_body);

}  // namespace sof

#endif // SOFPARSER_H
