"""snake.py - 智能把驼峰命名转成 snake_case

保护规则:
  - Qt 类/方法/宏(Q_OBJECT、QString、setText 等)不动
  - QT_ 开头的宏不动
  - 已有的 snake_case 单词不动
  - SCREAMING_SNAKE_CASE 枚举值不动
  - 字符串字面量内的内容不动
  - 注释不动(只动代码标识符)
"""
import re
import sys

# 这些是 Qt / C++ 标准 / 项目里已经被保护的标识符,绝不替换
PROTECTED = {
    # 项目自定义类/结构体(类型名保留 CamelCase,只改成员和方法)
    "PhysicalMeta", "BplcFrame", "MpduInfo", "PacketEntry",
    "MsduState", "BplcParser", "CommConfigDialog",
    "DispatcherWorker", "FrameDispatcher", "FrameStatistics",
    "HexView", "PacketListModel", "ProtocolTree",
    "ReaderWorker", "SerialReader", "RawFrameImporter",
    "ByteRingBuffer", "FrameQueue", "ReaderConfig",
    "MainWindow", "Ui", "CommConfigDialog",
    # Qt 类
    "QObject", "QWidget", "QString", "QStringList", "QByteArray", "QDateTime", "QList", "QHash",
    "QMap", "QVector", "QQueue", "QStack", "QMutex", "QMutexLocker", "QSemaphore",
    "QThread", "QTimer", "QObject", "QVariant", "QModelIndex", "QAbstractTableModel",
    "QAbstractItemView", "QHeaderView", "QSortFilterProxyModel",
    "QTableView", "QTreeWidget", "QTreeWidgetItem", "QPlainTextEdit", "QTextEdit",
    "QSplitter", "QStatusBar", "QMenuBar", "QToolBar", "QAction", "QToolButton",
    "QComboBox", "QLineEdit", "QLabel", "QPushButton", "QCheckBox", "QRadioButton",
    "QGroupBox", "QDialog", "QFileDialog", "QInputDialog", "QSettings",
    "QFile", "QTextStream", "QColor", "QPainter", "QFont", "QFontMetrics",
    "QScrollBar", "QSize", "QPoint", "QRect", "QPalette", "QKeySequence",
    "QApplication", "QCoreApplication", "QMetaObject", "QMetaType",
    "QSerialPort", "QSerialPortInfo", "QStandardPaths", "QShortcut",
    "QMenu", "QStatusBar", "QHeaderView", "QFontMetrics",
    # Qt 枚举(由 QSerialPort::DataBits 等)
    "DataBits", "StopBits", "Parity",
    "NoParity", "EvenParity", "OddParity", "MarkParity", "SpaceParity",
    "Data5", "Data6", "Data7", "Data8",
    "OneStop", "OneAndHalfStop", "TwoStop",
    # Qt 宏
    "Q_OBJECT", "Q_ENUM", "Q_DECLARE_METATYPE", "Q_INVOKABLE", "Q_PROPERTY",
    "Q_SIGNALS", "Q_SLOTS", "Q_FOREACH", "Q_UNUSED",
    "QStringLiteral", "QChar", "Qt", "Q_ASSERT",
    # Qt 方法
    "setText", "setMinimumWidth", "setMaximumWidth", "setReadOnly", "setFont",
    "setLineWrapMode", "setColumnCount", "setHeaderLabels", "setUniformRowHeights",
    "setAlternatingRowColors", "setSelectionBehavior", "setSelectionMode",
    "setSortingEnabled", "setShowGrid", "setEditTriggers", "setModel",
    "setVerticalScrollBarPolicy", "setPalette", "setStyleSheet",
    "setIconSize", "setMovable", "setWindowTitle", "setStretchLastSection",
    "setLayout", "setCentralWidget", "setMenuBar", "setStatusBar",
    "setToolTip", "setEnabled", "setVisible", "setFocus", "setCheckable",
    "setCurrentText", "setCurrentIndex", "setItemSelected",
    "setSectionResizeMode", "setHorizontalScrollBarPolicy",
    "setSelectionModel",
    # Qt 方法(类成员函数)— 全部保留 camelCase
    "getOpenFileName", "getSaveFileName", "getExistingDirectory",
    "getMultiLineText", "getInt", "getItem", "getText",
    "currentDateTime", "currentMSecsSinceEpoch", "toString",
    "addItem", "addItems", "addWidget", "addSeparator", "addAction",
    "exec", "accepted", "rejected",
    "horizontalAdvance", "horizontalHeader", "verticalHeader",
    "resizeSection", "resizeContentsPrecision",
    "setResizeMode", "sectionResizeMode",
    "connectSlotsByName", "moveToThread",
    "model", "selectionModel",
    "isChecked", "isEnabled", "isVisible",
    "isEmpty", "isReadOnly", "isValid",
    "removeSelectedText", "keepAnchor",
    "movePosition", "Start", "Down", "KeepAnchor",
    "showMessage",
    "geometry", "saveGeometry", "restoreGeometry",
    "deleteLater", "qt_getEnumName", "qt_getEnumName",
    "data", "flags", "headerData", "rowCount", "columnCount",
    "getData", "setData", "highlightRange",
    "showPacket", "selectedByteRange", "setSource", "on_SourceTypeChanged",
    # Qt 头文件/常量(出现在 #include 里)
    "QtEndian", "QtCore", "QtGui", "QtWidgets", "QtSerialPort",
    # 标准库 / 通用
    "true", "false", "nullptr", "void", "int", "char", "bool", "unsigned",
    "signed", "short", "long", "float", "double", "auto",
    "const", "static", "extern", "register", "inline", "virtual", "explicit",
    "public", "private", "protected", "friend", "operator", "template",
    "typedef", "typename", "using", "namespace", "struct", "class", "union",
    "enum", "case", "default", "switch", "if", "else", "for", "while",
    "do", "break", "continue", "return", "goto", "sizeof", "new", "delete",
    "this", "try", "catch", "throw", "and", "or", "not",
    "std", "stdin", "stdout", "stderr",
    # 文件名/路径(我们不对斜杠做改动)
    # 项目已有 snake_case
    "snake_case", "snake", "MPDU_BASE", "MPDU_SOF", "MPDU_ACK", "MPDU_COORD",
    # ReaderConfig 内部字段保持不变(已 snake_case 风格)
}


