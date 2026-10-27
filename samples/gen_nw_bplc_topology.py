#!/usr/bin/env python3
"""构造南网 BPLC 1.0 三级网络测试数据集 (9 节点, 载波+无线混合)。

复用 gen_nw_bplc_frames.py 的 CRC/封装/帧构建 helpers。
拓扑:
  CCO (TEI=1, 载波)
  ├─ 代理A (TEI=2, 载波)
  │   ├─ STA (TEI=3, 载波)
  │   └─ STA (TEI=4, 无线)
  └─ 代理B (TEI=5, 无线)
      ├─ STA (TEI=6, 无线)
      └─ 代理C (TEI=7, 载波)
          ├─ STA (TEI=8, 无线)
          └─ STA (TEI=9, 载波)

帧清单 (每节点: 关联请求→CCO, 关联确认←CCO, 关联指示→代理, 发现列表→广播)
"""

import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_nw_bplc_frames import (
    crc24, crc32, le16, le24, le32, put_tei12, put_tei12h,
    plc_pb_size, build_fch, build_sof, build_std_mac, build_msdu_mme,
    wrap_frame, SNID, CCO_MAC, STA_MAC,
)

BROADCAST_MAC = b'\xff\xff\xff\xff\xff\xff'

def mac_of(tei):
    """MAC 分配: CCO=ce..ce, 其余 = 00:00:00:00:00:0(tei+1)"""
    if tei == 1:
        return CCO_MAC
    return bytes([0, 0, 0, 0, 0, tei + 1])

# 角色: 1=STA, 2=PCO(代理), 4=CCO
ROLE_STA, ROLE_PCO, ROLE_CCO = 1, 2, 4

# 9 节点拓扑: (TEI, 角色, 层级, 代理TEI, 链路类型)  链路: 0=载波, 1=无线
NODES = [
    (1, ROLE_CCO, 0, 0, 0),
    (2, ROLE_PCO, 1, 1, 0),   # 代理A
    (3, ROLE_STA, 2, 2, 0),
    (4, ROLE_STA, 2, 2, 1),   # 无线
    (5, ROLE_PCO, 1, 1, 1),   # 代理B 无线
    (6, ROLE_STA, 2, 5, 1),
    (7, ROLE_PCO, 2, 5, 0),   # 代理C
    (8, ROLE_STA, 3, 7, 1),   # 无线
    (9, ROLE_STA, 3, 7, 0),
]
NODE_BY_TEI = {n[0]: n for n in NODES}

def neighbors_of(tei):
    """返回 tei 的邻居 (代理 + 子节点 + CCO 的子代理)."""
    nb = set()
    _, _, _, proxy, _ = NODE_BY_TEI[tei]
    if proxy:
        nb.add(proxy)
    for t, _, _, p, _ in NODES:
        if p == tei:
            nb.add(t)
    if tei == 1:
        for t, _, _, p, _ in NODES:
            if p == 1:
                nb.add(t)
    return sorted(nb)


# ============ 关联请求 (0x0030) : STA → CCO ============
def build_assoc_req(sta_tei, proxy_tei, link_type):
    sta_mac = mac_of(sta_tei)
    body = bytearray()
    body += sta_mac                       # byte6-11 STA MAC
    for i in range(5):                    # 候选代理 TEI ×5 (2B/条)
        b = bytearray(2)
        if i == 0:
            put_tei12(b, 0, proxy_tei)
            if proxy_tei:
                b[1] |= (link_type & 1) << 4  # 链路类型 bit4(仅占位有效时置位)
        body += bytes(b)
    body += b'\x00' * 3                   # byte22-24 相线
    body += bytes([0x01])                 # byte25 设备类型
    body += b'\x00' * 2                   # byte26-27 保留
    body += bytes([0x00])                 # byte28 MAC 类型
    body += bytes([0x00])                 # byte29 模块+链路
    body += le32(0)                       # byte30-33 关联随机数
    body += b'\x00' * 18                  # byte34-51 厂商信息
    body += bytes([0x00])                 # byte52 BOOT 原因
    body += bytes([0x00])                 # byte53 BOOT 版本
    body += le16(0)                       # byte54-55 软件版本
    body += b'\x00' * 2                   # byte56-57 版本时间
    body += le16(0)                       # byte58-59 厂商 ID
    body += le16(0)                       # byte60-61 芯片代码
    body += le16(0)                       # byte62-63 硬复位
    body += le16(0)                       # byte64-65 软复位
    body += bytes([0x02])                 # byte66 代理类型(动态代理)
    body += bytes([SNID & 0xFF])          # byte67 网络序列号
    body += bytes([0x00])                 # byte68 MMe 版本
    body += bytes([0x00])                 # byte69 频段支持
    body += le32(0)                       # byte70-73 端到端序列号
    msdu = build_msdu_mme(CCO_MAC, sta_mac, 0x0030, bytes(body))
    # STA 未入网, MAC 层 src_tei=0, dst=CCO(1)
    mac = build_std_mac(1, 0, msdu, head_flag=0)
    return build_sof(0, 1, mac, tmi=4)


