/// @file protocoltree.cpp
/// @brief ProtocolTree 实现
#include "protocoltree.h"
#include "i18n.h"
#include <QHeaderView>

namespace {
/// @brief MSDU buffer 偏移 → raw 帧偏移的映射参数
/// 单块:base=msdu_body 在 raw 的起点(17);多块:由 pb_num/pb_size 逐块推算。
struct MsduRawMap {
    int     base;      ///< 单块时 buffer[0] 的 raw 偏移;跨块=-1
    int     pb_num;    ///< 物理块数(跨块映射用)
    int     pb_size;   ///< 物理块大小(跨块映射用)
    int     fch_size;  ///< 控制头长度(16)
};
/// @brief 把相对重组 buffer 的 [rel_start,rel_start+rel_len) 映射到 raw 偏移;
///        跨块且字段越块边界(或越界)返回 -1(无法高亮)。
int msdu_raw_of(const MsduRawMap& m, int rel_start, int rel_len) {
    if (rel_start < 0 || rel_len <= 0) return -1;
    if (m.base >= 0) return m.base + rel_start;
    if (m.pb_num <= 0 || m.pb_size <= 4) return -1;
    const int body = m.pb_size - 4;          // 每块数据字节
    const int end  = rel_start + rel_len;
    const int b0   = rel_start / body;
    const int b1   = (end - 1) / body;
    if (b0 != b1 || b0 >= m.pb_num) return -1;  // 跨块或越界 → 不可映射
    return m.fch_size + b0 * m.pb_size + 1 + (rel_start - b0 * body);
}

/// @brief 递归把 MsduInfo 字段树渲染进协议树(带 [Nbit] 标注)
/// @param raw_map  buffer→raw 偏移映射(供逐字段高亮换算)
void render_msdu_tree(QTreeWidgetItem* parent, const QVector<MsduFieldNode>& nodes,
                      const MsduRawMap& raw_map) {
    for (const auto& n : nodes) {
        QTreeWidgetItem* it = new QTreeWidgetItem(parent);
        it->setText(0, n.name);
        it->setText(1, n.value);
        // 字段带相对 msdu_body 的字节区间,且本帧含该 MSDU 时 → 映射 raw 高亮
        int rstart = msdu_raw_of(raw_map, n.rel_start, n.rel_len);
        if (rstart >= 0) {
            it->setData(0, Qt::UserRole, rstart);
            it->setData(1, Qt::UserRole, n.rel_len);
        }
        if (!n.children.isEmpty()) {
            render_msdu_tree(it, n.children, raw_map);
            it->setExpanded(true);
        }
    }
}
}  // namespace

ProtocolTree::ProtocolTree(QWidget* parent) : QTreeWidget(parent) {
    setColumnCount(2);
    setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    setUniformRowHeights(true);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1, QHeaderView::Stretch);
    setAlternatingRowColors(true);
    // 鼠标点击必触发(即便重复点击已选中行 itemSelectionChanged 不触发,
    // 改用 itemClicked 保证每点都刷新高亮);键盘方向键仍走 selectionChanged。
    auto apply_selection = [this](QTreeWidgetItem* it) {
        if (!it) return;
        int start = it->data(0, Qt::UserRole).toInt();
        int len   = it->data(1, Qt::UserRole).toInt();
        m_selected_range = {start, len};
        emit range_selected(start, len);   // 无字节映射的行发 (-1,0) → 清除高亮
    };
    connect(this, &QTreeWidget::itemClicked, this,
            [apply_selection](QTreeWidgetItem* it, int) { apply_selection(it); });
    connect(this, &QTreeWidget::itemSelectionChanged, this, [this, apply_selection]() {
        // 鼠标点击时当前项即选中项;键盘方向键移动也同步。
        QTreeWidgetItem* it = currentItem();
        if (!it) {
            const auto sel = selectedItems();
            it = sel.isEmpty() ? nullptr : sel.first();
        }
        apply_selection(it);
    });
}

QTreeWidgetItem* ProtocolTree::add_item(QTreeWidgetItem* parent,
                                       const QString& field,
                                       const QString& value,
                                       int byte_start, int byte_len) {
    QTreeWidgetItem* it = new QTreeWidgetItem(parent);
    it->setText(0, field);
    it->setText(1, value);
    it->setData(0, Qt::UserRole, byte_start);
    it->setData(1, Qt::UserRole, byte_len);
    // parent==nullptr 表示顶层节点:QTreeWidgetItem 不会自动挂载,
    // 必须显式 addTopLevelItem 才能显示(否则整棵树空白)。
    if (parent == nullptr) addTopLevelItem(it);
    return it;
}

