/// @file nw_2026_parser.cpp
/// @brief 南网 NW_2026 双模协议解析器实现(骨架:帧控制 + 各帧型 FCH 字段)
/// @details 字段坐标参考《通感一体化低压电力线宽带载波通信(SPLC)规约 第4部分:
///          数据链路层通信协议(试行)》表16/表20/表23-26/表41/表47 等,
///          移植自 D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG 的 MPDU_Class.py。
///          标准版本号(表16:0=保留/1=BPLC/2=SPLC)决定分发:版本=2 走本解析器
///          SPLC 表23(单站点数据帧)/表24-25(训练帧)/表26(OFDMA 多站点帧);
///          版本=1(BPLC)与 0(保留)整帧委托旧解析器 NW_2021_Parser 解析展示
///          (nw_2021 是 nw_2026 的子集)。
///          物理头 [dlen2][ts4][phr_mcs][option][channel][isRF] 与国网一致。
#include "nw_2026_parser.h"
#include "nw_2026_pb_table.h"
#include "nw_2026_msdu_parser.h"
#include "nw_2026_beacon_parser.h"
#include "nw_2026_tree.h"
#include "../nw_2021/nw_2021_parser.h"   // 标准版本号==1(BPLC)委托旧解析器:nw_2021 是 nw_2026 的子集
#include "common/fieldtools.h"
#include "crc.h"
#include "pb_assembler.h"
#include "bcd.h"
#include "i18n.h"
#include <QDateTime>

namespace {

int bcd2dec_here(quint8 b) {
    return ((b >> 4) & 0x0F) * 10 + (b & 0x0F);
}

}  // namespace

NW_2026_Parser::NW_2026_Parser() {}

// 剥物理层头(与国网一致:[dlen2][ts4][phr_mcs][option][channel][isRF])
bool NW_2026_Parser::decode_envelope(const BplcFrame& in, Result& r) {
    r.meta = in.meta;
    r.arrival_us = in.arrival_us;
    r.raw_wire   = in.raw_wire;
    const QByteArray& d = in.data;

    if (in.meta.from_raw) {
        if (d.size() < 2) { r.reject_reason = trl::L("空帧"); return false; }
        r.meta.is_rf = (quint8(d[0]) != 0);
        r.payload_for_log = d.mid(1);
        // frame_time 已由 SerialReader 从文本头时间填充(r.meta 已拷贝)
        r.meta.epoch_ms = r.meta.frame_time.toMSecsSinceEpoch();
        return true;
    }

    int hdr = in.meta.has_time_tag ? 8 : 0;
    if (d.size() < 20 + hdr) {
        r.reject_reason = trl::L("帧长过短");
        return false;
    }

    if (in.meta.has_time_tag) {
        try {
            int y  = bcd2dec_here((quint8)d[0]);
            int mo = bcd2dec_here((quint8)d[1]);
            int da = bcd2dec_here((quint8)d[2]);
            int hh = bcd2dec_here((quint8)d[3]);
            int mm = bcd2dec_here((quint8)d[4]);
            int ss = bcd2dec_here((quint8)d[5]);
            int ms = bcd2dec_here((quint8)d[6]) * 100 + bcd2dec_here((quint8)d[7]);
            r.meta.frame_time = QDateTime(QDate(2000 + y, mo, da),
                                          QTime(hh, mm, ss, ms));
            r.meta.has_time_tag = true;
            r.meta.epoch_ms = r.meta.frame_time.toMSecsSinceEpoch();
        } catch (...) {
            r.meta.frame_time = QDateTime::currentDateTime();
            r.meta.epoch_ms = r.meta.frame_time.toMSecsSinceEpoch();
        }
    } else {
        // 无时间标签:frame_time 已由 SerialReader 填充
        r.meta.epoch_ms = r.meta.frame_time.toMSecsSinceEpoch();
    }

    int offset = hdr;
    quint32 ts = 0;
    ts |= (quint32)(quint8)d[offset + 2];
    ts |= (quint32)(quint8)d[offset + 3] << 8;
    ts |= (quint32)(quint8)d[offset + 4] << 16;
    ts |= (quint32)(quint8)d[offset + 5] << 24;
    r.meta.timestamp = ts;

    QByteArray payload = d.mid(offset + 6);
    if (payload.size() < 4) {
        r.reject_reason = trl::L("剥头后载荷过短(<4B)");
        return false;
    }
    r.meta.phr_mcs  = (quint8)payload[0];
    r.meta.option   = (quint8)payload[1];
    r.meta.channel  = (quint8)payload[2];
    quint8 media_id = (quint8)payload[3];
    r.meta.is_rf = (media_id != 0);

    payload.remove(0, 4);
    r.payload_for_log = payload;
    if (payload.isEmpty()) {
        r.reject_reason = trl::L("剥头后空载荷");
        return false;
    }
    return true;
}

