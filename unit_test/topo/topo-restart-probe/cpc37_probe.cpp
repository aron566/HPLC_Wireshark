/// 探针:NW 0x0037 单个代理变更确认 link_type 提取
/// 合成 NW_2026/NW_2021 通用 MAC 帧(长头 32B + MSDU 长头 18B + MMe 0x0037),
/// 断言:成功+RF→(Cnf,1);成功+载波→(Cnf,0);失败+RF→(非Cnf,-1);
/// 成功+0x03→(Cnf,1,仅 bit0);成功+0x02→(Cnf,0,bit1 不是 link)
#include "nw_2026_msdu_parser.h"
#include "nw_2021_msdu_parser.h"
#include "common/crc.h"
#include <QCoreApplication>
#include <cstdio>

static int g_fail = 0;
#define CHECK(name, cond) do { \
    std::printf("%s %s\n", (cond) ? "PASS" : "FAIL", name); \
    if (!(cond)) ++g_fail; } while (0)

static void put_le16(QByteArray& b, int off, quint16 v) {
    b[off] = char(v & 0xFF); b[off + 1] = char((v >> 8) & 0xFF);
}
static void put_le32(QByteArray& b, int off, quint32 v) {
    for (int i = 0; i < 4; ++i) b[off + i] = char((v >> (8 * i)) & 0xFF);
}

// result: MMe Result(4B); link: mme[20] 整字节
static QByteArray build_frame(quint32 result, quint8 link) {
    QByteArray body(32 + 56, 0);
    body[0] = char(0x02);  // Version=1(bit1-2),长头 flag=0
    put_le16(body, 2, 56); // msdu_len = MSDU 长头 18 + MMe 38
    // MSDU 长头(相对 msdu_body):dst MAC 6B + src MAC 6B + VLAN + 类型
    put_le32(body, 32 + 12, 0x8100);  // VLAN MANAGEMENT
    put_le16(body, 32 + 16, 0x88E1);  // MSDU 类型 MANAGEMENT_MESSAGE
    // MMe(相对 msdu_body[18],绝对体偏移 50)
    const int m = 32 + 18;
    body[m + 0] = char(1);      // MMe Version
    put_le16(body, m + 1, 0x0037);
    put_le32(body, m + 6, result);
    body[m + 10] = char(1);     // TotalPacketNum
    put_le16(body, m + 12, 90); // STATEI
    put_le16(body, m + 14, 1);  // ProxyTEI
    body[m + 20] = char(link);  // LinkType 字节(bit0 有效)
    // CRC32:覆盖 msdu_body(56B),存体末 LE
    const quint32 crc = crc32_le(reinterpret_cast<const quint8*>(body.constData()) + 32, 56 + 4);
    body.resize(32 + 56 + 4);
    put_le32(body, 32 + 56, crc);
    return body;
}

static bool has_link_node(const MsduInfo& r) {
    // add_fields 命名为 "LinkType [8b]";translate_enum_i18n 改 value 为"值 - 释义",名不变
    for (const MsduFieldNode& n : r.tree)
        if (n.name.startsWith(QStringLiteral("LinkType")) && n.value.startsWith(QStringLiteral("1 -"))) return true;
    return false;
}

template <typename Parser>
static void run_suite(const char* tag, MsduInfo (*parse)(const QByteArray&)) {
    char buf[128];
    { MsduInfo r = parse(build_frame(0, 0x01));
      std::snprintf(buf, sizeof buf, "%s success+RF -> (Cnf,1)", tag);
      CHECK(buf, r.topo_event.kind == TopoEventKind::ChangeProxyCnf && r.topo_event.link_type == 1); }
    { MsduInfo r = parse(build_frame(0, 0x00));
      std::snprintf(buf, sizeof buf, "%s success+carrier -> (Cnf,0)", tag);
      CHECK(buf, r.topo_event.kind == TopoEventKind::ChangeProxyCnf && r.topo_event.link_type == 0); }
    { MsduInfo r = parse(build_frame(1, 0x01));
      std::snprintf(buf, sizeof buf, "%s fail+RF -> (not Cnf,-1)", tag);
      CHECK(buf, r.topo_event.kind != TopoEventKind::ChangeProxyCnf && r.topo_event.link_type == -1); }
    { MsduInfo r = parse(build_frame(0, 0x03));
      std::snprintf(buf, sizeof buf, "%s success+0x03 -> (Cnf,1,bit0 only)", tag);
      CHECK(buf, r.topo_event.kind == TopoEventKind::ChangeProxyCnf && r.topo_event.link_type == 1); }
    { MsduInfo r = parse(build_frame(0, 0x02));
      std::snprintf(buf, sizeof buf, "%s success+0x02 -> (Cnf,0,bit1 ignored)", tag);
      CHECK(buf, r.topo_event.kind == TopoEventKind::ChangeProxyCnf && r.topo_event.link_type == 0); }
    { MsduInfo r = parse(build_frame(0, 0x01));
      std::snprintf(buf, sizeof buf, "%s tree has LinkType node", tag);
      CHECK(buf, has_link_node(r)); }
    { MsduInfo r = parse(build_frame(0, 0x03));
      std::snprintf(buf, sizeof buf, "%s tree LinkType is 1bit (0x03 -> 1)", tag);
      bool ok = false;
      for (const MsduFieldNode& n : r.tree)
          if (n.name == QStringLiteral("LinkType [1b]") && n.value.startsWith(QStringLiteral("1 -"))) ok = true;
      CHECK(buf, ok); }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    run_suite<NW_2026_MsduParser>("NW26", &NW_2026_MsduParser::parse);
    run_suite<NW_2021_MsduParser>("NW21", &NW_2021_MsduParser::parse);
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
