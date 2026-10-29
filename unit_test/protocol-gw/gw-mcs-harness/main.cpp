// 渲染 RF SOF 字段树,打印 MCS/Payload PB Size/Frame Length/Reserved 行
#include <QApplication>
#include <QTreeWidget>
#include <cstdio>
#include "bplcframe.h"
#include "gw_2022_tree.h"
#include "gw_2022_parser.h"

static void dump(QTreeWidgetItem* it, int depth) {
    std::printf("%*s%s | %s\n", depth * 2, "", qPrintable(it->text(0)), qPrintable(it->text(1)));
    for (int i = 0; i < it->childCount(); ++i) dump(it->child(i), depth + 1);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    PacketEntry e;
    e.meta.is_rf = 1;
    e.mpdu.frame_type = (quint8)GW_2022_FrameType::SOF;
    e.mpdu.version = 1;
    e.mpdu.src_tei = 1; e.mpdu.dst_tei = 2; e.mpdu.link_id = 0;
    e.mpdu.frame_len = 0x100;
    e.mpdu.pb_num = 1; e.mpdu.pb_num_field = 3;      // 大小码 3 -> 136B
    e.mpdu.symbol_num = 0x1AB;                        // 保留原始值
    e.mpdu.tmi = 7;
    e.mpdu.tmi_ext = 5;                               // 保留原始值
    e.mpdu.pb_size = 136;
    QTreeWidget tw;
    auto* root = new QTreeWidgetItem(&tw);
    GW_2022_TreeBuilder builder; builder.build(root, e);
    dump(root, 0);
    return 0;
}
