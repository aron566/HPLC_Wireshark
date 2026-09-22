/// @file nw_2021_beacon_parser.cpp
/// @brief 南网 NW_2021 信标帧载荷区解析实现
/// @details 对照数据链路层报批稿表24(信标帧载荷字段)/表25-28(信标类型/组网/
///          多网络优选/开始关联)/表29-31(信标管理信息)及 Python 参考
///          MPDU_BEACON_LOAD。信标物理块 = 固定头 + 管理信息 + BPCS(CRC32)
///          + 保留字节 + 物理块检查序列(CRC24)。
#include "nw_2021_beacon_parser.h"
#include "common/fieldspec.h"
#include "common/fieldtools.h"
#include "crc.h"
#include "i18n.h"

namespace {

// 信标类型名称(表25:0 发现/1 代理/2 中央/其它保留)
static QString beacon_type_name(quint8 t) {
    switch (t) {
        case 0: return trl::L("发现信标");
        case 1: return trl::L("代理信标");
        case 2: return trl::L("中央信标");
        default: return trl::L("保留");
    }
}

// 信标条目头名称(表31)
static QString beacon_item_head_name(quint8 h) {
    switch (h) {
        case 0x01: return trl::L("站点能力条目");
        case 0x02: return trl::L("时隙分配条目");
        case 0x06: return trl::L("路由参数条目");
        case 0x07: return trl::L("频段变更条目");
        case 0x0A: return trl::L("频段探测条目");
        case 0x0B: return trl::L("万年历同步条目");
        default:
            return (h >= 0x80 && h <= 0xEF) ? trl::L("厂家自定义条目")
                                            : trl::L("保留条目");
    }
}

// 信标帧载荷固定头(表24):信标类型/组网标志/多网络优选/开始关联/
// 组网序列号/短网络标识,字节 2 bit4 起 28b 保留
static const FieldSpec kBeaconLoadSpec[] = {
    {"BeaconType",             0, 0, 3,  Fmt::DEC},
    {"NetWorkingFlag",         0, 3, 1,  Fmt::DEC},
    {"RSV0",                   0, 4, 1,  Fmt::DEC},
    {"MultiNetChooseFuncFlag", 0, 5, 1,  Fmt::DEC},
    {"AssociationFlag",        0, 6, 1,  Fmt::DEC},
    {"RSV1",                   0, 7, 1,  Fmt::DEC},
    {"NetSN",                  1, 0, 8,  Fmt::DEC},
    {"ShortNIDBeacon",         2, 0, 4,  Fmt::DEC},
    {"RSV2",                   2, 4, 28, Fmt::DEC},
};
static const int kBeaconLoadSpecN = int(sizeof(kBeaconLoadSpec) / sizeof(kBeaconLoadSpec[0]));

}  // namespace

