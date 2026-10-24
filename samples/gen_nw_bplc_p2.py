#!/usr/bin/env python3
"""构造南网 BPLC 1.0 P2 事件帧 (8 帧: 离线/延迟离线/心跳/成功率上报)。

布局依据用户指定规约原文 (10.5 第5部分 2021报批版.md):
  表71 离线指示 0x0049   L1458  20B: 站点TEI 0-1 / 原因 2-3 / 站点MAC 4-9 / 代理TEI 10-11 / 保留 12-19
  表73 延迟离线 0x005D   L1486  16+N*6: 原因(表74仅0x3) / 站点总数 / 延迟时间(秒) / 保留10 / MAC N*6
  表76 心跳检测 0x0051   L1520  139B: 原始源TEI / 最大发现站点TEI / 最大发现数(32b) / 位图130B / 保留1
  表82 成功率上报 0x005E L1658  4+N*4: 代理TEI / 站点总数 / 表83条目(子站点TEI 2B + 下行1B + 上行1B)
字节序: L341 「低字节在前，低位在前」→ 16/32bit 一律小端(le16/le32)。
心跳位图: L1540-1542 第0字节bit1=TEI1, 第1字节bit0=TEI8 → TEI t(1起) = byte (t-1)//8 的 bit (t-1)%8。
成功率取值 0-100 (L1580)，全部填非零可区分值。

拓扑复用 gen_nw_bplc_topology.NODES (9 节点 3 级)。
产物: samples/test_topology_nw_p2.bin + .json (manifest 含布局自校验结果)
"""

import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_nw_bplc_frames import (
    le16, le32, build_msdu_mme, build_std_mac, build_sof,
    wrap_frame, CCO_MAC,
)
from gen_nw_bplc_topology import NODES, mac_of, BROADCAST_MAC, ROLE_STA, ROLE_PCO

CHILDREN = {p: [t for t, _, _, pp, _ in NODES if pp == p] for p in {1, 2, 5, 7}}


# ============ 0x0049 离线指示 (表71, 20B) : CCO → 代理 ============
def build_leave_ind(sta_tei, reason, proxy_tei):
    """原因: 0x0=未入网收到非关联报文 / 0x2=层级超上限 / 0x4=立即离线 (表72)."""
    body = bytearray()
    body += le16(sta_tei)                      # 0-1 站点 TEI (16b 小端)
    body += le16(reason)                       # 2-3 原因 (16b 小端, 表72)
    body += mac_of(sta_tei)                    # 4-9 站点 MAC
    body += le16(proxy_tei)                    # 10-11 代理 TEI (16b 小端)
    body += b'\x00' * 8                        # 12-19 保留
    assert len(body) == 20
    msdu = build_msdu_mme(mac_of(proxy_tei), CCO_MAC, 0x0049, bytes(body))
    mac = build_std_mac(proxy_tei, 1, msdu, head_flag=0)
    return build_sof(1, proxy_tei, mac, tmi=4), body


# ============ 0x005D 延迟离线 (表73) : CCO → 广播 ============
def build_delay_leave(offs_tei, delay_s=30):
    """原因仅 0x3 (CCO 判断站点不在最新白名单, 表74). 延迟时间单位秒."""
    body = bytearray()
    body += le16(3)                            # 0-1 原因 = 0x3
    body += le16(len(offs_tei))                 # 2-3 站点总数
    body += le16(delay_s)                      # 4-5 延迟时间(秒, 16b 小端)
    body += b'\x00' * 10                       # 6-15 保留
    for t in offs_tei:
        body += mac_of(t)                      # 16+ 站点 MAC (N*6)
    msdu = build_msdu_mme(BROADCAST_MAC, CCO_MAC, 0x005D, bytes(body))
    mac = build_std_mac(0xFFF, 1, msdu, head_flag=0)
    return build_sof(1, 0xFFF, mac, tmi=4), body


