// gw26_rfdl_probe:HDC 2.0 单跳帧(消息类型0 无线/2 载波发现列表)RFDL 解析验证
// 直接调用 GW_2026_MsduParser::parse() 喂入合成的单跳 MSDU 载荷,打印 topo_event.rssi_entries。
// (与 gw_rfdl_probe.cpp 同构,gw_2026 侧载波/无线两簇 RSSI 映射)
#include <QCoreApplication>
#include <QByteArray>
#include <QDebug>
#include <cstdio>
#include "gw_2026_msdu_parser.h"

static QByteArray hex(const QString& s) {
    QByteArray out;
    QString t = s; t.remove(' '); t.remove('\n'); t.remove('\t');
    for (int i = 0; i + 1 < t.size(); i += 2)
        out.append(char(t.mid(i, 2).toInt(nullptr, 16)));
    return out;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        qDebug() << "usage: gw26_rfdl_probe <hex>";
        return 1;
    }
    const QByteArray body = hex(QString::fromLocal8Bit(argv[1]));
    auto info = GW_2026_MsduParser::parse(body);
    fprintf(stderr, "summary: %s | simple_head: %d | present: %d\n",
            info.summary.toLocal8Bit().constData(), (int)info.simple_head, (int)info.present);
    fprintf(stderr, "topo_event: kind=%d rssi_entries=%d link_type=%d\n",
            (int)info.topo_event.kind, (int)info.topo_event.rssi_entries.size(),
            info.topo_event.link_type);
    for (const auto& pr : info.topo_event.rssi_entries)
        fprintf(stderr, "  rssi: TEI=%u -> %d dBm\n", (unsigned)pr.first, pr.second);
    return 0;
}