MsduInfo NW_2021_BeaconParser::parse_beacon(const QByteArray& payload, int pbsize) {
    MsduInfo out;
    out.present = false;
    if (pbsize < 16 || payload.size() < 16 + pbsize) return out;

    // 信标载荷区 = payload[16 : 16+pbsize-3](固定头 + 管理信息 + BPCS + 保留字节),
    // 不含块尾 3B 物理块检查序列。
    QByteArray gb = payload.mid(16, pbsize - 3);
    if (gb.size() < 8) return out;
    out.present = true;

    // BPCS(帧载荷校验序列,32-bit):gb 倒数第 5..2 字节(小端);
    // 覆盖前 gb.size()-5 字节(固定头 + 管理信息,不含 BPCS 与保留字节)。
    quint32 bpcs = 0;
    bool bpcs_ok = false;
    if (gb.size() >= 5) {
        bpcs = (quint8)gb[gb.size() - 5]
             | ((quint32)(quint8)gb[gb.size() - 4] << 8)
             | ((quint32)(quint8)gb[gb.size() - 3] << 16)
             | ((quint32)(quint8)gb[gb.size() - 2] << 24);
        quint32 calc = crc32_le(
            reinterpret_cast<const quint8*>(gb.constData()), gb.size() - 1);
        bpcs_ok = (bpcs == calc);
    }
    out.summary = QStringLiteral("BEACON BPCS CRC32:%1")
                      .arg(bpcs_ok ? QStringLiteral("OK") : QStringLiteral("FAIL"));

    auto& root = group(out.tree, QStringLiteral("Beacon Load [%1B]").arg(gb.size()));

    // 固定头(表24)
    add_fields(root.children, gb, 0, kBeaconLoadSpec, kBeaconLoadSpecN);

    // 值解释:信标类型(表25)/组网标志(表26)/多网络优选(表27)/开始关联(表28)
    const quint8 beacon_type = (quint8)get_bits(gb, 0, 0, 3);
    for (auto& ch : root.children) {
        if (ch.name.startsWith(QStringLiteral("BeaconType"))) {
            ch.value = QStringLiteral("%1 - %2").arg(ch.value, beacon_type_name(beacon_type));
        } else if (ch.name.startsWith(QStringLiteral("NetWorkingFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 3, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("组网完成") : trl::L("组网未完成"));
        } else if (ch.name.startsWith(QStringLiteral("MultiNetChooseFuncFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 5, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("使能网络评估") : trl::L("未使能网络评估"));
        } else if (ch.name.startsWith(QStringLiteral("AssociationFlag"))) {
            const quint8 v = (quint8)get_bits(gb, 0, 6, 1);
            ch.value = QStringLiteral("%1 - %2").arg(v).arg(
                v ? trl::L("允许站点发起关联请求")
                  : trl::L("不允许站点发起关联请求"));
        }
    }

    // 信标管理信息(表29):gb[6 : gb.size()-5],首字节条目数,随后 头+长度+内容
    const int mgmt_start = 6;
    const int mgmt_end   = gb.size() - 5;  // BPCS 起点
    if (mgmt_end > mgmt_start + 1) {
        const quint8 item_num = (quint8)gb[mgmt_start];
        auto& mg = group(root.children, QStringLiteral("Beacon Mgmt Info"));
        MsduFieldNode in;
        in.name      = QStringLiteral("ItemNum [8b]");
        in.value     = QString::number(item_num);
        in.rel_start = mgmt_start;
        in.rel_len   = 1;
        mg.children.append(in);

        int pos = mgmt_start + 1;
        for (int n = 0; n < item_num && pos < mgmt_end; ++n) {
            const int head_abs = pos;
            const quint8 head = (quint8)gb[pos++];
            // 长度字段大小:0x02(时隙分配)用 2B,其余 1B(表31)
            const int len_bytes = (head == 0x02) ? 2 : 1;
            if (pos + len_bytes > mgmt_end) break;
            quint32 len_raw = 0;
            for (int k = 0; k < len_bytes; ++k)
                len_raw |= (quint32)(quint8)gb[pos + k] << (8 * k);
            pos += len_bytes;
            // 条目内容长:时隙分配 len-3(头1+长度2),其余 len-2(头1+长度1)
            const int item_len = (head == 0x02) ? int(len_raw) - 3 : int(len_raw) - 2;
            if (item_len < 0 || pos + item_len > mgmt_end) break;
            const QByteArray it = gb.mid(pos, item_len);
            const int abs0 = pos;
            pos += item_len;

            auto& grp = group(mg.children,
                trl::L("Item[%1] %2 (%3B)").arg(n).arg(beacon_item_head_name(head)).arg(item_len));
            MsduFieldNode hd;
            hd.name      = QStringLiteral("ItemHead [8b]");
            hd.value     = QStringLiteral("0x%1 - %2").arg(head, 2, 16, QChar('0'))
                               .arg(beacon_item_head_name(head));
            hd.rel_start = head_abs;
            hd.rel_len   = 1;
            grp.children.append(hd);
            MsduFieldNode ln;
            ln.name      = (len_bytes == 2) ? QStringLiteral("ItemLen [16b]")
                                            : QStringLiteral("ItemLen [8b]");
            ln.value     = QString::number(len_raw);
            ln.rel_start = head_abs + 1;
            ln.rel_len   = len_bytes;
            grp.children.append(ln);
            MsduFieldNode body;
            body.name      = QStringLiteral("Content [%1B]").arg(item_len);
            body.value     = QString::fromLatin1(it.toHex(' ').toUpper());
            body.rel_start = abs0;
            body.rel_len   = item_len;
            grp.children.append(body);
        }
    }

    // BPCS(帧载荷校验序列,32-bit)
    {
        MsduFieldNode crc;
        crc.name      = QStringLiteral("BPCS [32b]");
        crc.value     = QStringLiteral("0x%1 %2")
                            .arg(bpcs, 8, 16, QChar('0'))
                            .arg(bpcs_ok ? QStringLiteral("OK") : QStringLiteral("FAIL"));
        crc.rel_start = gb.size() - 5;
        crc.rel_len   = 4;
        root.children.append(crc);
    }
    // 保留字节(1B)
    {
        MsduFieldNode rsv;
        rsv.name      = QStringLiteral("RSV [8b]");
        rsv.value     = QStringLiteral("0x%1").arg((quint8)gb[gb.size() - 1], 2, 16, QChar('0'));
        rsv.rel_start = gb.size() - 1;
        rsv.rel_len   = 1;
        root.children.append(rsv);
    }

    // 物理块检查序列(24-bit,块尾 3B;覆盖 PB 头+块体+保留,表7)
    if (payload.size() >= 16 + pbsize) {
        const quint8* blk = reinterpret_cast<const quint8*>(payload.constData()) + 16;
        quint32 stored = (quint32)blk[pbsize - 3]
                       | ((quint32)blk[pbsize - 2] << 8)
                       | ((quint32)blk[pbsize - 1] << 16);
        const bool ok24 = (stored == crc24_lsb(blk, pbsize));
        MsduFieldNode pbc;
        pbc.name      = QStringLiteral("PB CRC24");
        pbc.value     = QStringLiteral("0x%1 %2")
                            .arg(stored, 6, 16, QChar('0'))
                            .arg(ok24 ? QStringLiteral("OK") : QStringLiteral("FAIL"));
        pbc.rel_start = pbsize - 3;  // 相对载荷区起点(blk = payload+16)
        pbc.rel_len   = 3;
        root.children.append(pbc);
    }

    return out;
}

// ===== i18n:文件级 中→英 显示词典(仅显示翻译;未命中回退中文) =====
namespace {
struct I18nReg {
    I18nReg() {
        trl::register_en("发现信标", "Discovery Beacon");
        trl::register_en("代理信标", "Proxy Beacon");
        trl::register_en("中央信标", "Central Beacon");
        trl::register_en("保留", "Reserved");
        trl::register_en("组网完成", "Network established");
        trl::register_en("组网未完成", "Network not established");
        trl::register_en("使能网络评估", "Network estimation enabled");
        trl::register_en("未使能网络评估", "Network estimation disabled");
        trl::register_en("允许站点发起关联请求", "Association request allowed");
        trl::register_en("不允许站点发起关联请求", "Association request not allowed");
        trl::register_en("站点能力条目", "STA Capability Item");
        trl::register_en("时隙分配条目", "Time Slot Allocation Item");
        trl::register_en("路由参数条目", "Route Parameter Item");
        trl::register_en("频段变更条目", "Band Change Item");
        trl::register_en("频段探测条目", "Band Probe Item");
        trl::register_en("万年历同步条目", "Calendar Sync Item");
        trl::register_en("厂家自定义条目", "Vendor-defined Item");
        trl::register_en("保留条目", "Reserved Item");
        trl::register_en("Item[%1] %2 (%3B)", "Item[%1] %2 (%3B)");
    }
};
const I18nReg g_i18n_reg_nw_beacon;
}  // namespace
