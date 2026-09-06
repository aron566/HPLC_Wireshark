"""权威 BEACON 载荷解析参考:用 comdrv 切帧 + Python 原版 MPDU_Process,
打印 BEACON 帧载荷区(MPDU_BEACON_LOAD)全部字段,供 Qt 移植对照。"""
import sys
import os

BPLCM_DIR = r"D:\code\HPLC_HRF\监控器\BPLCMonitor"
sys.path.insert(0, BPLCM_DIR)
sys.path.insert(0, os.path.join(BPLCM_DIR, "data"))

import comdrv
import MPDU_Class
from MPDU_Class import MPDU_BASE, MPDU_BEACON, MPDU_BEACON_LOAD

BIN = r"D:\code\gitlab\HPLC_HRF_GW\monitor\BPLC_STA_QtMonitor\samples\replay_test.bin"
MSDU_Param = [0, 0, 0]
MSDU = []

driver = comdrv.comdrv(com=None, file_name=BIN, timeoutarg=0, logflag=0, baudrate=460800)

beacon_cnt = 0
while True:
    buf = driver.readtill3E()
    if not buf:
        break
    try:
        unesc = driver.array_post_handle(buf)
    except Exception:
        continue
    if len(unesc) < 10 + 16:
        continue
    data = list(unesc[6:])
    data.pop(0)
    data.pop(0)
    data.pop(0)
    # data[0] = isRF;随后为 MPDU
    mpdu_data = data[1:]
    base = MPDU_BASE()
    msgs = []
    if base.update_mpdu_content(list(mpdu_data), msgs) != 0:
        continue
    if base.FrameType.bit_field_content != 0:
        continue

    beacon_cnt += 1
    if beacon_cnt > 3:
        break
    b = MPDU_BEACON()
    err = b.update_mpdu_content(list(mpdu_data), msgs)
    print(f"\n===== BEACON#{beacon_cnt} PBSize={b.PBSize} err={err}")
    print(f"  TimeStamp=0x{b.TimeStamp.bit_field_content:08x} "
          f"SrcTEI={b.SourceTEI.bit_field_content} "
          f"TMI={b.TMI.bit_field_content} "
          f"Sym={b.SymbolNum.bit_field_content} "
          f"Line={b.LineNum.bit_field_content}")
    from MPDU_Class import gbBeaconMPDU, gbPBSize
    g = bytes(gbBeaconMPDU)
    print(f"  gbBeaconMPDU len={len(g)} gbPBSize={gbPBSize}")
    print(f"  hex: {g.hex()}")
    load = MPDU_BEACON_LOAD()
    msgs2 = []
    err2 = load.update_mpdu_content(list(mpdu_data), msgs2)
    print(f"  LOAD err={err2}")
    print(f"  BeaconType={load.BeaconType.bit_field_content} "
          f"NetWorking={load.NetWorkingFlag.bit_field_content} "
          f"Simple={load.SimpleBeaconFlag.bit_field_content} "
          f"Assoc={load.AssociationFlag.bit_field_content} "
          f"CE={load.BeaconCEFlag.bit_field_content}")
    print(f"  NetSN={load.NetSN.bit_field_content}")
    print(f"  CCO_MAC=0x{load.CCO_MAC_ADDR.bit_field_content:012x}")
    print(f"  BeaconPeriodCount={load.BeaconPeriodCount.bit_field_content}")
    print(f"  NetRfChannel={load.NetRfChannel.bit_field_content} "
          f"NetRfOption={load.NetRfOption.bit_field_content}")
    print(f"  ItemNum={load.BeaconItemNum} "
          f"STACapNum={load.STACapilityItemNum} "
          f"RouteNum={load.RouteParamItemNum} "
          f"BandChange={load.BandChangeItemNum} "
          f"TSA={load.TimeSlotAllocaItemNum}")
    for it in load.STACapilityItem:
        print(f"  STA Cap: TEI={it.TEI.bit_field_content} "
              f"PCOTEI={it.PCOTEI.bit_field_content} "
              f"Rate={it.LinkMinCommSuccessRate.bit_field_content} "
              f"MAC=0x{it.SourceMAC.bit_field_content:012x} "
              f"Role={it.Role.bit_field_content} "
              f"Level={it.NetLevel.bit_field_content} "
              f"ChQ={it.PCOChannelQuality.bit_field_content} "
              f"Line={it.STALine.bit_field_content} "
              f"RFHop={it.LinkRFHopNum.bit_field_content}")
    for it in load.RouteParamItem:
        print(f"  Route: Period={it.RoutePeriod.bit_field_content} "
              f"Next={it.NextRouteEstimationTime.bit_field_content} "
              f"PCODisc={it.PCODiscoveryListPeriod.bit_field_content} "
              f"STADisc={it.STADiscoveryListPeriod.bit_field_content}")
    for it in load.TimeSlotAllocaItem:
        print(f"  TSA: NonCCO={it.NonCCOBeaconNum.bit_field_content} "
              f"CCO={it.CCOBeaconNum.bit_field_content} "
              f"CSMALines={it.CSMALineSupportNum.bit_field_content} "
              f"PCO={it.PCOBeaconNum.bit_field_content} "
              f"SlotLen={it.BeaconSlotLen.bit_field_content} "
              f"CSMASplit={it.CSMASlotSplitLen.bit_field_content} "
              f"BindLines={it.BindingCSMALineSupportNum.bit_field_content} "
              f"BindLinkID={it.BindingCSMALinkID.bit_field_content} "
              f"TDMALen={it.TDMASlotLen.bit_field_content} "
              f"TDMALinkID={it.TDMALinkID.bit_field_content} "
              f"StartNTB=0x{it.BeaconPeriodStartNTB.bit_field_content:08x} "
              f"Period={it.BeaconPeriod.bit_field_content} "
              f"RfSlotLen={it.RfBeaconSlotLen.bit_field_content}")
        for nb in it.NonCCOBeaconInfo:
            print(f"    NonCCO: TEI={nb.TEI.bit_field_content} "
                  f"Type={nb.BeaconType.bit_field_content} "
                  f"RF={nb.RFBeaconFlag.bit_field_content}")
        for cs in it.CSMASlotInfo:
            print(f"    CSMA: len={cs.CSMASlotLen.bit_field_content} "
                  f"line={cs.CSMASlotLine.bit_field_content}")
        for bc in it.BindingCSMASlotInfo:
            print(f"    Binding: len={bc.BindingCSMASlotLen.bit_field_content} "
                  f"line={bc.BindingCSMASlotLine.bit_field_content}")