// 南网 MPDU_BASE 帧控制(16B):FrameType + ConInd + ShortNID(4b) + VersionNum + FCCS
bool NW_2026_Parser::parse_mpdu_base(const QByteArray& body, MpduInfo& info, QString& err) {
    if (body.size() < 16) { err = trl::L("MPDU_BASE 长度不足 16B"); return false; }
    const quint8* p = reinterpret_cast<const quint8*>(body.constData());

    quint32 calc_crc = crc24_lsb(p, 16);
    quint32 fch_crc  = (quint32)p[13] | ((quint32)p[14] << 8) | ((quint32)p[15] << 16);
    info.fch_crc_ok = (calc_crc == fch_crc);
    if (!info.fch_crc_ok) {
        err = QString("FCH CRC24 错(calc=%1 rx=%2)")
              .arg(calc_crc, 6, 16, QChar('0')).arg(fch_crc, 6, 16, QChar('0'));
        return false;
    }

    info.frame_type = (quint8)get_bits(p, 0, 0, 3);   // 定界符类型
    info.net_type   = (quint8)get_bits(p, 0, 3, 1);   // 接入指示 ConInd
    info.net_id     = (quint32)get_bits(p, 0, 4, 4);  // 短网络标识 SNID 4b
    info.version    = (quint8)get_bits(p, 12, 4, 4);  // 标准版本号
    return true;
}

namespace {
/// i18n 注册(本 TU 概要文本)
struct I18nReg {
    I18nReg() {
        trl::register_en("训练帧（MIMO训练帧）", "Training Frame (MIMO)");
        trl::register_en(" (截断: PB 块不完整, 声明 %1 块仅收到 %2 块)", " (truncated: %1 PB declared, %2 received)");
        trl::register_en("训练帧（Bitloading训练帧）", "Training Frame (Bitloading)");
        trl::register_en("聚合帧(%1 个级联 MAC 帧)", "Aggregated Frame (%1 cascade MAC frames)");
        trl::register_en("聚合帧(%1 帧: %2)", "Aggregated Frame (%1 frames: %2)");
        trl::register_en("聚合帧(%1 帧: %2 ×%3)", "Aggregated Frame (%1 frames: %2 x%3)");
        trl::register_en(", ", ", ");
        trl::register_en(" 等", " etc.");
        trl::register_en("未识别", "unrecognized");
    }
};
const I18nReg g_i18n_reg_nw2026_parser;

/// 级联子树 rel 坐标平移:单条 MAC 帧系 → 级联体流系(rel_start>=0 才有字节映射)
void shift_msdu_rel(MsduFieldNode& n, int base) {
    if (n.rel_start >= 0) n.rel_start += base;
    for (auto& ch : n.children) shift_msdu_rel(ch, base);
}
}

