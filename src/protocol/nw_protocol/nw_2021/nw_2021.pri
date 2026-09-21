# nw_2021.pri - 南网 NW_2021 协议解析(待实现)
#
# 南网双模 2021 报批版。与国网 GW_2022 完全独立:
#   - nw_2021_parser.h/.cpp          总控(SNID 4b / 载荷 PB 大小查表)
#   - nw_2021_msdu_parser.h/.cpp     MSDU 头(MAC 48b + VLAN 标签)
#   - nw_2021_pb_table.h            载荷 PB 大小 → PB size(16/40/72/136/264/520)
#   - beacon/  sof/  ack/  coord/   各帧型解析
#
# 参考实现:D:/code/HPLC_HRF/监控器/BPLCMonitorPython_SG(MPDU_Class.py / MSDU_Class.py)