/// @brief 按位域坐标添加字段行:显示时把 bit 宽标注到字段名,
///        并换算为覆盖的字节区间用于 HexView 高亮。
/// @param field    字段名
/// @param value    文本值
/// @param parent   父节点
/// @param byte_idx 起始字节
/// @param bit_off  起始字节内位偏移
/// @param bit_len  字段位宽
QTreeWidgetItem* ProtocolTree::add_bit_field(QTreeWidgetItem* parent,
                                             const QString& field,
                                             const QString& value,
                                             int byte_idx, int bit_off, int bit_len) {
    QString name = field;
    if (bit_len > 0) name += QStringLiteral(" [%1b]").arg(bit_len);

    // 换算字节区间:起始位 -> (byte_idx*8+bit_off),结束位 -> +bit_len
    int first_bit = byte_idx * 8 + bit_off;
    int last_bit  = first_bit + bit_len - 1;
    int bs = first_bit / 8;
    int be = last_bit / 8;
    return add_item(parent, name, value, bs, be - bs + 1);
}

void ProtocolTree::show_packet(const PacketEntry& e) {
    clear();
    m_selected_range = {-1, 0};

    if (!e.accepted) {
        auto* root = add_item(nullptr, "Dropped Frame", e.reason);
        add_item(root, "Reason", e.reason);
        return;
    }

    auto* root = add_item(nullptr, "Frame",
        QStringLiteral("%1 frame (%2 B)")
            .arg(e.meta.is_rf ? "HRF" : "HPLC")
            .arg(e.raw_bytes.size()));

    auto* phys = add_item(root, "Physical", "");
    add_item(phys, "Media",       e.meta.is_rf ? "HRF (Wireless)" : "HPLC (PLC)");
    add_item(phys, "Timestamp",   QString::number(e.meta.timestamp) + " (NTB tick, 40us)");
    add_item(phys, "Channel/Band", QString::number(e.meta.channel));
    if (e.meta.is_rf) {
        add_item(phys, "PHR MCS", QString::number(e.meta.phr_mcs));
        add_item(phys, "Option",  QString::number(e.meta.option));
    }
    add_item(phys, "FrameTime", e.meta.frame_time.toString("yyyy-MM-dd HH:mm:ss.zzz"));

    auto* mpdu_base = add_item(root, "MPDU Base", "", 0, 16);
    // 字节范围相对 payload(raw_bytes,从 MPDU_BASE 首字节起算)
    add_bit_field(mpdu_base, "Frame Type", QString("%1 (%2)")
        .arg(e.mpdu.frame_type_name()).arg(e.mpdu.frame_type), 0, 0, 3);
    add_bit_field(mpdu_base, "Net Type", QString::number(e.mpdu.net_type), 0, 3, 5);
    add_bit_field(mpdu_base, "Net ID", QString("0x%1")
        .arg(e.mpdu.net_id, 6, 16, QChar('0')), 1, 0, 24);
    add_bit_field(mpdu_base, "Version", QString::number(e.mpdu.version), 12, 4, 4);
    add_bit_field(mpdu_base, "FCH CRC24", e.mpdu.fch_crc_ok ? "OK" : "FAIL", 13, 0, 24);

    if (e.mpdu.frame_type == 0) {
        // BEACON:TimeStamp(4,0,32) SourceTEI(8,0,12) TMI(9,4,4)
        //        SymbolNum(10,0,9) LineNum(11,1,2) RSV(11,3,9)
        auto* bcn = add_item(root, "BEACON", "");
        add_bit_field(bcn, "TimeStamp", QString("0x%1")
            .arg(e.mpdu.beacon_timestamp, 8, 16, QChar('0')), 4, 0, 32);
        add_bit_field(bcn, "Source TEI", QString::number(e.mpdu.src_tei), 8, 0, 12);
        add_bit_field(bcn, "TMI", QString::number(e.mpdu.tmi), 9, 4, 4);
        add_bit_field(bcn, "Symbol Num", QString::number(e.mpdu.symbol_num), 10, 0, 9);
        add_bit_field(bcn, "Line", QString::number(e.mpdu.beacon_line), 11, 1, 2);
        // 帧级物理块信息:信标帧为单块(FCH 后整块,无 PB 头),块长按 TMI 查表;
        // PB CRC24 / PB Padding 位于 Beacon Load 字段末尾(见下)
        if (e.mpdu.pb_size > 0)
            add_item(bcn, "PB Size", QString::number(e.mpdu.pb_size));
        // 载荷区(Beacon Load)位于 FCH 16B 之后
        if (e.beacon.present) {
            auto* load = add_item(bcn, "Beacon Load", "");
            // BEACON 载荷:beacon 树 rel 相对载荷区起点,载荷区在 raw 偏移 16 处(单块连续)
            MsduRawMap rm;
            rm.base = 16;
            rm.pb_num = 1;
            rm.pb_size = 0;
            rm.fch_size = 16;
            render_msdu_tree(load, e.beacon.tree, rm);
        }
    } else if (e.mpdu.frame_type == 1) {
        // SOF:SourceTEI(4,0,12) DestTEI(5,4,12) LinkID(7,0,8) FrameLen(8,0,12)
        //     PBNum(9,4,4) SymbolNum(10,0,9) BC/ReSend/Enc/TMI(11,*) TMI_EXT(12,0,4)
        auto* sof = add_item(root, "SOF", "", 4, 12);
        add_bit_field(sof, "Source TEI",      QString::number(e.mpdu.src_tei), 4, 0, 12);
        add_bit_field(sof, "Destination TEI", QString::number(e.mpdu.dst_tei), 5, 4, 12);
        // Link ID(链路标识符,表 20):0-3 报文优先级 / 4-254 业务分类 LID / 255 无效;
        // 注:链路标识符越大,优先级越高
        {
            QString link_desc;
            if (e.mpdu.link_id <= 3)
                link_desc = trl::L("报文优先级(越小越低)");
            else if (e.mpdu.link_id <= 254)
                link_desc = trl::L("业务分类LID");
            else
                link_desc = trl::L("无效值");
            add_bit_field(sof, "Link ID",
                QStringLiteral("0x%1 - %2")
                    .arg(e.mpdu.link_id, 2, 16, QChar('0')).arg(link_desc),
                7, 0, 8);
        }
        add_bit_field(sof, "Frame Length",    QString("%1 (x10us)").arg(e.mpdu.frame_len), 8, 0, 12);
        add_bit_field(sof, "PB Num",          QString::number(e.mpdu.pb_num), 9, 4, 4);
        add_bit_field(sof, "Symbol Num",      QString::number(e.mpdu.symbol_num), 10, 0, 9);
        add_bit_field(sof, "Broadcast",       e.mpdu.bc_flag ? "Yes" : "No", 11, 1, 1);
        add_bit_field(sof, "ReSend",          e.mpdu.re_send_flag ? "Yes" : "No", 11, 2, 1);
        add_bit_field(sof, "Encrypted",       e.mpdu.encryp_flag ? "Yes" : "No", 11, 3, 1);
        add_bit_field(sof, "TMI",             QString::number(e.mpdu.tmi), 11, 4, 4);
        add_bit_field(sof, "TMI_EXT",         QString::number(e.mpdu.tmi_ext), 12, 0, 4);
        add_item(sof, "PB Size",         QString::number(e.mpdu.pb_size));

        // ---- 逐 PB 块渲染:每块 Header / Body(+Padding) / CRC24 ----
        // 块 i 在帧内布局:头 = raw[16 + i*pb_size],数据 = 头+1 起 pb_size-4 B,
        // CRC24 = 块尾 3B;数据区在重组 buffer 中的相对偏移 = i*(pb_size-4)。
        {
            const int body_len = e.mpdu.pb_size - 4;
            const int pb_num   = (e.mpdu.pb_num > 0) ? e.mpdu.pb_num : 1;
            const quint8* d =
                reinterpret_cast<const quint8*>(e.raw_bytes.constData());
            const int raw_sz = e.raw_bytes.size();
            for (int bi = 0; bi < pb_num; ++bi) {
                const int blk_off = 16 + bi * e.mpdu.pb_size;   // 块起始(raw)
                const bool multi = pb_num > 1;
                // PB Header
                quint8 h = (bi < e.mpdu.pb_heads.size())
                               ? e.mpdu.pb_heads[bi]
                               : (blk_off < raw_sz ? d[blk_off] : 0);
                QStringList flags;
                if (h & 0x80) flags << QStringLiteral("END");
                if (h & 0x40) flags << QStringLiteral("START");
                QString desc = flags.isEmpty()
                                   ? QStringLiteral("(continuation)")
                                   : flags.join(QLatin1Char('|'));
                add_bit_field(sof,
                    multi ? QStringLiteral("PB Header (%1/%2)").arg(bi).arg(pb_num)
                          : QStringLiteral("PB Header"),
                    QStringLiteral("0x%1 (%2, seq=%3)")
                        .arg(h, 2, 16, QChar('0')).arg(desc).arg(h & 0x3F),
                    blk_off, 0, 8);
                // PB Body(数据区;高亮该块数据)
                if (body_len > 0 && blk_off + 1 < raw_sz) {
                    auto* pb = add_item(sof,
                        multi ? QStringLiteral("PB Body (%1/%2)").arg(bi).arg(pb_num)
                              : QStringLiteral("PB Body"),
                        QStringLiteral("%1 B").arg(body_len),
                        blk_off + 1, body_len);
                    // 填充区:本块数据区相对重组 buffer 偏移 = bi*body_len;
                    // 该块内 MSDU 帧结束后即为 0x00 填充(需 MSDU 完整定位)
                    if (e.msdu.present && e.msdu.total_len > 0) {
                        int buf_off = bi * body_len;   // 本块数据在 buffer 起点
                        int msdu_end_in_blk = e.msdu.total_len - buf_off;
                        if (msdu_end_in_blk > 0 && msdu_end_in_blk < body_len) {
                            add_item(pb, "PB Padding",
                                QStringLiteral("%1 B (0x00 fill)")
                                    .arg(body_len - msdu_end_in_blk),
                                blk_off + 1 + msdu_end_in_blk,
                                body_len - msdu_end_in_blk);
                        }
                    }
                    (void)pb;
                }
                // PB CRC24(块尾 3B)
                bool have = (blk_off + e.mpdu.pb_size) <= raw_sz;
                quint32 crc = 0;
                if (have) {
                    int o = blk_off + e.mpdu.pb_size - 3;
                    crc = (quint32)d[o] | ((quint32)d[o + 1] << 8)
                        | ((quint32)d[o + 2] << 16);
                }
                bool okb = (bi < e.mpdu.pb_crc_oks.size())
                               ? e.mpdu.pb_crc_oks[bi]
                               : e.mpdu.pb_crc_ok;
                add_item(sof,
                    multi ? QStringLiteral("PB CRC24 (block %1/%2)").arg(bi).arg(pb_num)
                          : QStringLiteral("PB CRC24"),
                    have ? QStringLiteral("0x%1 %2")
                               .arg(crc, 6, 16, QChar('0'))
                               .arg(okb ? "OK" : "FAIL")
                         : (okb ? QStringLiteral("OK") : QStringLiteral("FAIL")),
                    blk_off + e.mpdu.pb_size - 3, 3);
            }
        }

        if (e.msdu.present) {
            auto* msdu = add_item(root, "MSDU (Reassembled)",
                QStringLiteral("%1 B  %2").arg(e.msdu_body.size())
                                          .arg(e.msdu.summary));
            // 点击 MSDU 分组:高亮整个 MSDU 帧(头+数据+CRC,不含 PB 填充);
            // 跨块时按块布局映射(MSDU 帧连续落在块数据区内,可定位)
            {
                MsduRawMap rm;
                rm.base = e.msdu_raw_base;
                rm.pb_num = (e.msdu_raw_base >= 0)
                                ? 1 : (e.mpdu.pb_num > 0 ? e.mpdu.pb_num : 1);
                rm.pb_size = e.mpdu.pb_size;
                rm.fch_size = 16;
                int msdu_start = msdu_raw_of(rm, 0, e.msdu.total_len);
                if (msdu_start >= 0 && e.msdu.total_len > 0) {
                    msdu->setData(0, Qt::UserRole, msdu_start);
                    msdu->setData(1, Qt::UserRole, e.msdu.total_len);
                }
                add_item(msdu, "Length", QString::number(e.msdu_body.size()));
                render_msdu_tree(msdu, e.msdu.tree, rm);
            }
        } else if (!e.msdu_body.isEmpty()) {
            auto* msdu = add_item(root, "MSDU (Reassembled)",
                QStringLiteral("%1 B").arg(e.msdu_body.size()));
            add_item(msdu, "Length", QString::number(e.msdu_body.size()));
            add_item(msdu, "Hex", QString(e.msdu_body.toHex(' ')));
        }
    } else if (e.mpdu.frame_type == 2) {
        // ACK:ExtFrameType(12,0,4);type0 常规:SrcTEI(5,0,12) DstTEI(6,4,12)
        //     type1 Search / type2 Sync / type3 切频
        auto* ack = add_item(root, "ACK", "");
        switch (e.mpdu.ack_ext_type) {
            case 0:
                // 常规 ACK(物理字节序):RxRes RxStatus SrcTEI DstTEI RxPBNum
                //              RSV0 ChQ STALoad RSV1 ExtFrameType
                add_bit_field(ack, "RxRes", e.mpdu.ack_rx_res == 1
                              ? QStringLiteral("1 - FAIL")
                              : QStringLiteral("0 - PASS"), 4, 0, 4);
                add_bit_field(ack, "RxStatus", e.mpdu.ack_rx_status == 1
                              ? QStringLiteral("1 - success")
                              : QStringLiteral("0 - fail"), 4, 4, 4);
                add_bit_field(ack, "Source TEI",      QString::number(e.mpdu.src_tei), 5, 0, 12);
                add_bit_field(ack, "Destination TEI", QString::number(e.mpdu.dst_tei), 6, 4, 12);
                add_bit_field(ack, "RxPBNum",         QString::number(e.mpdu.ack_rx_pb_num), 8, 0, 3);
                add_bit_field(ack, "RSV0",            QString::number(e.mpdu.ack_rsv0), 8, 3, 5);
                add_bit_field(ack, "ChannelQuality",  QString("%1 dB").arg(e.mpdu.ack_channel_quality), 9, 0, 8);
                add_bit_field(ack, "STALoad",         QString::number(e.mpdu.ack_sta_load), 10, 0, 8);
                add_bit_field(ack, "RSV1",            QString::number(e.mpdu.ack_rsv1), 11, 0, 8);
                add_bit_field(ack, "ExtFrameType",    QString::number(e.mpdu.ack_ext_type), 12, 0, 4);
                break;
            case 1:
                add_bit_field(ack, "DstAddr", QString("0x%1")
                    .arg(e.mpdu.ack_dst_addr, 12, 16, QChar('0')), 4, 0, 48);
                add_bit_field(ack, "SearchTEI", QString::number(e.mpdu.ack_search_tei), 10, 0, 12);
                add_bit_field(ack, "SearchFreq", QString::number(e.mpdu.ack_search_freq), 11, 4, 4);
                break;
            case 2:
                add_bit_field(ack, "Timestamp", QString("0x%1")
                    .arg(e.mpdu.ack_sync_timestamp, 8, 16, QChar('0')), 4, 0, 32);
                add_bit_field(ack, "SyncTEI", QString::number(e.mpdu.ack_sync_tei), 8, 0, 12);
                break;
            case 3:
                add_bit_field(ack, "DstAddr", QString("0x%1")
                    .arg(e.mpdu.ack_dst_addr, 12, 16, QChar('0')), 4, 0, 48);
                add_bit_field(ack, "HRF Channel", QString::number(e.mpdu.ack_rx_pb_num), 10, 0, 8);
                add_bit_field(ack, "HRF Option",  QString::number(e.mpdu.ack_sta_load), 11, 0, 8);
                break;
            default:
                add_item(ack, "Reserved ExtFrameType", QString::number(e.mpdu.ack_ext_type));
                break;
        }
    } else if (e.mpdu.frame_type == 3) {
        // COORD 可变区域(表26):TimeDuration(4,16)/NextTimeSlotShift(6,16)
        // NeighbourNID(8,24)/NetRfChannel(11,8)/RSV0(12,0,4)
        auto* coord = add_item(root, "COORD", "");
        add_bit_field(coord, "TimeDuration",    QString("%1 ms").arg(e.mpdu.coord_duration), 4, 0, 16);
        add_bit_field(coord, "NextTimeSlotShift", QString("%1 ms").arg(e.mpdu.coord_shift), 6, 0, 16);
        add_bit_field(coord, "NeighbourNID",    QString("0x%1")
            .arg(e.mpdu.coord_neighbour_nid, 6, 16, QChar('0')), 8, 0, 24);
        add_bit_field(coord, "NetRfChannel",    QString::number(e.mpdu.coord_rf_channel), 11, 0, 8);
        add_bit_field(coord, "RSV0",            QString::number(e.mpdu.coord_rsv0), 12, 0, 4);
    }

    expandAll();
}

namespace {
/// @brief 本文件用户可见中文字符串 → 英文翻译注册
struct I18nReg {
    I18nReg() {
        trl::register_en("报文优先级(越小越低)", "message priority (smaller = lower)");
        trl::register_en("业务分类LID", "service-class LID");
        trl::register_en("无效值", "invalid value");
    }
};
const I18nReg g_i18n_reg_protocoltree;
}  // namespace
