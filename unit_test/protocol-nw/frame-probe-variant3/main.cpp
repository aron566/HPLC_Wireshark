// 变体3:文件内容 = 一份完整 MAC 帧(头+体+CRC32),直接喂 NW_2021_MsduParser 并打印字段树
#include <QCoreApplication>
#include <QFile>
#include <cstdio>
#include "nw_2021_msdu_parser.h"
static void dump(const QVector<MsduFieldNode>& ns, int d) {
    for (const auto& n : ns) {
        std::printf("%*s%s = %s\n", d * 2, "", qPrintable(n.name), qPrintable(n.value));
        dump(n.children, d + 1);
    }
}
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) return 1;
    const MsduInfo m = NW_2021_MsduParser::parse(f.readAll());
    std::printf("present=%d summary=%s\n", (int)m.present, qPrintable(m.summary));
    if (m.present) dump(m.tree, 1);
    return 0;
}
