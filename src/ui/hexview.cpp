/// @file hexview.cpp
/// @brief HexView 实现
/// @details 高亮采用 QPlainTextEdit::setExtraSelections 文本选区,
///          由 Qt 原生处理坐标映射与滚动跟随,不会出现自绘错位。
#include "hexview.h"
#include "i18n.h"
#include <QTextEdit>     // QTextEdit::ExtraSelection
#include <QTextCursor>
#include <QTextCharFormat>
#include <QFont>
#include <QFontMetrics>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>

// 行布局(见 render_hex):
//   偏移列 "0000  " = 6 字符
//   每字节 "xx "    = 3 字符;第 8 字节(col==7)之后多 1 空格
//   hex 与 ASCII 之间 1 空格;ASCII 16 字符;行尾 '\n'
static const int LINE_STRIDE = 6 + 16 * 3 + 1 + 1 + 16 + 1;  // = 73

HexView::HexView(QWidget* parent) : QPlainTextEdit(parent),
    m_hl_start(-1), m_hl_len(0) {
    setReadOnly(true);
    setFont(QFont("Consolas", 9));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFontMetrics fm(font());
    int char_w = fm.horizontalAdvance('0');
    setMinimumWidth(char_w * 78 + 30);
}

void HexView::set_data(const QByteArray& bytes) {
    m_bytes = bytes;
    render_hex();
    rebuild_highlight();
}

void HexView::clear() {
    m_bytes.clear();
    m_hl_start = -1;
    m_hl_len = 0;
    QPlainTextEdit::clear();
    setExtraSelections({});
}

QByteArray HexView::highlighted_bytes() const {
    if (m_hl_start < 0 || m_hl_len <= 0 || m_hl_start >= m_bytes.size())
        return {};
    int end = qMin(m_hl_start + m_hl_len, m_bytes.size());
    return m_bytes.mid(m_hl_start, end - m_hl_start);
}

void HexView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QByteArray sel = highlighted_bytes();
    if (sel.isEmpty()) {
        auto* act = menu.addAction(trl::L("无高亮字节可复制(先点击协议字段)"));
        act->setEnabled(false);
        menu.exec(event->globalPos());
        return;
    }
    // 组装两种粘贴格式
    QString plain, prefixed;
    for (int i = 0; i < sel.size(); ++i) {
        const QString h = QString("%1").arg((quint8)sel[i], 2, 16, QChar('0'));
        if (i) { plain += ' '; prefixed += ' '; }
        plain += h;
        prefixed += "0x" + h;
    }
    auto* a1 = menu.addAction(trl::L("复制 Hex(%1 字节)").arg(sel.size()));
    auto* a2 = menu.addAction(trl::L("复制为 0x 前缀(%1 字节)").arg(sel.size()));
    QAction* hit = menu.exec(event->globalPos());
    if (hit == a1)      QApplication::clipboard()->setText(plain);
    else if (hit == a2) QApplication::clipboard()->setText(prefixed);
}

void HexView::highlight_range(int start, int len) {
    m_hl_start = start;
    m_hl_len = len;
    rebuild_highlight();
}

int HexView::char_offset_of_byte(int byte_index) const {
    int row = byte_index / 16;
    int col = byte_index % 16;
    // 该字节 hex 首字符在行内的位置
    int within_row = 6 + col * 3 + (col >= 8 ? 1 : 0);
    return row * LINE_STRIDE + within_row;
}

void HexView::rebuild_highlight() {
    if (m_bytes.isEmpty() || m_hl_start < 0 || m_hl_len <= 0) {
        setExtraSelections({});
        return;
    }
    int end = m_hl_start + m_hl_len;
    if (m_hl_start >= m_bytes.size()) {
        setExtraSelections({});
        return;
    }
    if (end > m_bytes.size()) end = m_bytes.size();

    QList<QTextEdit::ExtraSelection> sels;
    sels.reserve(end - m_hl_start);

    // 每个字节高亮其 2 个 hex 字符;同字节簇背景相同,视觉上连成段
    for (int b = m_hl_start; b < end; ++b) {
        QTextCursor cur(document());
        int pos = char_offset_of_byte(b);
        cur.setPosition(pos);
        cur.setPosition(pos + 2, QTextCursor::KeepAnchor);

        QTextEdit::ExtraSelection sel;
        sel.cursor = cur;
        sel.format.setBackground(QColor(255, 224, 32, 110));  // 半透明黄
        sels.append(sel);
    }
    setExtraSelections(sels);

    // 仅当高亮首行不在当前可视区时才滚动定位;视口内点击不跳动
    QScrollBar* vsb = verticalScrollBar();
    if (vsb) {
        const int row_h = fontMetrics().lineSpacing();
        const int first_row = m_hl_start / 16;
        const int vh = viewport()->height();
        const int top_row    = (vh > 0 && row_h > 0) ? vsb->value() / row_h : 0;
        const int rows_vis   = (vh > 0 && row_h > 0) ? vh / row_h : 1;
        if (first_row < top_row || first_row >= top_row + rows_vis)
            vsb->setValue(first_row * row_h - 8);
    }
}

void HexView::render_hex() {
    // 只清文本,不调 clear()(那会清空 m_bytes 数据)
    QPlainTextEdit::clear();
    if (m_bytes.isEmpty()) return;
    QString out;
    out.reserve(m_bytes.size() * 4);

    const int per_line = 16;
    for (int i = 0; i < m_bytes.size(); i += per_line) {
        out += QString("%1  ").arg(i, 4, 16, QChar('0'));
        for (int j = 0; j < per_line; ++j) {
            if (i + j < m_bytes.size()) {
                out += QString("%1 ").arg((quint8)m_bytes[i + j], 2, 16, QChar('0'));
            } else {
                out += "   ";
            }
            if (j == 7) out += " ";
        }
        out += " ";
        for (int j = 0; j < per_line && (i + j) < m_bytes.size(); ++j) {
            quint8 c = (quint8)m_bytes[i + j];
            out += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar('.');
        }
        out += "\n";
    }
    setPlainText(out);
}
