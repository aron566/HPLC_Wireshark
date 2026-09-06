/// @file modelstress.cpp
/// @brief 复现:PacketListModel + QTableView 批量追加是否死锁
/// @details 与 MainWindow::on_flush_buffer -> append_packets 相同路径。
///          正常结束打印 PASS;若卡死则被 timeout 杀掉(exit 124)。
#include "packetlistmodel.h"
#include <QApplication>
#include <QTableView>
#include <cstdio>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    PacketListModel model;
    QTableView view;
    view.setModel(&model);
    view.resize(800, 300);
    view.show();
    for (int i = 0; i < 3; ++i) app.processEvents();  // 让视图完成布局/连接

    for (int batch = 0; batch < 20; ++batch) {
        QVector<PacketEntry> entries;
        entries.reserve(200);
        for (int i = 0; i < 200; ++i) {
            PacketEntry e;
            e.index    = batch * 200 + i + 1;
            e.epoch_ms = 0;
            e.accepted = true;
            e.mpdu.frame_type = 0;          // BEACON
            e.mpdu.net_id = 0xA1D500;
            e.meta.is_rf = false;
            e.raw_bytes = QByteArray(40, char(0x55));
            entries.append(e);
        }
        model.append_packets(entries);
        app.processEvents();
        std::printf("batch %d rows=%d\n", batch, model.rowCount());
        std::fflush(stdout);
    }
    std::printf("PASS rows=%d\n", int(model.rowCount()));
    return 0;
}