NW_2026_Parser::Result NW_2026_Parser::parse(const BplcFrame& in, MsduState& msdu, const Filter& f) {
    Result r;
    r.protocol = ProtocolVariant::NW_2026;   // 协议身份由解析器标记
    if (!decode_envelope(in, r)) {
        r.accept = false;
        return r;
    }

    if (!r.meta.is_rf && !f.link_hplc) { r.reject_reason = trl::L("HPLC 链路被过滤"); r.accept = false; return r; }
    if ( r.meta.is_rf && !f.link_hrf ) { r.reject_reason = trl::L("HRF 链路被过滤");  r.accept = false; return r; }

    QString err;
    if (!parse_mpdu_base(r.payload_for_log, r.mpdu, err)) {
        r.reject_reason = err;
        r.accept = false;
        return r;
    }

    // 南网帧型分派策略(用户决策 2026-10):SPLC(2026) 是 BPLC(2021) 保留位启用补充、
    // 兼容旧帧——信标 表119→表18 保留(12,1,1)→SNID高位、ACK 表7→表30 保留(4-11)→
    // SNID高位+SNR类型+SNR评估信息、COORD 表15→表41 保留→邻居网络比特图,均为保留位
    // 启用无冲突。因此仅 SOF 判断标准版本号(新旧 PB/TMI 布局差异大):
    //   1) SOF:version!=2 → 整帧委托旧解析器 NW_2021_Parser(BPLC);
    //   2) BEACON/ACK/COORD:不判版本号,默认走 SPLC 解析(BPLC 旧帧的保留位在 SPLC
    //      布局下解析为启用的 SNID高位/SNR类型 等字段,展示无害);
    //   3) 非 SOF 帧 SPLC 结构性失败(信标载荷区不完整等,见各分支回退点)→ 回退 m_legacy
    //      BPLC 解析。ACK/COORD 为固定字段解析,FCH 解析成功后字段必然在界内,无失败路径。
    const bool is_sof = (r.mpdu.frame_type == 1);
    if (is_sof && r.mpdu.version != 2) {
        // 聚合 BPLC 帧例外(用户需求 2026-10):载波 SOF version=1 但 PB 头聚合标志=1
        // (表47 byte2 bit0)→ nw_2021 无聚合拆分能力,留在本解析器本地解析——走下方
        // BPLC 表20 兜底分支 + 公共 pb_assembler 级联拆分(字段树含 AggregationFlag/
        // 级联头/Cascade MAC Frame);非聚合 version=1 仍整帧委托 NW_2021_Parser(现状)。
        // 无线 SOF 恒单块(表131)无聚合概念,保持委托不检测。纯本解析器内部分派,
        // 不涉 nw_2021→nw_2026 委托(用户禁止)。
        bool agg_bplc = false;
        if (!r.meta.is_rf && r.payload_for_log.size() >= 16 + 3)
            agg_bplc = (quint8(r.payload_for_log.at(16 + 2)) & 0x01) != 0;
        if (!agg_bplc) {
            // SPLC 扩展 MMe 例外(用户需求):version=1 非聚合帧承载 SPLC 表77 扩展 MMe
            // (0x0080-0x0085,BPLC 第2部分无定义)→ 本地解析(nw_2026 已有 MME_RUSNR_INFO_IND
            // 等 0x0080-0x0085 实现)。预读 MSDU 头判别,坐标同 nw_2021_msdu_parser:
            // PB 体自 payload[20](FCH16+PB头4);MACHeadFlag=体[0] bit0 → MAC 头长 32B/短 12B;
            // MSDU 帧类型 16b LE:长头在体[mac+16..17]/短头在体[mac+0..1](0x88E1=管理消息);
            // 其后 MMe = [版本 1B][类型 2B LE]。越界/判别失败保持原委托(不因预读崩溃)。
            // 纯本解析器内部分派,不涉 nw_2021→nw_2026 委托。
            bool splc_ext_mme = false;
            const QByteArray& pl = r.payload_for_log;
            if (!r.meta.is_rf && pl.size() >= 20 + 32 + 16 + 5) {
                const quint8* body = reinterpret_cast<const quint8*>(pl.constData()) + 20;
                // MACHeadFlag(体[0] bit0):0=长头 32B / 1=短头 12B
                // (nw_2021_msdu_parser.h LONG_HEAD=0x0/SHORT_HEAD=0x1,实帧 7 验证)
                const bool long_head = (body[0] & 0x01) == 0;
                const int mac_len = long_head ? 32 : 12;
                const int type_off = 20 + mac_len + (long_head ? 16 : 0);
                if (pl.size() >= type_off + 5
                    && quint8(pl.at(type_off)) == 0xE1
                    && quint8(pl.at(type_off + 1)) == 0x88) {
                    const quint16 mme = quint16(quint8(pl.at(type_off + 3)))
                                      | (quint16(quint8(pl.at(type_off + 4))) << 8);
                    splc_ext_mme = (mme >= 0x0080 && mme <= 0x0085);
                }
            }
            if (!splc_ext_mme)
                return m_legacy.parse(in, msdu, f);
        }
    }

    // SNID 共 5bit(试行规约 §5.1.2.2.1/表16):低位在 FCH(0,4,4),高位按帧型坐标合并:
    //   SOF 载波(表23)=(12,0,1) / SOF 无线(表45)=(12,0,1) /
    //   信标 载波(12,1,1) 无线(12,0,1) / ACK(表19/21)=(4,0,1) / COORD(表41)=(12,0,1)。
    //   注意 BPLC 版坐标不同:载波 SOF(表20)高位在 (5,0,1)、无线(表131)无高位——
    //   那是 NW_2021 的取值;NW_2026 走 SPLC 表23/表45,两种 SOF 都在 (12,0,1)。
    // 过滤与列显示统一用合并后的 5bit SNID(有效取值 1-31)。
    {
        const quint8* fch = reinterpret_cast<const quint8*>(r.payload_for_log.constData());
        quint8 snid_high = 0;
        switch (r.mpdu.frame_type) {
            case 0:  // 信标帧:载波(12,1,1) 无线(12,0,1)
                snid_high = (r.meta.is_rf ? ((fch[12] >> 0) & 0x01) : ((fch[12] >> 1) & 0x01));
                break;
            case 1: {  // SOF 载波:SPLC(表23)高位在 (12,0,1);BPLC(表20)高位在 (5,0,1);
                       // 无线两版本:表45 有 (12,0,1)/表131 无高位(本分支本地解析仅载波可达)
                snid_high = (r.mpdu.version == 2 || r.meta.is_rf)
                                ? (fch[12] & 0x01) : (fch[5] & 0x01);
                break;
            }
            case 2:  // ACK(4,0,1)
                snid_high = (fch[4] & 0x01);
                break;
            case 3:  // COORD(12,0,1)
                snid_high = (fch[12] & 0x01);
                break;
            default: break;
        }
        r.mpdu.net_id |= (quint32(snid_high) << 4);
    }

    if (f.nid_filter && !f.nid_list.contains(r.mpdu.net_id)) {
        r.reject_reason = QString("SNID 不在白名单: 0x%1").arg(r.mpdu.net_id, 1, 16);
        r.accept = false;
        return r;
    }

    const NW_2026_FrameType ftype = static_cast<NW_2026_FrameType>(r.mpdu.frame_type);
    switch (ftype) {
        case NW_2026_FrameType::BEACON: if (!f.allow_beacon) { r.reject_reason = trl::L("BEACON 被过滤"); r.accept = false; return r; } break;
        case NW_2026_FrameType::SOF:    if (!f.allow_sof)    { r.reject_reason = trl::L("SOF 被过滤");    r.accept = false; return r; } break;
        case NW_2026_FrameType::ACK:    if (!f.allow_ack)    { r.reject_reason = trl::L("ACK 被过滤");    r.accept = false; return r; } break;
        case NW_2026_FrameType::COORD:  if (!f.allow_coord)  { r.reject_reason = trl::L("COORD 被过滤");  r.accept = false; return r; } break;
        default: break;
    }

    const quint8* p = reinterpret_cast<const quint8*>(r.payload_for_log.constData());

    if (ftype == NW_2026_FrameType::SOF) {
        // SOF:源/目的 TEI(载波与无线同坐标)+ PB 大小(载波按标准版本号/无线 PBLen)
        // PB 块基址:普通帧 16(FCH 后);DL-OFDMA 帧 32(FCH 16 + eFC 16B,表27)
        int pb_base = 16;
        r.mpdu.src_tei = (quint16)get_bits(p, 1, 0, 12);
        r.mpdu.dst_tei = (quint16)get_bits(p, 2, 4, 12);
        if (r.meta.is_rf) {
            // 无线 SOF 可变域(第2部分表131):帧长(5,0,12,单位100µs)/载荷PB大小(6,4,4)/
            // MCS(7,0,4)/TEI过滤(7,4,1)/重传(7,5,1);其余为保留(字节8-11 整段、
            // (7,6,2)、(12,0,4)),无 PB 个数与符号数字段
            r.mpdu.link_id = (quint8) get_bits(p, 4, 0, 8);
            r.mpdu.frame_len = (quint16)get_bits(p, 5, 0, 12);
            quint8 pblen = (quint8)get_bits(p, 6, 4, 4);
            r.mpdu.mcs     = (quint8)get_bits(p, 7, 0, 4);   // MCS(表131)
            r.mpdu.pblen   = pblen;                       // 载荷 PB 大小索引(表130)
            r.mpdu.pb_size = (quint16)nw_2026_rf_pb_size(pblen);
            r.mpdu.pb_num  = 1;                           // 无线信道仅 1 个物理块
            r.mpdu.re_send_flag = (get_bits(p, 7, 5, 1) != 0);    // 重传标志位(表131)
            r.mpdu.symbol_num   = 0;                              // 无线无符号数字段
            r.mpdu.tmi_ext      = (quint8) get_bits(p, 12, 0, 4); // 整段原样存:(12,0,1)=SNID 高位(表45),(12,1,3)=保留;高位由下方合并块并入 net_id
        } else if (r.mpdu.version == 2) {
            // SPLC 版载波 SOF(表23-26):按 多站点帧标识(4,0,1)/训练帧标识(4,1,1) 分发
            const bool multi = (get_bits(p, 4, 0, 1) != 0);
            const bool train = (get_bits(p, 4, 1, 1) != 0);
            r.mpdu.frame_len = (quint16)get_bits(p, 8, 0, 12);   // 帧长(×10µs,表23-26)
            if (train) {
                // 训练帧(表24/25):无数据物理块,不重组 MSDU;
                // MSDU Type 列显示帧性质(用户需求):训练帧类型(5,0,1) 0=MIMO 1=Bitloading
                r.mpdu.pb_num = 0;
                r.mpdu.pb_size = 0;
                const quint8 train_type = (quint8)get_bits(p, 5, 0, 1);
                r.msdu.present = true;
                r.msdu.summary = (train_type == 1)
                    ? trl::L("训练帧（Bitloading训练帧）")
                    : trl::L("训练帧（MIMO训练帧）");
            } else if (multi) {
                // OFDMA 多站点帧(表26):基字段 + eFC(DL-OFDMA 表27,16B 独立结构) + 站点0 PB
                const quint8 ofdma_type  = (quint8)get_bits(p, 4, 1, 2);
                if (ofdma_type == 0 && r.payload_for_log.size() >= 32) {
                    // DL-OFDMA(表27):eFC 自 p[16] 起 16 字节,站点0 字段位于 eFC[0..3];
                    // PB 块自 p[32] 起(站点0),多站点其余站点 PB 依序随后
                    pb_base = 32;
                    const quint8* e = p + 16;
                    const quint8 st0_pbn_field = (quint8)get_bits(e, 0, 2, 1);
                    const quint8 st0_tmi       = (quint8)get_bits(e, 1, 7, 5);
                    r.mpdu.pb_num  = st0_pbn_field + 1;   // 表27/28:字段 0→1 块,1→2 块
                    r.mpdu.pb_size = (quint16)nw_2026_splc_pb_size(st0_tmi);
                } else {
                    // SACK(类型1)/trigger(类型2)/UL-SACK(类型3):本站不重组 MSDU
                    r.mpdu.pb_num = 0;
                }
            } else {
                // SPLC 单站点数据帧(表23):物理块个数(4,2-5)/Bitloading(6,0)/TMI编号(6,1-5)
                r.mpdu.pb_num  = (quint8) get_bits(p, 4, 2, 4);
                const bool bitload = (get_bits(p, 6, 0, 1) != 0);
                const quint8 tmi_idx = (quint8)get_bits(p, 6, 1, 5);
                r.mpdu.pb_size = bitload
                    ? (quint16)nw_2026_splc_bitload_pb_size(tmi_idx)
                    : (quint16)nw_2026_splc_pb_size(tmi_idx);
            }
        } else {
            // 防御性兜底(策略下不可达:SOF 且 version!=2 已在 parse() 顶部委托 NW_2021_Parser,
            // 走到此处的 SOF 必为 version==2):BPLC 版载波 SOF(表20):链路标识符(4,0,8)/SNID高位(5,0,1 由树渲染)/
            // PBNum(7,0,4)/TMI(7,4,4)/FrameLen(8,0,12)/TEI过滤/重传/符号数/TMI_EXT
            r.mpdu.link_id = (quint8) get_bits(p, 4, 0, 8);
            r.mpdu.pb_num    = (quint8) get_bits(p, 7, 0, 4);
            r.mpdu.tmi       = (quint8) get_bits(p, 7, 4, 4);
            r.mpdu.frame_len = (quint16)get_bits(p, 8, 0, 12);
            r.mpdu.re_send_flag = (get_bits(p, 10, 6, 1) != 0);   // 重传标志位(表18)
            r.mpdu.symbol_num   = (quint16)get_bits(p, 10, 7, 9);  // 符号数(表16)
            r.mpdu.tmi_ext   = (quint8) get_bits(p, 12, 0, 4);
            r.mpdu.pb_size   = (quint16)nw_2026_pb_size(r.mpdu.tmi, r.mpdu.tmi_ext);
        }
        if (f.tei_filter && !f.tei_list.contains(r.mpdu.src_tei)
                        && !f.tei_list.contains(r.mpdu.dst_tei)) {
            r.reject_reason = trl::L("TEI 不在白名单");
            r.accept = false;
            return r;
        }
        // PB 块逐块 CRC24 + 块体提取 + 聚合级联拆分(公共 pb_assembler;南网布局:
        // PB头4B 表47 + 块体 + 保留1B + CRC24 3B,聚合标志 byte2 bit0,§5.1.3.2.1)
        if (r.mpdu.pb_size > 8 && r.mpdu.pb_num > 0) {
            const pb_asm::PbLayout layout{/*head*/4, /*rsv*/1, /*crc*/3,
                                          /*agg_byte*/2, /*agg_bit*/0,
                                          /*base*/pb_base};
            const pb_asm::AssembleResult res =
                pb_asm::assemble_pb_blocks(r.payload_for_log, r.mpdu.pb_num,
                                           r.mpdu.pb_size, layout);
            for (const auto& b : res.blocks) r.mpdu.pb_crc_oks.append(b.crc_ok);
            r.mpdu.pb_crc_ok = res.all_crc_ok;
            if (res.any_aggregated) {
                // 聚合帧(SPLC 图11 多块聚合,用户决策:不参与 MSDU 拼接):级联 MAC 帧
                // 逐帧独立解析展示——级联记录可跨 PB 体边界(实测锚点:4 条记录
                // [2d 10]/[2d 00]/[2d 00]/[2d 20],首记录 Start=1、末记录 End=1)
                // 子树坐标平移:msdu parser 产出 rel 相对单条 MAC 帧,统一平移到
                // 级联体流(msdu_body)坐标系,树层 MsduRawMap 按块换算 raw 多段高亮
                int cas_cnt = 0;
                QStringList cas_sums;   // 各级联帧 MSDU 类型摘要(MSDU Type 列展示聚合内容)
                for (const auto& c : res.cascade) {
                    const MsduInfo cm = NW_2026_MsduParser::parse(c.mac);
                    MsduFieldNode grp;
                    grp.name = QStringLiteral("Cascade MAC Frame[%1]").arg(cas_cnt);
                    grp.value = QStringLiteral("%1 B%2%3")
                                    .arg(c.mac_len)
                                    .arg(c.start_flag ? QStringLiteral(" (Start)") : QString())
                                    .arg(c.end_flag ? QStringLiteral(" (End)") : QString());
                    // 高亮映射:rel 相对级联体流(msdu_body=各块体依序拼接)
                    grp.rel_start = c.body_start + 2;   // 跳过级联头 2B
                    grp.rel_len   = c.mac_len;
                    const int base = grp.rel_start;
                    for (MsduFieldNode n : cm.tree) {
                        shift_msdu_rel(n, base);
                        grp.children.append(n);
                    }
                    r.msdu.tree.append(grp);
                    r.msdu.present = true;
                    ++cas_cnt;
                    cas_sums << (cm.summary.isEmpty() ? trl::L("未识别") : cm.summary);
                }
                // 摘要(用户需求):逐帧类型列表,让用户知道聚合了什么——
                // 全同型压缩 "T ×N";混合型列前 4 项(>4 截断加 " 等");空流保持计数式
                if (cas_sums.isEmpty()) {
                    r.msdu.summary = trl::L("聚合帧(%1 个级联 MAC 帧)").arg(cas_cnt);
                } else {
                    bool all_same = true;
                    for (const auto& s : cas_sums)
                        if (s != cas_sums.first()) { all_same = false; break; }
                    if (all_same) {
                        r.msdu.summary = trl::L("聚合帧(%1 帧: %2 ×%3)")
                                             .arg(cas_cnt).arg(cas_sums.first()).arg(cas_cnt);
                    } else {
                        const QStringList head =
                            cas_sums.mid(0, qMin(cas_sums.size(), 4));
                        QString list = head.join(trl::L(", "));
                        if (cas_sums.size() > 4) list += trl::L(" 等");
                        r.msdu.summary = trl::L("聚合帧(%1 帧: %2)")
                                             .arg(cas_cnt).arg(list);
                    }
                }
                // msdu_body 保留为级联体流供树值/高亮映射,非重组 MSDU 语义(见 msdu.cascade);
                // total_len = 级联体流总长(树层 Length 子节点/区间高亮依据)
                r.msdu.cascade = true;
                r.msdu_body = res.msdu_body;   // 级联体流(含块体尾填充,高亮映射用)
                // total_len = 级联记录流有效长度(逐条 2+len 累加,不含尾填充;
                // 显示/高亮区间按协议语义取有效长度,msdu_body 保留全流)
                r.msdu.total_len = res.cascade_used;
                // 高亮:单块级联 = 首块块体起点(整段连续);多块级联跨块不连续 → -1(逐块换算)
                r.msdu_raw_base = (r.mpdu.pb_num == 1 && !res.blocks.isEmpty())
                                      ? res.blocks.first().body_raw_offset : -1;
            } else if (!res.msdu_body.isEmpty()) {
                r.msdu_body = res.msdu_body;
                // 表6:单跳帧协议(版本=2)仅无线信道支持;载波信道收到属异常(排查信道/串口错配)
                if (!r.meta.is_rf && (((quint8)res.msdu_body[0] >> 1) & 0x03) == 2)
                    qWarning("NW_2026: single-hop MAC version=2 on PLC channel (isRF=0)");
                r.msdu = NW_2026_MsduParser::parse(res.msdu_body);
                // 高亮:单块连续 = 首块块体起点(块体在 raw_bytes 中的绝对偏移);
                // 多块重组时各块体被 PB 头/保留/CRC 隔断,不连续 → -1(沿用国网约定)
                r.msdu_raw_base = (r.mpdu.pb_num == 1 && !res.blocks.isEmpty())
                                      ? res.blocks.first().body_raw_offset : -1;
            }
            // 截断提示(用户需求):声明块数 vs 实收块数,聚合/非聚合都标;FCH 声明 pb_num,
            // 实收 = 完整容纳块数(pb_assembler res.blocks)
            if (res.truncated)
                append_summary(r.msdu, trl::L(" (截断: PB 块不完整, 声明 %1 块仅收到 %2 块)")
                    .arg(r.mpdu.pb_num).arg(res.blocks.size()));
            r.msdu.cascade_break_len = res.cascade_break_declared_len;
        }
    } else if (ftype == NW_2026_FrameType::BEACON) {
        // BEACON:信标时间戳/周期计数/源 TEI(载波与无线同坐标)
        r.mpdu.beacon_timestamp  = (quint32)get_bits(p, 1, 0, 32);
        r.mpdu.beacon_period_cnt = (quint32)get_bits(p, 5, 0, 32);
        r.mpdu.src_tei           = (quint16)get_bits(p, 9, 0, 12);
        if (r.meta.is_rf) {
            r.mpdu.mcs     = (quint8)get_bits(p, 10, 4, 4);   // MCS(表128)
            r.mpdu.pblen   = (quint8)get_bits(p, 11, 0, 4);   // 载荷 PB 大小索引(表130)
            r.mpdu.pb_size = (quint16)nw_2026_rf_pb_size(r.mpdu.pblen);
        } else {
            r.mpdu.tmi       = (quint8)get_bits(p, 10, 4, 4); // 载波映射表索引
            r.mpdu.symbol_num = (quint16)get_bits(p, 11, 0, 9);
            r.mpdu.beacon_line = (quint8)get_bits(p, 12, 2, 2);
            r.mpdu.pb_size   = (quint16)nw_2026_pb_size(r.mpdu.tmi, 0);
        }
        // 信标帧载荷区(固定头 + 管理信息 + BPCS CRC32 + 保留字节 + PB CRC24)
        if (r.mpdu.pb_size > 0)
            r.beacon = NW_2026_BeaconParser::parse_beacon(r.payload_for_log, r.mpdu.pb_size, r.meta.is_rf);
        // 非 SOF 帧 SPLC 结构性失败(载荷不足/信标载荷区不完整,parse_beacon 置 present=false)
        // → 回退 BPLC 解析(分派策略第 3 条,用户决策)
        if (!r.beacon.present) {
            return m_legacy.parse(in, msdu, f);
        }
    } else if (ftype == NW_2026_FrameType::ACK) {
        // ACK(南网 MPDU_ACK_FCH 坐标,表19/表21)
        r.mpdu.ack_ext_type = (quint8)get_bits(p, 12, 0, 4);
        switch (static_cast<NW_2026_AckExtType>(r.mpdu.ack_ext_type)) {
            case NW_2026_AckExtType::Search:  // 网络搜索帧(抄控器,表21)
                r.mpdu.ack_dst_addr   = get_bits(p, 1, 0, 48);
                r.mpdu.ack_search_tei = (quint16)get_bits(p, 7, 0, 12);
                break;
            case NW_2026_AckExtType::Sync:  // 同步帧(抄控器,表21)
                r.mpdu.ack_sync_timestamp = (quint32)get_bits(p, 1, 0, 32);
                r.mpdu.ack_sync_tei       = (quint16)get_bits(p, 5, 0, 12);
                break;
            case NW_2026_AckExtType::Bitloading:
                // Bitloading 扩展帧(表34):源/目的 TEI 落标量;Bitloading 帧类型与
                // 训练子字段(表35-40)由字段树按原始字节渲染(nw_2026_tree.cpp)
                r.mpdu.src_tei = (quint16)get_bits(p, 1, 0, 12);
                r.mpdu.dst_tei = (quint16)get_bits(p, 2, 4, 12);
                break;
            case NW_2026_AckExtType::Normal:
            default:
                // 选择确认帧(表19);未知扩展帧类型(表21"其他")默认按选择确认帧解析
                r.mpdu.ack_rx_res    = (quint8) get_bits(p, 1, 0, 4);
                r.mpdu.ack_rx_status = (quint8) get_bits(p, 1, 4, 4);
                r.mpdu.dst_tei       = (quint16)get_bits(p, 2, 0, 12);
                r.mpdu.ack_rx_pb_num = (quint8) get_bits(p, 3, 4, 4);
                break;
        }
    } else if (ftype == NW_2026_FrameType::COORD) {
        // COORD(南网 MPDU_COORD_FCH 坐标,表41):
        //   邻居网络比特图1(1,0,16)/无线信道(3,0,8)/比特图2(4,0,10)/持续时间(5,2,14)/
        //   比特图3(7,0,1)/带宽结束标志(7,1,1)/无线option(7,2,2)/比特图4(7,4,4)/
        //   带宽结束偏移(8,0,16)/带宽开始偏移(10,0,16)/SNID高位(12,0,1)+保留(12,1,3)
        // 注:31bit 邻居比特图 = 比特图1(16b)+比特图2(10b)+比特图3(1b)+比特图4(4b);
        //     MpduInfo 标量无 31bit 字段,树渲染直接读原始字节(见 nw_2026_tree)。
        r.mpdu.coord_neighbour_nid     = (quint32)get_bits(p, 1, 0, 16);  // 邻居网络比特图1
        r.mpdu.coord_duration          = (quint16)get_bits(p, 5, 2, 14);  // 持续时间(×40ms)
        r.mpdu.coord_band_end_flag     = (quint8) get_bits(p, 7, 1, 1);
        r.mpdu.coord_band_end_offset   = (quint16)get_bits(p, 8, 0, 16);  // ×4ms
        r.mpdu.coord_band_start_offset = (quint16)get_bits(p, 10, 0, 16); // ×4ms
        r.mpdu.coord_rsv1             = (quint16)get_bits(p, 4, 0, 10);   // 邻居网络比特图2(表41)
        r.mpdu.coord_rsv2             = (quint8) get_bits(p, 7, 0, 1);    // 邻居网络比特图3(表41)
        r.mpdu.coord_rsv3             = (quint8) get_bits(p, 7, 4, 4);    // 邻居网络比特图4(表41)
        r.mpdu.coord_rsv0             = (quint8) get_bits(p, 12, 0, 4);   // SNID高位(12,0,1)+保留(12,1,3)
    }

    r.accept = true;
    return r;
}
