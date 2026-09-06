/// @file hexview.cpp
/// @brief HexView 实现
/// @details 高亮采用 QPlainTextEdit::setExtraSelections 文本选区,
///          由 Qt 原生处理坐标映射与滚动跟随,不会出现自绘错位。
#include "hexview.h"
#include <QTextEdit>     // QTextEdit::ExtraSelection
#include <QTextCursor>
#include <QTextCharFormat>
#include <QFont>
#include <QFontMetrics>
#include <QScrollBar>

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

    // 让高亮行滚动可见
    int first_row = m_hl_start / 16;
    ensureCursorVisible();
    QScrollBar* vsb = verticalScrollBar();
    if (vsb) {
        int row_h = fontMetrics().lineSpacing();
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