def is_already_snake(token: str) -> bool:
    """判断是否已经是 snake_case 或 SCREAMING_SNAKE,避免重复改"""
    if "_" in token:
        return True
    # 全部小写单词
    if token.islower():
        return True
    # 全大写单词(SCREAMING)
    if token.isupper():
        return True
    return False


def to_snake(name: str) -> str:
    """驼峰转 snake_case

    getData -> get_data
    dataLength -> data_length
    parseMpduBase -> parse_mpdu_base
    """
    if not name or name in PROTECTED or is_already_snake(name):
        return name

    # 在小写/大写边界插入下划线
    s = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name)
    # 连续大写后跟小写:HTTPResponse -> HTTP_Response
    s = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2', s)
    return s.lower()


def transform_line(line: str) -> str:
    """处理一行代码,只替换标识符,不动字符串/注释"""
    # 剥离字符串字面量
    def repl_token(m):
        tok = m.group(0)
        return to_snake(tok)

    # 匹配标识符:以字母/下划线开头,后跟字母/数字/下划线
    # 不替换字符串/注释里的内容(粗略:不替换 ".." 内容,// 后面不替换)
    # 简化:把代码里的 token 提出来

    # 移除行尾注释
    code_only = line
    # 不拆 comment,因为 // 可能出现在字符串里... 但工程里都是真注释,简化
    # 改用更安全策略:跳过 "..." 内部
    out = []
    i = 0
    in_str = False
    while i < len(code_only):
        c = code_only[i]
        if c == '"' and (i == 0 or code_only[i-1] != '\\'):
            in_str = not in_str
            out.append(c)
            i += 1
            continue
        if c == '/' and i + 1 < len(code_only) and code_only[i+1] == '/' and not in_str:
            # 行尾注释,原样保留
            out.append(code_only[i:])
            break
        if in_str:
            out.append(c)
            i += 1
            continue
        # 标识符起点
        if c.isalpha() or c == '_':
            j = i
            while j < len(code_only) and (code_only[j].isalnum() or code_only[j] == '_'):
                j += 1
            tok = code_only[i:j]
            new_tok = to_snake(tok)
            out.append(new_tok)
            i = j
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def process_file(path: str, dry_run: bool = False):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    new_lines = [transform_line(l) for l in text.splitlines(keepends=True)]
    new_text = ''.join(new_lines)
    if new_text == text:
        return False
    if not dry_run:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(new_text)
    return True


if __name__ == '__main__':
    files = sys.argv[1:]
    if not files:
        print("Usage: python snake.py <files...>")
        sys.exit(1)
    for f in files:
        changed = process_file(f)
        print(f"{'+' if changed else ' '} {f}")