# ============ 0x0051 心跳检测 (表76, 139B) : 站点 → CCO (OSTEI 不变) ============
def build_heartbeat(orig_tei, max_tei, max_cnt, found_teis):
    body = bytearray()
    body += le16(orig_tei)                     # 0-1 原始源 TEI (OSTEI, 转发不变)
    body += le16(max_tei)                      # 2-3 发现站点数最大的站点 TEI
    body += le32(max_cnt)                      # 4-7 最大发现站点数 (32b 小端)
    bitmap = bytearray(130)                    # 8-137 可发现站点 TEI 位图
    for t in found_teis:
        bitmap[(t - 1) // 8] |= 1 << ((t - 1) % 8)
    body += bytes(bitmap)
    body += b'\x00'                            # 138 保留
    assert len(body) == 139
    msdu = build_msdu_mme(mac_of(1), mac_of(orig_tei), 0x0051, bytes(body))
    mac = build_std_mac(1, orig_tei, msdu, head_flag=0)
    return build_sof(orig_tei, 1, mac, tmi=11), body


def heartbeat_bitmap_check(body, found_teis):
    """位图按文档 L1540-1542 规则回读."""
    bm = body[8:138]
    got = [t for t in range(1, 1041) if bm[(t - 1) // 8] >> ((t - 1) % 8) & 1]
    assert got == sorted(found_teis), f"位图回读 {got} != {sorted(found_teis)}"
    return got


# ============ 0x005E 通信成功率上报 (表82+83) : 仅代理站点 → CCO ============
def build_succ_rate(agent_tei, entries):
    """entries: [(sta_tei, down, up)] 成功率 0-100 且非零."""
    body = bytearray()
    body += le16(agent_tei)                    # 0-1 代理站点 TEI (自身)
    body += le16(len(entries))                 # 2-3 站点总数 (子站点个数)
    for sta, down, up in entries:
        assert 0 < down <= 100 and 0 < up <= 100
        body += le16(sta)                      # 站点 TEI (16b 小端)
        body += bytes([down])                  # 下行通信成功率 (0-100)
        body += bytes([up])                    # 上行通信成功率 (0-100)
    msdu = build_msdu_mme(mac_of(1), mac_of(agent_tei), 0x005E, bytes(body))
    mac = build_std_mac(1, agent_tei, msdu, head_flag=0)
    return build_sof(agent_tei, 1, mac, tmi=4), body


def succ_rate_check(body, agent, entries):
    """按表82/83 回读 (小端)."""
    assert le16_read(body, 0) == agent
    n = le16_read(body, 2)
    assert n == len(entries)
    for i, (sta, down, up) in enumerate(entries):
        off = 4 + i * 4
        assert le16_read(body, off) == sta, f"entry{i} TEI 回读错"
        assert body[off + 2] == down and body[off + 3] == up
    return n


def le16_read(buf, off):
    return buf[off] | (buf[off + 1] << 8)


# ============ 帧清单 (8 帧) ============
def build_all():
    frames = []    # (name, mpdu, extra)
    manifest = []  # (name, src, dst, purpose)

    # 离线指示 ×2: CCO→代理, 通知子站点离线
    mpdu, body = build_leave_ind(9, reason=0x04, proxy_tei=7)
    frames.append(("leave_ind_tei9", mpdu, body))
    manifest.append(("leave_ind_tei9", 1, 7, "CCO→代理7 离线指示(TEI9, 原因0x4立即离线)"))

    mpdu, body = build_leave_ind(6, reason=0x00, proxy_tei=5)
    frames.append(("leave_ind_tei6", mpdu, body))
    manifest.append(("leave_ind_tei6", 1, 5, "CCO→代理5 离线指示(TEI6, 原因0x0未入网)"))

    # 延迟离线 ×1: CCO→广播, 2 站点延迟 30s 离线 (白名单 0x3)
    mpdu, body = build_delay_leave([8, 9], delay_s=30)
    frames.append(("delay_leave_tei8_9", mpdu, body))
    manifest.append(("delay_leave_tei8_9", 1, "广播", "CCO 延迟离线(TEI8/9, 原因0x3, 延迟30s)"))

    # 心跳 ×2: OSTEI 发起者, 转发路径沿途汇总
    mpdu, body = build_heartbeat(orig_tei=9, max_tei=7, max_cnt=3, found_teis=[7, 8, 9])
    got = heartbeat_bitmap_check(body, [7, 8, 9])
    frames.append(("heartbeat_tei9", mpdu, body))
    manifest.append(("heartbeat_tei9", 9, 1, "TEI9 心跳(OSTEI=9, 最大发现=代理7/3站, 位图TEI7/8/9)"))

    mpdu, body = build_heartbeat(orig_tei=6, max_tei=5, max_cnt=3, found_teis=[5, 6, 7])
    got = heartbeat_bitmap_check(body, [5, 6, 7])
    frames.append(("heartbeat_tei6", mpdu, body))
    manifest.append(("heartbeat_tei6", 6, 1, "TEI6 心跳(OSTEI=6, 最大发现=代理5/3站, 位图TEI5/6/7)"))

    # 成功率上报 ×3: 每个 PCO 上报子站点逐 STA 成功率 (表83, 仅代理发送)
    for agent, ag_entries in (
        (2, [(3, 85, 92), (4, 73, 64)]),
        (5, [(6, 78, 88), (7, 91, 95)]),
        (7, [(8, 67, 79), (9, 82, 93)]),
    ):
        mpdu, body = build_succ_rate(agent, ag_entries)
        n = succ_rate_check(body, agent, ag_entries)
        frames.append((f"succ_rate_tei{agent}", mpdu, body))
        es = " ".join(f"TEI{a}(↓{d}/↑{u})" for a, d, u in ag_entries)
        manifest.append((f"succ_rate_tei{agent}", agent, 1,
                         f"代理{agent} 成功率上报({n}子站: {es})"))

    return frames, manifest


def main():
    frames, manifest = build_all()
    out_dir = os.path.dirname(os.path.abspath(__file__))
    bin_path = os.path.join(out_dir, "test_topology_nw_p2.bin")

    with open(bin_path, "wb") as f:
        f.write(bytes(8))
        for name, mpdu, _ in frames:
            f.write(wrap_frame(mpdu))

    man = {
        "mme": {"0x0049": "离线指示(表71)", "0x005d": "延迟离线(表73)",
                "0x0051": "心跳检测(表76)", "0x005e": "成功率上报(表82+83)"},
        "frames": [{"no": i + 1, "name": n, "src": s, "dst": d, "purpose": u}
                   for i, (n, s, d, u) in enumerate(manifest)],
    }
    with open(os.path.join(out_dir, "test_topology_nw_p2.json"), "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=2)

    print(f"生成 {len(frames)} 帧 → {bin_path}")
    for i, (n, mpdu, body) in enumerate(frames):
        print(f"  [{i+1}] {n:<20} MMe体 {len(body):>3}B  MPDU {len(mpdu):>3}B")
    print("布局自校验 (小端回读 + 位图 + 成功率 0-100 非零): 全部断言通过 ✓")


if __name__ == "__main__":
    main()