# ============ 关联指示 (0x0034) : CCO → 代理 ============
def build_assoc_ind(sta_tei, sta_mac, proxy_tei, link_type, sta_level):
    body = bytearray()
    body += bytes([0x00])                 # byte6 result=0
    body += bytes([sta_level])            # byte7 sta level
    body += sta_mac                       # byte8-13 STA MAC
    body += CCO_MAC                       # byte14-19 CCO MAC
    b = bytearray(2)
    put_tei12(b, 0, sta_tei)              # byte20-21 sta TEI
    body += bytes(b)
    body += le16(proxy_tei)               # byte22-23 proxy TEI
    body += bytes([(link_type & 1)])      # byte24 link_type(bit0, mask 0x01)
    body += b'\x00' * 2                   # byte25-26 rsv
    body += bytes([0x01])                 # byte27 total pkgs
    body += bytes([0x00])                 # byte28 pkg idx
    body += bytes([0x01])                 # byte29 last pkg
    body += le32(0)                       # byte30-33 assoc rand
    body += b'\x00' * 17                  # byte34-50 gap
    body += bytes([SNID & 0xFF])          # byte51 net_sn
    body += b'\x00' * 2                   # byte52-53 rsv
    body += le32(0)                       # byte54-57 reassoc time
    body += le32(0)                       # byte58-61 e2e seq
    body += b'\x00' * 8                   # byte62-69 rsv
    body += b'\x00' * 8                   # byte70+ 路由信息(空)
    msdu = build_msdu_mme(mac_of(proxy_tei), CCO_MAC, 0x0034, bytes(body))
    mac = build_std_mac(proxy_tei, 1, msdu, head_flag=0)
    return build_sof(1, proxy_tei, mac, tmi=11)


