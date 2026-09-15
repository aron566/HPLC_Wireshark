#!/usr/bin/env bash
# md → pdf:pandoc(或 python-markdown)转 html,Edge/Chrome headless 打印 pdf
# 用法: bash scripts/md2pdf.sh <input.md> <output.pdf>
set -e
IN="${1:?用法: md2pdf.sh <input.md> <output.pdf>}"
OUT="${2:?用法: md2pdf.sh <input.md> <output.pdf>}"
HTML="${IN%.md}.html"

# md → html(优先 pandoc,回退 python-markdown)
if command -v pandoc >/dev/null 2>&1; then
    pandoc "$IN" -f gfm -t html5 -s \
        --metadata title="BPLC STA Monitor - Wireshark Plugin" -o "$HTML"
else
    python - "$IN" "$HTML" <<'PY'
import sys
import markdown
md = open(sys.argv[1], encoding='utf-8').read()
body = markdown.markdown(md, extensions=['tables', 'fenced_code'])
css = "body{font-family:'Microsoft YaHei',sans-serif;max-width:900px;margin:24px auto;padding:0 16px;} table{border-collapse:collapse;width:100%;} th,td{border:1px solid #bbb;padding:6px 10px;text-align:left;} th{background:#f0f0f0;} code{background:#f5f5f5;padding:1px 4px;border-radius:3px;} pre{background:#f5f5f5;padding:10px;border-radius:4px;overflow-x:auto;}"
open(sys.argv[2], 'w', encoding='utf-8').write(
    f'<html><head><meta charset="utf-8"><style>{css}</style></head><body>{body}</body></html>')
PY
fi

# Edge/Chrome headless 打印 pdf(无页眉页脚)
EDGE="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
[ -x "$EDGE" ] || EDGE="/c/Program Files/Google/Chrome/Application/chrome.exe"
# 路径转 Windows 绝对形式(兼容 MSYS /d/... 与原生 D:/... 与相对路径)
winpath() {
    case "$1" in
        /*) cygpath -w "$1" 2>/dev/null || echo "$1" ;;
        [A-Za-z]:/*) echo "$1" ;;
        *) cygpath -w "$PWD/$1" 2>/dev/null || echo "$PWD/$1" ;;
    esac
}
HTMLWIN=$(winpath "$HTML")
OUTWIN=$(winpath "$OUT")
"$EDGE" --headless --disable-gpu --no-pdf-header-footer \
    --print-to-pdf="$OUTWIN" "file:///$HTMLWIN"
rm -f "$HTML"
echo "生成: $OUT"
