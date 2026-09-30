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
import sys, html
md = open(sys.argv[1], encoding='utf-8').read()
# 纯 stdlib 兜底(不依赖 markdown 模块):原文转义 + 等宽预排,
# 表格/标题不完整渲染但内容可读;有 pandoc 时走上面分支、完整渲染
css = "body{font-family:'Microsoft YaHei',sans-serif;max-width:900px;margin:24px auto;padding:0 16px;} table{border-collapse:collapse;width:100%;} th,td{border:1px solid #bbb;padding:6px 10px;text-align:left;} th{background:#f0f0f0;} code{background:#f5f5f5;padding:1px 4px;border-radius:3px;} pre{background:#f5f5f5;padding:10px;border-radius:4px;overflow-x:auto;white-space:pre-wrap;}"
body = f'<pre>{html.escape(md)}</pre>'
open(sys.argv[2], 'w', encoding='utf-8').write(
    f'<html><head><meta charset="utf-8"><style>{css}</style></head><body>{body}</body></html>')
PY
fi

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

# 候选浏览器:Chrome 优先(本机 Edge 某版本 headless --print-to-pdf 静默失败:
# --version 无输出、打印无产物),Edge 回退。逐个尝试并校验产物确实生成,
# 都失败则报错(不再无条件打印"生成"掩盖失败)。
#
# CI 环境注意:Chrome headless 必须加 --no-sandbox(否则在 CI/容器里静默失败),
# --disable-dev-shm-usage 避免 /dev/shm 不足导致崩溃。
CHROME_FLAGS="--headless --no-sandbox --disable-gpu --disable-dev-shm-usage --no-pdf-header-footer"
BROWSERS=(
    "/c/Program Files/Google/Chrome/Application/chrome.exe"
    "/c/Program Files (x86)/Google/Chrome/Application/chrome.exe"
    "/c/Program Files/Microsoft/Edge/Application/msedge.exe"
    "/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
)
# 额外:用 where 命令兜底找浏览器(处理 choco 装到非常规路径的情况)
for exe in chrome.exe msedge.exe; do
    wpath=$(where "$exe" 2>/dev/null | head -1)
    [ -n "$wpath" ] && BROWSERS+=("$wpath")
done
OK=0
for B in "${BROWSERS[@]}"; do
    [ -x "$B" ] || [ -f "$B" ] || continue
    echo "md2pdf: 尝试浏览器 $B"
    "$B" $CHROME_FLAGS \
        --print-to-pdf="$OUTWIN" "file:///$HTMLWIN" >/dev/null 2>&1 || true
    if [ -f "$OUT" ]; then OK=1; echo "md2pdf: 成功 ($B)"; break; fi
    echo "md2pdf: $B 未生成 PDF,尝试下一个"
done

rm -f "$HTML"
if [ "$OK" != 1 ]; then
    echo "错误: md2pdf 打印失败($IN → $OUT)"
    echo "  已尝试的浏览器:"
    for B in "${BROWSERS[@]}"; do
        if [ -f "$B" ]; then echo "    - $B (存在,但未生成 PDF)"
        else echo "    - $B (不存在)"; fi
    done
    echo "  请检查 Chrome/Edge 是否正确安装,或加 --no-sandbox 相关 flag"
    exit 1
fi
echo "生成: $OUT"
