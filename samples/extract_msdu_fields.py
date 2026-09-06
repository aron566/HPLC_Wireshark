"""提取指定类行区间内 ALL self.XXX = BitDefine(a,b,c) 行(任意缩进,含方法内)"""
import re

SRC = r"D:\code\HPLC_HRF\监控器\BPLCMonitor\MSDU_Class.py"
WANT = ["MMeAssocReq", "MMeAssocCnf", "MMeChangeProxyReq",
        "MMeChangeProxyBitMapCnf", "MMeHeartBeatCheck",
        "MMeDiscoverNodeList", "MMeSuccessRateReport",
        "APP_EventPacket"]
lines = open(SRC, encoding='utf-8').read().splitlines()

pat_cls = re.compile(r'^class (\w+)[:(]')
starts = []
for i, ln in enumerate(lines):
    m = pat_cls.match(ln)
    if m:
        starts.append((i, m.group(1)))
starts.append((len(lines), None))

def ranges_of(name):
    out = []
    for i in range(len(starts) - 1):
        idx, nm = starts[i]
        if nm == name:
            out.append((idx, starts[i + 1][0]))
    return out

# 在类区间内找方法定义(缩进4的行 def ...),打印方法名以便分块
pat_def = re.compile(r'^    def (\w+)')
pat_bit = re.compile(r'^\s+self\.(\w+)\s*=\s*BitDefine\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,?\s*(\d+)?\s*\)')

for name in WANT:
    for (s, e) in ranges_of(name):
        print(f"\n########## {name}  (line {s+1}..{e}) ##########")
        last = None
        for i in range(s, min(e, s + 900)):
            ln = lines[i]
            md = pat_def.match(ln)
            if md:
                last = md.group(1)
            m = pat_bit.search(ln)
            if m:
                extra = f",{m.group(5)}" if m.group(5) else ""
                print(f"  [{last}] {m.group(1)} = BitDefine({m.group(2)},{m.group(3)},{m.group(4)}{extra})  (L{i+1})")
