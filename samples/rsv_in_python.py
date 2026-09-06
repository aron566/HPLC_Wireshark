"""列出 Python 各协议类中所有 RSV/Reserved 字段定义,人工对照 Qt spec。"""
import re, io

def scan(path, tag):
    print(f"\n===== {tag} =====")
    cur_class = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = re.match(r"class (\w+)", ln)
            if m:
                cur_class = m.group(1)
                continue
            m = re.search(r"self\.(\w*(?:RSV|Reserved|Rsv)\w*)\s*=\s*BitDefine\(\s*([^)]+)\)", ln)
            if m and cur_class:
                print(f"{cur_class:34s} {m.group(1):28s} BitDefine({m.group(2)})")

scan(r"D:\code\HPLC_HRF\监控器\BPLCMonitor\MSDU_Class.py", "MSDU_Class.py(消息层)")
