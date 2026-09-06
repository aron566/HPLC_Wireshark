"""Qt 侧核对:哪些字段已带单位、哪些裸。扫描 headless 输出与源码 spec 单位注释。"""
import re

# 1) headless_test.exe 的协议树 dump(字段名 [Nb] = 值)
import subprocess, os
os.environ["PATH"] = r"C:\Qt\6.10.1\mingw_64\bin" + ";" + os.environ["PATH"]
out = subprocess.run(
    [r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\headless_build\release\headless_test.exe"],
    capture_output=True, text=True,
    cwd=r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor", timeout=120).stdout

# log 中带单位的权威字段
log_units = {
    "BeaconPeriod": "ms", "BeaconSlotLen": "ms", "CSMASlotLen": "ms",
    "CSMASlotSplitLen": "ms", "ChannelQuality": "dB", "EvaluateBeginTimeout": "s",
    "NextRouteEstimationTime": "s", "NextTimeSlotShift": "ms",
    "PCOChannelQuality": "dB", "PCODiscoveryListPeriod": "s",
    "ProxyChannelQuality": "dB", "RfBeaconSlotLen": "ms", "RoutePeriod": "s",
    "STADiscoveryListPeriod": "s", "STAReAssocTime": "ms", "TDMASlotLen": "ms",
    "TimeDuration": "ms",
}
# 每字段在 headless 输出中的样例行
for name, unit in log_units.items():
    lines = [l for l in out.splitlines() if re.search(rf"\b{re.escape(name)}(?: \[[0-9]+b\])?\s*=", l)]
    if not lines:
        print(f"{name:28s} 未在 headless dump 中出现")
        continue
    # 去重显示前2个
    seen = []
    for l in lines:
        v = l.split("=", 1)[1].strip() if "=" in l else l
        if v not in seen:
            seen.append(v)
        if len(seen) >= 2:
            break
    has_unit = any(re.search(rf"{re.escape(unit)}\b", s) for s in seen)
    flag = "OK " if has_unit else "MISSING"
    print(f"{name:28s} {flag}  [{unit}]  样例: {' ; '.join(seen)}")