# ============ 发现列表 (0x0055) : 节点 → 广播 ============
def build_disc_list(tei, up=100, down=100, minr=100):
    _, role, level, proxy, _ = NODE_BY_TEI[tei]
    nb = neighbors_of(tei)
    body = bytearray()
    body += le16(tei)                     # byte6-7 TEI
    body += bytes([role])                 # byte8 role
    body += bytes([level])                # byte9 level
    body += mac_of(tei)                   # byte10-15 MAC
    body += le16(proxy)                   # byte16-17 proxy TEI
    body += b'\x00' * 3                   # byte18-20 rsv
    body += bytes([0x01])                 # byte21 rate_finish
    body += le32(up)                      # byte22-25 代理上行成功率
    body += le32(down)                    # byte26-29 代理下行成功率
    body += le16(len(nb))                 # byte30-31 站点数量
    body += le16(0)                       # byte32-33 发送数量
    body += le16(0)                       # byte34-35 上行路由条目数
    body += bytes([0])                    # byte36 路由条目大小
    body += b'\x00' * 2                   # byte37-38 rsv
    body += le16(0)                       # byte39-40 路由剩余时间
    body += bytes([0])                    # byte41 相线
    body += bytes([minr])                 # byte42 最小成功率
    body += b'\x00' * 5                   # byte43-47 rsv
    bitmap = bytearray(128)
    for t in nb:
        if t < 1024:
            bitmap[t // 8] |= 1 << (t % 8)
    body += bytes(bitmap)                 # 位图 128B
    for _ in nb:
        body += bytes([1])                # 收到发现列表数 (每置位 1B)
    msdu = build_msdu_mme(BROADCAST_MAC, mac_of(tei), 0x0055, bytes(body))
    mac = build_std_mac(0xFFF, tei, msdu, head_flag=0)
    return build_sof(tei, 0xFFF, mac, tmi=9)


# ============ 生成完整帧清单 ============
def build_all():
    frames = []  # (name, mpdu)
    manifest = []  # (frame_no, name, src, dst, purpose)

    # P0: CCO 信标
    from gen_nw_bplc_frames import build_beacon
    frames.append(("beacon_cco", build_beacon()))
    manifest.append(("beacon_cco", 1, "广播", "CCO 中央信标"))

    # 单跳帧发现列表: P0 提取产物, MMe 体为实包 STA2 站点属性真字节(表217)
    from gen_nw_bplc_frames import build_singlehop_rfdl
    frames.append(("singlehop_rfdl_sta2", build_singlehop_rfdl()))
    manifest.append(("singlehop_rfdl_sta2", 1, 2, "CCO→TEI2 单跳无线发现列表(STA2 站点属性实包真字节, P0 提取)"))

    # P1: 每个 STA(2-9) 关联请求 → CCO
    for tei, _, _, proxy, link in NODES[1:]:
        frames.append((f"assoc_req_tei{tei}", build_assoc_req(tei, proxy, link)))
        manifest.append((f"assoc_req_tei{tei}", tei, 1, f"TEI{tei} 关联请求(候选代理 {proxy})"))

    # 关联确认 ← CCO (result=0, link_type)
    from gen_nw_bplc_frames import build_std_mac as _m
    for tei, role, level, proxy, link in NODES[1:]:
        body = bytearray()
        body += mac_of(tei)               # STA MAC
        body += bytes([0x00])             # result=0
        body += bytes([level])            # level
        b = bytearray(2); put_tei12(b, 0, tei); body += bytes(b)  # sta TEI
        body += le16(proxy)               # proxy TEI
        body += bytes([0x01])             # total pkgs
        body += bytes([0x00])             # pkg idx
        body += bytes([0x01])             # last pkg
        body += bytes([(link & 1)])       # link_type bit0 (mask 0x01)
        body += le32(0) + le32(0) + le32(0) + le32(0)  # rand/reassoc/e2e/path
        body += bytes([SNID & 0xFF])      # net_sn
        body += bytes([0x00])             # mme_ver
        body += b'\x00\x00'               # gap
        body += b'\x00' * 8               # 路由信息(空)
        msdu = build_msdu_mme(mac_of(tei), CCO_MAC, 0x0031, bytes(body))
        mac = _m(tei, 1, msdu, head_flag=0)
        frames.append((f"assoc_cnf_tei{tei}", build_sof(1, tei, mac, tmi=4)))
        manifest.append((f"assoc_cnf_tei{tei}", 1, tei, f"CCO→TEI{tei} 关联确认(link={link})"))

    # 关联指示 → 代理 (只对有代理的 STA)
    for tei, _, level, proxy, link in NODES[1:]:
        if proxy == 1:
            continue  # 一级代理直接关联 CCO, 无上级代理指示(可选省略)
        frames.append((f"assoc_ind_tei{tei}", build_assoc_ind(tei, mac_of(tei), proxy, link, level)))
        manifest.append((f"assoc_ind_tei{tei}", 1, proxy, f"CCO→代理{proxy} 关联指示(TEI{tei})"))

    # 发现列表 → 广播
    for tei, _, _, _, _ in NODES:
        frames.append((f"disc_list_tei{tei}", build_disc_list(tei)))
        manifest.append((f"disc_list_tei{tei}", tei, "广播", f"TEI{tei} 发现列表"))

    return frames, manifest


def main():
    frames, manifest = build_all()
    out_dir = os.path.dirname(os.path.abspath(__file__))
    bin_path = os.path.join(out_dir, "test_topology_nw.bin")

    with open(bin_path, "wb") as f:
        f.write(bytes(8))  # 文件头占位
        for name, mpdu in frames:
            f.write(wrap_frame(mpdu))

    # manifest json
    man = {
        "nodes": [{"tei": t, "mac": mac_of(t).hex(":"), "role": r, "level": lv,
                   "proxy_tei": p, "link_type": ("RF" if lt else "PLC")}
                  for t, r, lv, p, lt in NODES],
        "frames": [{"no": i + 1, "name": n, "src": s, "dst": d, "purpose": u}
                   for i, (n, s, d, u) in enumerate(manifest)],
    }
    with open(os.path.join(out_dir, "test_topology_nw.json"), "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=2)

    print(f"生成 {len(frames)} 帧 → {bin_path}")
    for i, (n, s, d, u) in enumerate(manifest):
        print(f"  [{i+1:2d}] {n:<20} {str(s):>5} → {str(d):>6}  {u}")


if __name__ == "__main__":
    main()
