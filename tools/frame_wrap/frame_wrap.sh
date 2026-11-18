#!/usr/bin/env bash
# 原始报文 -> 0x3C/0x3E 封装 txt 转换工具(Linux/macOS 启动器)
# 依赖: python3 + tkinter(多数发行版需单独安装: Debian/Ubuntu 为 python3-tk)
set -u
cd "$(dirname "$0")"

if command -v python3 >/dev/null 2>&1; then
    PY=python3
elif command -v python >/dev/null 2>&1; then
    PY=python
else
    echo "[错误] 未找到 python3，请先安装。" >&2
    exit 1
fi

# tkinter 缺失时给出明确提示(无图形环境同样会在此暴露)
if ! "$PY" -c "import tkinter" >/dev/null 2>&1; then
    echo "[错误] 当前 Python 缺少 tkinter 模块。" >&2
    echo "       Debian/Ubuntu: sudo apt-get install -y python3-tk" >&2
    echo "       Fedora:        sudo dnf install -y python3-tkinter" >&2
    echo "       也可先跑自检(不需图形环境): $PY frame_wrap_gui.py --selftest" >&2
    exit 1
fi

exec "$PY" frame_wrap_gui.py "$@"
