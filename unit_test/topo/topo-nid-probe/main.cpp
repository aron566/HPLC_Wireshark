#include <QApplication>
#include <QComboBox>
#include <QSet>
#include <cstdio>
// 复刻 topo_window.cpp 当前（3885267 之后）refresh_nids + set_current_nid +
// force_display_nid 的关键段：同步循环在提前返回之前；refresh_nids 列表未变不重建，
// 第 3 步用 force_display_nid 收敛显示；NID 消失回落首个并重建内容。
static QComboBox* combo = nullptr;
static quint32 m_current_nid = 0;
static int rebuild_count = 0;

static void rebuild_all() { ++rebuild_count; }  // 仅计数，不建内容

// force_display_nid (topo_window.cpp:736)：仅显示 + 更新 m_current_nid，不重建
static void force_display_nid(quint32 nid) {
    m_current_nid = nid;
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toUInt() == nid) {
            if (combo->currentIndex() != i) {
                combo->blockSignals(true);
                combo->setCurrentIndex(i);
                combo->blockSignals(false);
            }
            break;
        }
    }
}

// set_current_nid (topo_window.cpp:814)：UI 同步无条件执行，之后才早退
static void set_current_nid(quint32 nid) {
    const bool same_nid = (nid == m_current_nid);
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toUInt() == nid) {
            if (combo->currentIndex() != i) {
                combo->blockSignals(true);
                combo->setCurrentIndex(i);
                combo->blockSignals(false);
            }
            break;
        }
    }
    if (same_nid) return;
    m_current_nid = nid;
    rebuild_all();
}

// refresh_nids (topo_window.cpp:751)：states 为当前 NID 集合，labels 为对应下拉文案
static void refresh_nids(const QSet<quint32>& states, const QVector<QString>& labels,
                         const QVector<quint32>& nids) {
    const quint32 prev = m_current_nid;
    bool same_list = (nids.size() == combo->count());
    if (same_list) {
        for (int i = 0; i < nids.size(); ++i) {
            if (combo->itemText(i) != labels[i] || combo->itemData(i).toUInt() != nids[i]) {
                same_list = false;
                break;
            }
        }
    }
    if (!same_list) {
        combo->blockSignals(true);
        combo->clear();
        for (int i = 0; i < nids.size(); ++i) combo->addItem(labels[i], nids[i]);
        combo->blockSignals(false);
    }
    if (states.contains(prev)) {
        force_display_nid(prev);
    } else if (combo->count() > 0) {
        const quint32 first = combo->itemData(0).toUInt();
        const bool changed = (first != m_current_nid);
        force_display_nid(first);
        if (changed) rebuild_all();
    } else {
        rebuild_all();
    }
}

static int failures = 0;
static void check(const char* name, bool ok) {
    printf("%s: %s (index=%d data=0x%x m_current=0x%x rebuilds=%d)\n", name,
           ok ? "PASS" : "FAIL", combo->currentIndex(), combo->currentData().toUInt(),
           m_current_nid, rebuild_count);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QComboBox c;
    combo = &c;
    const quint32 nid1 = 0x1111, nid2 = 0x2222;
    const QVector<quint32> two = {nid1, nid2};
    const QVector<QString> two_labels = {"NID:0x1111", "NID:0x2222"};

    // (a) 收起下拉后仍显示所选 NID：选 NID2 后 popup_closed -> refresh_nids（列表未变）
    combo->addItem("NID:0x1111", nid1);
    combo->addItem("NID:0x2222", nid2);
    combo->setCurrentIndex(1);
    m_current_nid = nid2;
    refresh_nids(QSet<quint32>{nid1, nid2}, two_labels, two);
    check("a_popup_closed_keeps_selection",
          combo->currentIndex() == 1 && combo->currentData().toUInt() == nid2
              && m_current_nid == nid2);

    // (b) 定时刷新后仍显示所选 NID：文案变化迫使重建（clear 后 index 回 0），
    //     第 3 步 force_display 须把显示收敛回 NID2；另直接测 set_current_nid 同值同步。
    const QVector<QString> two_labels_v2 = {"NID:0x1111 #9", "NID:0x2222 #9"};
    refresh_nids(QSet<quint32>{nid1, nid2}, two_labels_v2, two);
    check("b_timer_refresh_rebuild_keeps_selection",
          combo->currentIndex() == 1 && combo->currentData().toUInt() == nid2
              && m_current_nid == nid2);
    // 旧 bug 的精确复现点：裸 clear+重建后 index=0，再调同值 set_current_nid
    combo->blockSignals(true);
    combo->clear();
    combo->addItem("NID:0x1111 #9", nid1);
    combo->addItem("NID:0x2222 #9", nid2);
    combo->blockSignals(false);
    set_current_nid(nid2);  // 同值：旧代码首行早退致错位，新代码先同步后早退
    check("b_same_nid_set_syncs_index",
          combo->currentIndex() == 1 && combo->currentData().toUInt() == nid2);

    // (c) NID 消失时回落首个：NID2 从 states 消失 -> 显示首个 + m_current 改为首个
    rebuild_count = 0;
    refresh_nids(QSet<quint32>{nid1}, {"NID:0x1111"}, {nid1});
    check("c_missing_nid_falls_back_to_first",
          combo->currentIndex() == 0 && combo->currentData().toUInt() == nid1
              && m_current_nid == nid1 && rebuild_count == 1);

    printf(failures ? "RESULT: %d FAILED\n" : "RESULT: ALL PASS\n", failures);
    return failures ? 1 : 0;
}
