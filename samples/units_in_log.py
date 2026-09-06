"""扫 log,提取所有 '字段: 数值+单位' 形态,统计每字段的显示样例,供 Qt 对照。"""
import re, collections

LOG = r"D:\code\HPLC_HRF\监控器\data\BPLCMonitorGW-2026.09.04-13.02.23.log"
# 形如: RoutePeriod: 80s / BeaconPeriod: 2100ms / ProxyChannelQuality: 19dB / DownCommRate: 100%
pat = re.compile(r"(?:^|[\s|])([A-Za-z_][A-Za-z_0-9]*): (-?\d+(?:\.\d+)?)(s|ms|dB|%|Hz|khz|kHz|KHz)\b")

stats = collections.defaultdict(collections.Counter)  # 字段 -> 单位计数
samples = collections.defaultdict(set)
with open(LOG, encoding="utf-8", errors="replace") as f:
    for ln in f:
        for m in pat.finditer(ln):
            name, val, unit = m.group(1), m.group(2), m.group(3)
            # 排除行内杂散上下文(TIME: / timestamp 等开头的大写元信息)
            if name in ("TIME", "PARA", "PBSize", "get"):
                continue
            stats[name][unit] += 1
            if len(samples[name]) < 3:
                samples[name].add(f"{val}{unit}")

for name in sorted(stats):
    units = ", ".join(f"{u}x{n}" for u, n in stats[name].most_common())
    ex = " / ".join(sorted(samples[name])[:3])
    print(f"{name:32s} [{units}]  例: {ex}")
