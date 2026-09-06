/// @file hexview.h
/// @brief 16 字节/行格式的 hex+ASCII 视图控件
/// @details 高亮使用 QPlainTextEdit::setExtraSelections(文本选区),
///          由控件原生处理坐标/滚动/重绘,避免自绘 paintEvent 偏移问题。
#ifndef HEXVIEW_H
#define HEXVIEW_H

#include <QPlainTextEdit>
#include <QByteArray>

class HexView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit HexView(QWidget* parent = nullptr);

    /// @brief 设置待显示的原始字节并重新渲染
    void set_data(const QByteArray& bytes);

    /// @brief 高亮 [start, start+len) 字节(hex 列)。
    ///        传 (-1, 0) 清除高亮。
    void highlight_range(int start, int len);

    void clear();

private:
    /// @brief 计算第 byte_index 字节的 hex 首字符在文档中的位置
    /// @details 行格式: "0000  xx xx ... xx  xx ...  ascii\n"
    ///          每字节占 "xx " 3 字符,第 8 字节(col==7)后多 1 空格,
    ///          行首偏移列 4hex+2sp = 6 字符;行内容固定 72 字符 + '\n'。
    int char_offset_of_byte(int byte_index) const;

    void rebuild_highlight();

    QByteArray m_bytes;
    int        m_hl_start;
    int        m_hl_len;

    void render_hex();
};

#endif // HEXVIEW_H
