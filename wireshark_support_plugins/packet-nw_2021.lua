--[[
    packet-nw_2021.lua
    南网 NW_2021 双模通信(高速载波+无线) 数据链路层协议 dissector
    依据: 南网双模 2021 报批稿。字段坐标对照 Qt 监控器 src/protocol/nw_protocol/nw_2021/*
          与 Python 参考 BPLCMonitorPython_SG 的 MPDU_Class.py / MSDU_Class.py。

    与国网 GW_2022 (packet-gw_2022.lua) 的主要差异:
      - FCH byte0: 帧类型(3b) + 接入指示ConInd(1b) + 短网络标识SNID(4b),
        非国网的 DT(3b)+网络类型(5b)+NID(24b)
      - 各帧型 FCH 可变区域字段坐标不同(信标含信标周期计数BPC, SOF 的 PB数/TMI 在字节7)
      - ACK 帧按扩展帧类型细分(常规/网络搜索/同步/无线切频), 非国网 SACK
      - SOF 物理块 = PB头(4B) + 块体(PBSize-8) + 保留(1B) + CRC24(3B),
        非国网的 头(1B) + 体(PBSize-4) + CRC24(3B); 多块时块体需重组出 MAC 帧
      - MAC 帧头 32B长/12B短(MACHeadFlag 决定), MSDU 帧头 = 目的/源MAC(48b)+VLAN(32b)+MSDU类型,
        非国网的 16B 帧头 + MSDU类型单字节
      - 管理消息 MMe: 头 6B(MMVersion+MMType16b+RSV24b), 非国网的 4B MMTYPE
      - FCCS(CRC24) / BPCS(CRC32) / PB CRC24 / MSDU ICV(CRC32) 算法与国网一致

    ★ 字节序约定 (关键, 与 GW_2022 相同):
      - 字节内 bit0 = LSB
      - 跨字节 bit 字段(12bit TEI 等): little-endian bit 序, 用 read_bits()
      - 整字节多字节字段(16/24/32bit): little-endian, 用 le_uint()
      - 时间戳(BTS/基准NTB)=NTB值, 25MHz时钟, 40ns/tick, 约171.8s回绕

    覆盖:
      - MPDU 帧控制 16B (FCH: 帧类型/ConInd/SNID)         [完整]
      - 信标/SOF/ACK(4种扩展)/网间协调 可变区域             [完整]
      - 信标帧载荷: 固定头6B + 管理信息条目 + BPCS + 保留   [完整]
      - SOF 物理块(多块重组) + MAC帧(长/短头/单跳) + MSDU   [完整]
      - MMe 管理消息 17 种 MMType                          [完整]
      - APP 应用层报文(通道控制信息+业务报文头+BID释义)     [完整]
      - FCCS / PB CRC24 / BPCS / MSDU ICV 校验             [完整:原始值+计算值+校验通过标志]

    使用:
      tshark -r capture.pcap -X lua_script:packet-nw_2021.lua -V
      (pcap linktype: USER2=149 载波 / USER3=150 无线, 由 bin2pcap.py --nw 生成)
]]

-- Wireshark Lua 位运算: 标准版有 bit 库, 但为兼容所有环境(含定制版无 bit 库),
-- 全部用纯算术实现, 不依赖 bit/bit32/原生 & 运算符.
local function band(a, b)  -- 按位与
    local r, p = 0, 1
    while a > 0 and b > 0 do
        if a % 2 == 1 and b % 2 == 1 then r = r + p end
        a, b, p = math.floor(a / 2), math.floor(b / 2), p * 2
    end
    return r
end

-- 按位异或 (纯算术, 不依赖 bit 库)
local function bxor(a, b)
    local r, p = 0, 1
    while a > 0 or b > 0 do
        local ba, bb = a % 2, b % 2
        if ba ~= bb then r = r + p end
        a, b, p = math.floor(a / 2), math.floor(b / 2), p * 2
    end
    return r
end

-- CRC24 (poly=0xC60001, init=0, LSB-first, 校验前 len-3 字节, 结果末 3B 小端)
-- 与 Qt fieldspec.h crc24_lsb / Python MPDU_Class.cal_crc24 一致 (GW_2022/NW_2021 同算法)
local crc24_table = {}
for v = 0, 255 do
    local crc = v
    for _ = 1, 8 do
        if band(crc, 1) == 1 then
            crc = bxor(math.floor(crc / 2), 0xC60001)
        else
            crc = math.floor(crc / 2)
        end
    end
    crc24_table[v] = band(crc, 0xFFFFFF)
end

local function crc24_lsb(tvb, off, len)
    local crc = 0
    for i = 0, len - 3 - 1 do
        local idx = band(bxor(crc, tvb(off + i, 1):uint()), 0xFF)
        crc = bxor(math.floor(crc / 256), crc24_table[idx])
    end
    return band(crc, 0xFFFFFF)
end

-- CRC32 (poly=0xEDB88320, init=0xFFFFFFFF, LSB-first, 末取反, 校验前 len-4 字节)
-- 与 Qt fieldspec.h crc32_le / Python cal_crc32 一致 (MSDU ICV/信标 BPCS 同算法)
local crc32_table = {}
for v = 0, 255 do
    local crc = v
    for _ = 1, 8 do
        if band(crc, 1) == 1 then
            crc = bxor(math.floor(crc / 2), 0xEDB88320)
        else
            crc = math.floor(crc / 2)
        end
    end
    crc32_table[v] = crc
end

local function crc32_le(tvb, off, len)
    local crc = 0xFFFFFFFF
    for i = 0, len - 4 - 1 do
        local idx = band(bxor(crc, tvb(off + i, 1):uint()), 0xFF)
        crc = bxor(math.floor(crc / 256), crc32_table[idx])
    end
    return bxor(crc, 0xFFFFFFFF)  -- 末取反
end

-- 语言开关: 与 GW_2022 相同策略 (界面语言 → %APPDATA%\Wireshark\language, 环境变量可覆盖)
local function system_is_chinese()
    local lc = os.getenv("LANG") or os.getenv("LANGUAGE") or ""
    if lc:lower():match("^zh") then return true end
    local h = io.popen('reg query "HKCU\\Control Panel\\International" /v LocaleName 2>nul')
    if h then
        local out = h:read("*a")
        h:close()
        if out:lower():match("zh%-") then return true end
    end
    return false
end

local function detect_english()
    -- 1) 环境变量覆盖 (NW_2021_LANG=en/zh)
    local env = os.getenv("NW_2021_LANG")
    if env == "en" then return true end
    if env == "zh" then return false end
    -- 2) 读 Wireshark language 文件
    local lang_file = os.getenv("APPDATA") .. "\\Wireshark\\language"
    local f = io.open(lang_file, "r")
    if f then
        local content = f:read("*a")
        f:close()
        local lang = content:match("language:%s*(%S+)")
        if lang then
            if lang:lower():match("^zh") then return false end
            if lang:lower():match("^en") then return true end
            return not system_is_chinese()
        end
    end
    -- 3) 回退: 系统语言
    return not system_is_chinese()
end

local nw_english = detect_english()

-- 双语显示名: 按界面语言返回中文或英文
local function T(zh, en)
    if nw_english then return en end
    return zh
end

local nw = Proto("nw_2021", T("南网NW_2021 双模通信数据链路层协议", "NW_2021 Dual-Mode Data Link Layer Protocol"))

-- =========================================================================
-- TEI ↔ MAC 映射表 (按 SNID 分组)
-- 从历史帧学习: 关联确认/关联指示/关联汇总/发现列表/信标站点能力条目 都携带 TEI+MAC
-- 之后在 Source/Destination 列附加显示 MAC 地址
-- =========================================================================
local tei_mac_map = {}   -- tei_mac_map[snid][tei] = "xx:xx:xx:xx:xx:xx"
local cur_snid = 0       -- 当前解析帧的 SNID (主 dissector 设置)

-- 6 字节 MAC → "xx:xx:xx:xx:xx:xx"
local function mac_to_str(tvb, off)
    if off + 6 > tvb:len() then return nil end
    return string.format("%02x:%02x:%02x:%02x:%02x:%02x",
        tvb(off,1):uint(), tvb(off+1,1):uint(), tvb(off+2,1):uint(),
        tvb(off+3,1):uint(), tvb(off+4,1):uint(), tvb(off+5,1):uint())
end

-- 记录 TEI → MAC (TEI=0 未分配 / 0xFFF 广播不记录)
local function record_tei_mac(tei, mac_str)
    if not tei or tei == 0 or tei == 0xFFF or not mac_str then return end
    if not tei_mac_map[cur_snid] then tei_mac_map[cur_snid] = {} end
    tei_mac_map[cur_snid][tei] = mac_str
end

-- 查询 TEI → MAC, 无则返回 nil
local function lookup_tei_mac(snid, tei)
    local m = tei_mac_map[snid]
    if not m then return nil end
    return m[tei]
end

-- =========================================================================
-- value_string 表
-- =========================================================================
local dt_vals = {
    [0] = T("信标帧", "Beacon"), [1] = T("SOF 数据帧", "SOF"),
    [2] = T("ACK 确认帧", "ACK"), [3] = T("网间协调帧", "Coordination"),
}
local ack_ext_type_vals = {
    [0] = T("常规 ACK", "Normal ACK"), [1] = T("网络搜索帧", "Network Search"),
    [2] = T("同步帧", "Sync"), [3] = T("无线切频帧", "RF Channel Switch"),
    [10] = T("时隙预约(南网扩展)", "Slot Reservation"), [11] = T("测距响应(南网扩展)", "Ranging Response"),
    [12] = T("测距请求(南网扩展)", "Ranging Request"),
}
local beacon_type_vals = {
    [0] = T("发现信标", "Discovery Beacon"), [1] = T("代理信标", "Proxy Beacon"), [2] = T("中央信标", "Central Beacon"),
}
-- 相线: 信标可变区域/关联请求用 (0=未知); 站点能力/CSMA时隙用 (0=全相线)
local phase_unk_vals = {
    [0] = T("未知", "Unknown"), [1] = T("A相线", "Line A"), [2] = T("B相线", "Line B"), [3] = T("C相线", "Line C"),
}
local phase_all_vals = {
    [0] = T("全相线", "All lines"), [1] = T("A相线", "Line A"), [2] = T("B相线", "Line B"), [3] = T("C相线", "Line C"),
}
local role_vals = {
    [0] = T("未知", "Unknown"), [1] = T("站点 STA", "STA"), [2] = T("代理站点 PCO", "PCO"), [4] = T("中央协调器 CCO", "CCO"),
}
local beacon_entry_type_vals = {
    [0x01] = T("站点能力条目", "STA Capability Item"), [0x02] = T("时隙分配条目", "Time Slot Allocation Item"),
    [0x06] = T("路由参数条目", "Route Parameter Item"), [0x07] = T("频段变更条目", "Band Change Item"),
    [0x0A] = T("频段探测条目", "Band Probe Item"), [0x0B] = T("万年历同步条目", "Calendar Sync Item"),
}
local ncb_type_vals = { [0] = T("发现信标", "Discovery Beacon"), [1] = T("代理信标", "Proxy Beacon") }
local msdu_type_long_vals = {
    [0x88E1] = T("管理消息报文", "Management message"), [0x0001] = T("应用层报文", "Application packet"),
}
local msdu_type_short_vals = { [0x01] = T("应用层报文", "Application packet") }
local mac_version_vals = { [1] = T("标准帧", "Standard"), [2] = T("单跳帧", "Single-hop") }
local mac_head_flag_vals = { [0] = T("长帧头(32B)", "Long head (32B)"), [1] = T("短帧头(12B)", "Short head (12B)") }
local mme_type_vals = {
    [0x0030] = T("关联请求 MMeAssocReq", "MMeAssocReq"),
    [0x0031] = T("关联确认 MMeAssocCnf", "MMeAssocCnf"),
    [0x0032] = T("代理变更请求 MMeChangeProxyReq", "MMeChangeProxyReq"),
    [0x0034] = T("关联指示 MMeAssocInd", "MMeAssocInd"),
    [0x0037] = T("代理变更确认 MMeChangeProxyCnf", "MMeChangeProxyCnf"),
    [0x003A] = T("关联汇总指示 MMeAssocGatherInd", "MMeAssocGatherInd"),
    [0x003B] = T("代理变更确认位图版 MMeChangeProxyBitMapCnf", "MMeChangeProxyBitMapCnf"),
    [0x0049] = T("离线指示 MMeLeaveInd", "MMeLeaveInd"),
    [0x0051] = T("心跳检测 MMeHeartBeatCheck", "MMeHeartBeatCheck"),
    [0x0055] = T("发现列表 MMeDiscoverNodeList", "MMeDiscoverNodeList"),
    [0x005D] = T("延迟离线指示 MMeDelayLeaveInd", "MMeDelayLeaveInd"),
    [0x005E] = T("通信成功率上报 MMeSuccessRateReport", "MMeSuccessRateReport"),
    [0x005F] = T("网络冲突上报 MMeNetworkConflictReport", "MMeNetworkConflictReport"),
    [0x0062] = T("过零NTB采集指示 MMeZeroCrossNTBCollectInd", "MMeZeroCrossNTBCollectInd"),
    [0x0063] = T("过零NTB上报 MMeZeroCrossNTBReport", "MMeZeroCrossNTBReport"),
    [0x0064] = T("网络诊断 MMeNetDiagnose", "MMeNetDiagnose"),
    [0x0070] = T("无线信道冲突上报 MMeRFChannelConflictReport", "MMeRFChannelConflictReport"),
    [0x0083] = T("站点TEI列表请求 MMeStationTEIListRequest", "MMeStationTEIListRequest"),
    [0x0084] = T("站点TEI列表响应 MMeStationTEIListResponse", "MMeStationTEIListResponse"),
    [0x00A0] = T("汇聚数据上报 MMeConvergenceDataReport", "MMeConvergenceDataReport"),
}
-- 关联确认结果 (表53)
local assoc_cnf_result_vals = {
    [0x00] = T("关联请求成功", "Association succeeded"), [0x01] = T("站点不在白名单中", "STA not in whitelist"),
    [0x03] = T("加入站点个数超过上限", "STA count over limit"), [0x04] = T("没有设置白名单列表", "No whitelist configured"),
    [0x05] = T("代理站点个数超过上限", "PCO count over limit"), [0x06] = T("子站点个数超过上限", "Child STA count over limit"),
    [0x08] = T("重复的MAC地址", "Duplicate MAC address"), [0x09] = T("超过拓扑层级", "Topology level exceeded"),
    [0x0A] = T("站点再次关联请求入网成功", "STA re-association succeeded"),
    [0x0B] = T("新站点试图以自己的子站点为代理入网", "New STA uses own child as proxy"),
    [0x0C] = T("组网拓扑中存在环路", "Loop in topology"), [0x0D] = T("CCO端未知原因出错", "CCO unknown error"),
}
-- 关联指示结果 (表58, 0x07=没有回复 / 0x0A=曾经入网再次入网)
local assoc_ind_result_vals = {
    [0x00] = T("关联请求成功", "Association succeeded"), [0x01] = T("站点不在白名单中", "STA not in whitelist"),
    [0x03] = T("加入站点个数超过上限", "STA count over limit"), [0x04] = T("没有设置白名单列表", "No whitelist configured"),
    [0x05] = T("代理站点个数超过上限", "PCO count over limit"), [0x06] = T("子站点个数超过上限", "Child STA count over limit"),
    [0x07] = T("没有回复", "No reply"), [0x08] = T("重复的MAC地址", "Duplicate MAC address"),
    [0x09] = T("超过拓扑层级", "Topology level exceeded"), [0x0A] = T("曾经入网的站点再次入网", "Former STA rejoined"),
    [0x0B] = T("新站点试图以自己的子站点为代理入网", "New STA uses own child as proxy"),
    [0x0C] = T("组网拓扑中存在环路", "Loop in topology"), [0x0D] = T("CCO端未知原因出错", "CCO unknown error"),
}
local last_pkt_vals = { [0] = T("不是最后一个分包", "Not last fragment"), [1] = T("是最后一个分包", "Last fragment") }
local proxy_change_reason_vals = { [1] = T("周期代理变更", "Periodic proxy change"), [2] = T("快速代理变更", "Fast proxy change") }
local proxy_change_result_vals = { [0] = T("变更成功", "Change succeeded") }
local gather_result_vals = { [0] = T("允许加入网络", "Allow join") }
local leave_reason_vals = {
    [0] = T("站点未入网却收到其报文", "STA not joined but packet received"),
    [2] = T("拓扑层级超过上限", "Topology level exceeds limit"), [4] = T("立即离线", "Leave immediately"),
}
local delay_leave_reason_vals = { [3] = T("站点不在最新白名单中", "STA not in latest whitelist") }
local route_type_vals = {
    [0] = T("错误路由类型", "Incorrect route"), [1] = T("同级路由类型", "Same-level route"),
    [2] = T("上级路由类型", "Upper-level route"), [3] = T("代理主路径路由类型", "Proxy main path route"),
    [4] = T("上上级路由类型", "Upper-upper-level route"),
}
local rate_calc_vals = { [0] = T("未完成", "Not finished"), [1] = T("已完成", "Finished") }
local device_type_vals = {
    [1] = T("抄控器", "Reader/CCO device"), [2] = T("集中器通信模块", "Concentrator comm module"),
    [3] = T("单相电表通信模块", "Meter comm module"), [4] = T("中继器", "Repeater"),
    [5] = T("II型采集器", "Type-II collector"), [6] = T("I型采集器", "Type-I collector"),
    [7] = T("三相表通信模块", "3-phase meter comm module"),
}
local mac_addr_type_vals = {
    [0] = T("电能表地址", "Meter address"), [1] = T("模块本身MAC地址", "Module MAC address"), [2] = T("采集器地址", "Collector address"),
}
local proxy_type_vals = { [2] = T("动态代理", "Dynamic proxy") }
local band_support_vals = { [0] = T("频段0和频段1", "Band 0 & 1"), [1] = T("频段0/1/2", "Band 0/1/2") }
local boot_reason_vals = { [0] = T("正常重启", "Normal boot") }
local band_vals = { [0] = "Band 0", [1] = "Band 1", [2] = "Band 2" }
local chip_id_vals = {
    [0] = T("保留", "Reserved"), [1] = "HS", [2] = "ES", [3] = "TC", [4] = "LH",
    [5] = "HT", [6] = "RS", [7] = "SW", [8] = "SC",
}
local collect_mode_vals = { [1] = T("下降沿采集", "Falling edge"), [2] = T("上升沿采集", "Rising edge") }
local app_port_vals = { [0x11] = T("业务报文", "Service packet"), [0x13] = T("管理报文", "Management packet") }
local app_packet_id_vals = { [0x0101] = T("CCO-STA 应用层报文", "CCO-STA APP packet") }
local app_packet_type_vals = {
    [0x0] = T("确认/否认", "ACK/NACK"), [0x1] = T("数据转发帧", "Data forward"),
    [0x2] = T("命令帧", "Command"), [0x3] = T("主动上报帧", "Event report"),
    [0x4] = T("抄控器相关协议", "Reader protocol"), [0x5] = T("广播命令帧", "Broadcast command"),
    [0x6] = T("数据订阅路由帧", "Data subscription route"), [0xE] = T("厂家调试", "Vendor debug"),
    [0xF] = T("厂测帧", "Factory frame"),
}

-- 业务标识(BID)释义: 依赖帧类型域(表9), 返回 nil 表示保留/无释义
local function business_id_name(port, ptype, bid)
    if ptype == 0x0 then
        if bid == 0x00 then return T("确认", "ACK") end
        if bid == 0x01 then return T("否认", "NACK") end
    elseif ptype == 0x1 then
        if bid == 0x00 then return T("数据透传至设备", "Forward to device") end
        if bid == 0x01 then return T("数据透传至模块", "Forward to module") end
    elseif ptype == 0x2 then
        if bid == 0x00 then return T("查询终端搜索结果", "Query search result") end
        if bid == 0x01 then return T("下发搜索终端列表", "Distribute search list") end
        if bid == 0x02 then return T("文件传输", "File transfer") end
        if bid == 0x03 then return T("允许/禁止从节点事件", "Enable/disable node event") end
        if bid == 0x04 then return T("从节点重启", "Reboot node") end
        if bid == 0x05 then return T("从节点信息查询", "Query node info") end
        if bid == 0x06 then return T("下发通信地址映射表列表", "Distribute address map") end
        if bid == 0x07 then return T("查询从节点运行状态信息", "Query node running status") end
        if bid == 0x08 then return T("查询从节点信道信息", "Query node channel info") end
        if bid == 0x10 then return T("台区户变关系/相位识别", "Station/phase identification") end
        if bid == 0xF0 then return T("测试帧", "Test frame") end
    elseif ptype == 0x3 then
        if bid == 0x00 then return T("电表事件主动上报", "Meter event report") end
        if bid == 0x01 then return T("停上电事件上报", "Power on/off event report") end
        if bid == 0x02 then
            if port == 0x13 then return T("通信模块事件上报", "Comm module event report") end
            return T("设备事件主动上报", "Device event report")
        end
    elseif ptype == 0x4 then
        if bid == 0x00 then return T("抄控器-CCO协议", "Reader-CCO protocol") end
        if bid == 0x01 then return T("数据透传串口转发", "Forward via UART") end
    elseif ptype == 0x5 then
        if bid == 0x04 then return T("从节点重启", "Reboot node") end
        if bid == 0x05 then return T("从节点信息查询", "Query node info") end
        if bid == 0x07 then return T("查询从节点运行状态信息", "Query node running status") end
        if bid == 0x08 then return T("查询从节点信道信息", "Query node channel info") end
    end
    return nil
end

-- =========================================================================
-- ProtoField 定义 (第一参数 = filter abbr, 统一 nw_2021. 前缀)
-- =========================================================================
local f = {}

-- MPDU 帧控制 (16B)
f.fc_dt = ProtoField.uint8 ("nw_2021.fc.dt", T("帧类型", "Frame Type"), base.DEC, dt_vals, 0x07)
f.fc_conind = ProtoField.bool ("nw_2021.fc.conind", T("接入指示(ConInd)", "Access Indication (ConInd)"), 8, nil, 0x08)
f.fc_snid = ProtoField.uint8 ("nw_2021.fc.snid", T("短网络标识(SNID)", "Short Network ID (SNID)"), base.HEX, nil, 0xF0)
f.fc_vf = ProtoField.bytes ("nw_2021.fc.vf", T("可变区域", "Variant Field"), base.NONE)
f.fc_std_ver = ProtoField.uint8 ("nw_2021.fc.std_ver", T("标准版本号", "Standard Version"), base.DEC, nil, 0xF0)
f.fc_fccs = ProtoField.uint24 ("nw_2021.fc.fccs", T("帧控制校验序列(FCCS,CRC24)", "Frame Control Check Sequence (FCCS, CRC24)"), base.HEX)
f.fc_fccs_calc = ProtoField.uint24 ("nw_2021.fc.fccs_calc", T("FCCS 计算值", "FCCS Calculated"), base.HEX)
f.fc_fccs_ok = ProtoField.bool ("nw_2021.fc.fccs_ok", T("FCCS 校验通过", "FCCS Check Passed"), 8, nil, 0x01)

-- 信标帧可变区域 (字节1-12, 载波/无线同坐标至源TEI)
f.beacon_bts = ProtoField.uint32 ("nw_2021.beacon.bts", T("信标时间戳(BTS,原始NTB)", "Beacon Timestamp (BTS, raw NTB)"), base.DEC)
f.beacon_bts_sec = ProtoField.double ("nw_2021.beacon.bts_sec", T("信标时间戳(秒)", "Beacon Timestamp (seconds)"), base.DEC)
f.beacon_bpc = ProtoField.uint32 ("nw_2021.beacon.bpc", T("信标周期计数(BPC)", "Beacon Period Count (BPC)"), base.DEC)
f.beacon_src_tei = ProtoField.uint16 ("nw_2021.beacon.src_tei", T("源TEI", "Source TEI"), base.DEC)
f.beacon_tmi = ProtoField.uint8 ("nw_2021.beacon.tmi", T("载波映射表索引(TMI)", "Tone Map Index (TMI)"), base.DEC, nil, 0xF0)
f.beacon_symbol_cnt = ProtoField.uint16 ("nw_2021.beacon.symbol_cnt", T("符号数", "Symbol Count"), base.DEC)
f.beacon_phase = ProtoField.uint8 ("nw_2021.beacon.phase", T("信标相线", "Beacon Line"), base.DEC, phase_unk_vals, 0x0C)
f.beacon_pblen = ProtoField.uint8 ("nw_2021.beacon.pblen", T("载荷PB大小(无线)", "Payload PB Size (RF)"), base.DEC, nil, 0x0F)

-- SOF 帧可变区域 (字节1-12)
f.sof_src_tei = ProtoField.uint16 ("nw_2021.sof.src_tei", T("源TEI", "Source TEI"), base.DEC)
f.sof_dst_tei = ProtoField.uint16 ("nw_2021.sof.dst_tei", T("目的TEI", "Destination TEI"), base.DEC)
f.sof_lid = ProtoField.uint8 ("nw_2021.sof.lid", T("链路标识符(LID)", "Link Identifier (LID)"), base.DEC)
f.sof_pb_num = ProtoField.uint8 ("nw_2021.sof.pb_num", T("物理块个数(载波)", "PB Count (PLC)"), base.DEC, nil, 0x0F)
f.sof_tmi = ProtoField.uint8 ("nw_2021.sof.tmi", T("载波映射表索引(TMI,载波)", "Tone Map Index (TMI, PLC)"), base.DEC, nil, 0xF0)
f.sof_frame_len = ProtoField.uint16 ("nw_2021.sof.frame_len", T("帧长", "Frame Length"), base.DEC)
f.sof_tmi_ext = ProtoField.uint8 ("nw_2021.sof.tmi_ext", T("扩展TMI(TMI_EXT)", "Extended TMI"), base.DEC, nil, 0x0F)
f.sof_pblen = ProtoField.uint8 ("nw_2021.sof.pblen", T("载荷PB大小(无线)", "Payload PB Size (RF)"), base.DEC, nil, 0xF0)

-- ACK 帧可变区域 (按扩展帧类型, 字节12 低4bit)
f.ack_ext_type = ProtoField.uint8 ("nw_2021.ack.ext_type", T("扩展帧类型", "Extended Frame Type"), base.DEC, ack_ext_type_vals, 0x0F)
f.ack_rx_res = ProtoField.uint8 ("nw_2021.ack.rx_res", T("接收结果", "Receive Result"), base.DEC, nil, 0x0F)
f.ack_rx_status = ProtoField.uint8 ("nw_2021.ack.rx_status", T("接收状态", "Receive Status"), base.HEX, nil, 0xF0)
f.ack_dst_tei = ProtoField.uint16 ("nw_2021.ack.dst_tei", T("目的TEI", "Destination TEI"), base.DEC)
f.ack_rx_pb_num = ProtoField.uint8 ("nw_2021.ack.rx_pb_num", T("接收物理块个数", "Received PB Count"), base.DEC, nil, 0xF0)
f.ack_dst_mac = ProtoField.ether ("nw_2021.ack.dst_mac", T("目的地址(MAC)", "Destination Address (MAC)"))
f.ack_search_tei = ProtoField.uint16 ("nw_2021.ack.search_tei", T("网络搜索站点TEI", "Search STA TEI"), base.DEC)
f.ack_sync_ts = ProtoField.uint32 ("nw_2021.ack.sync_ts", T("同步时间戳(NTB)", "Sync Timestamp (NTB)"), base.DEC)
f.ack_sync_tei = ProtoField.uint16 ("nw_2021.ack.sync_tei", T("同步站点TEI", "Sync STA TEI"), base.DEC)
f.ack_chan_qual = ProtoField.uint8 ("nw_2021.ack.chan_qual", T("信道质量", "Channel Quality"), base.DEC)
f.ack_sta_load = ProtoField.uint8 ("nw_2021.ack.sta_load", T("站点负载(缓存报文数)", "STA Load (buffered packets)"), base.DEC)

-- 网间协调帧可变区域 (字节1-12)
f.coord_nbr_bmp = ProtoField.uint16 ("nw_2021.coord.nbr_bmp", T("邻居网络号位图", "Neighbor Network ID Bitmap"), base.HEX)
f.coord_rf_ch = ProtoField.uint8 ("nw_2021.coord.rf_ch", T("本网络无线信道编号", "RF Channel Number"), base.DEC)
f.coord_rsv1 = ProtoField.uint16 ("nw_2021.coord.rsv1", T("保留", "Reserved"), base.HEX, nil, 0x03FF)
f.coord_duration = ProtoField.uint16 ("nw_2021.coord.duration", T("持续时间(×40ms)", "Duration (×40ms)"), base.DEC)
f.coord_rsv2 = ProtoField.uint8 ("nw_2021.coord.rsv2", T("保留", "Reserved"), base.HEX, nil, 0x01)
f.coord_band_end_flag = ProtoField.uint8 ("nw_2021.coord.band_end_flag", T("带宽截止标志", "Band End Flag"), base.DEC, {[0]=T("无截止","None"),[1]=T("有截止","Ended")}, 0x02)
f.coord_option = ProtoField.uint8 ("nw_2021.coord.option", T("option", "option"), base.DEC, nil, 0x0C)
f.coord_rsv3 = ProtoField.uint8 ("nw_2021.coord.rsv3", T("保留", "Reserved"), base.HEX, nil, 0xF0)
f.coord_band_end_offset = ProtoField.uint16 ("nw_2021.coord.band_end_offset", T("带宽截止偏移(×4ms)", "Band End Offset (×4ms)"), base.DEC)
f.coord_band_start_offset = ProtoField.uint16 ("nw_2021.coord.band_start_offset", T("带宽开始偏移(×4ms)", "Band Start Offset (×4ms)"), base.DEC)
f.coord_rsv0 = ProtoField.uint8 ("nw_2021.coord.rsv0", T("保留", "Reserved"), base.HEX, nil, 0x0F)

-- SOF 物理块 (南网: PB头4B + 块体(PBSize-8) + 保留1B + CRC24 3B)
f.pb_hdr = ProtoField.bytes ("nw_2021.pb.hdr", T("物理块头(PB头)", "PB Header"), base.NONE)
f.pb_size = ProtoField.uint16 ("nw_2021.pb.size", T("物理块大小(字节)", "PB Size (bytes)"), base.DEC)
f.pb_body = ProtoField.bytes ("nw_2021.pb.body", T("物理块体(PB Body)", "PB Body"), base.NONE)
f.pb_rsv = ProtoField.uint8 ("nw_2021.pb.rsv", T("保留", "Reserved"), base.HEX)
f.pb_pbcs = ProtoField.uint24 ("nw_2021.pb.pbcs", T("物理块检查序列(PBCS,CRC24)", "PB Check Sequence (PBCS, CRC24)"), base.HEX)
f.pb_crc_ok = ProtoField.bool ("nw_2021.pb.crc_ok", T("CRC24校验通过", "CRC24 Check Passed"), 8, nil, 0x01)
f.pb_crc_calc = ProtoField.uint24 ("nw_2021.pb.crc_calc", T("CRC24计算值", "CRC24 Calculated"), base.HEX)
f.pb_padding = ProtoField.bytes ("nw_2021.pb.padding", T("填充(Padding)", "Padding"), base.NONE)

-- 信标帧载荷固定头 (6B)
f.beacon_type = ProtoField.uint8 ("nw_2021.beacon.type", T("信标类型", "Beacon Type"), base.DEC, beacon_type_vals, 0x07)
f.beacon_net_cplt = ProtoField.bool ("nw_2021.beacon.net_cplt", T("组网标志位", "Networking Flag"), 8, nil, 0x08)
f.beacon_multi_net = ProtoField.bool ("nw_2021.beacon.multi_net", T("多网络优选功能标志", "Multi-Network Choose Flag"), 8, nil, 0x20)
f.beacon_start_assoc = ProtoField.bool ("nw_2021.beacon.start_assoc", T("开始关联标志", "Start Association Flag"), 8, nil, 0x40)
f.beacon_net_seq = ProtoField.uint8 ("nw_2021.beacon.net_seq", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.beacon_snid = ProtoField.uint8 ("nw_2021.beacon.snid", T("短网络标识(信标)", "Short Network ID (Beacon)"), base.HEX, nil, 0x0F)
f.beacon_head_rsv = ProtoField.bytes ("nw_2021.beacon.head_rsv", T("保留", "Reserved"), base.NONE)

-- 信标管理信息
f.beacon_entry_cnt = ProtoField.uint8 ("nw_2021.beacon.entry_cnt", T("信标条目数", "Beacon Item Count"), base.DEC)
f.beacon_ent_type = ProtoField.uint8 ("nw_2021.beacon.ent.type", T("信标条目头", "Beacon Item Head"), base.HEX, beacon_entry_type_vals)
f.beacon_ent_len = ProtoField.uint16 ("nw_2021.beacon.ent.len", T("信标条目长度", "Beacon Item Length"), base.DEC)
f.beacon_ent_data = ProtoField.bytes ("nw_2021.beacon.ent.data", T("信标条目内容", "Beacon Item Content"), base.NONE)
f.beacon_bpcs = ProtoField.uint32 ("nw_2021.beacon.bpcs", T("帧载荷校验序列(BPCS,CRC32)", "Beacon Payload Check Sequence (BPCS, CRC32)"), base.HEX)
f.beacon_bpcs_calc = ProtoField.uint32 ("nw_2021.beacon.bpcs_calc", T("BPCS 计算值", "BPCS Calculated"), base.HEX)
f.beacon_bpcs_ok = ProtoField.bool ("nw_2021.beacon.bpcs_ok", T("BPCS 校验通过", "BPCS Check Passed"), 8, nil, 0x01)
f.beacon_bpcs_rsv = ProtoField.uint8 ("nw_2021.beacon.bpcs_rsv", T("保留字节", "Reserved Byte"), base.HEX)

-- 站点能力条目 (0x01, 内容 20B)
f.ent_net_level = ProtoField.uint8 ("nw_2021.beacon.cap.net_level", T("层级数", "Level"), base.DEC, nil, 0x3F)
f.ent_sta_line = ProtoField.uint8 ("nw_2021.beacon.cap.sta_line", T("站点相线", "STA Line"), base.DEC, phase_all_vals, 0xC0)
f.ent_tei = ProtoField.uint16 ("nw_2021.beacon.cap.tei", T("TEI", "TEI"), base.DEC)
f.ent_role = ProtoField.uint8 ("nw_2021.beacon.cap.role", T("角色", "Role"), base.DEC, role_vals, 0xF0)
f.ent_beacon_usage = ProtoField.uint8 ("nw_2021.beacon.cap.beacon_usage", T("信标使用标志", "Beacon Usage Flag"), base.DEC, {[0]=T("不使用信标评估信道","Not used for estimation"),[1]=T("使用信标评估信道","Used for estimation")})
f.ent_sta_mac = ProtoField.ether ("nw_2021.beacon.cap.sta_mac", T("发送信标站点MAC", "Beacon TX STA MAC"), base.NONE)
f.ent_pco_tei = ProtoField.uint16 ("nw_2021.beacon.cap.pco_tei", T("代理站点TEI", "Proxy TEI"), base.DEC)
f.ent_path_rate = ProtoField.uint32 ("nw_2021.beacon.cap.path_rate", T("路径最低通信成功率(%)", "Path Min Comm Rate (%)"), base.DEC)
f.ent_cap_rsv = ProtoField.bytes ("nw_2021.beacon.cap.rsv", T("保留", "Reserved"), base.NONE)

-- 时隙分配条目 (0x02, 固定头 24B + 可变长区)
f.ent_nc_beacon_cnt = ProtoField.uint8 ("nw_2021.beacon.slot.nc_beacon_cnt", T("非中央信标时隙总数", "Non-CCO Beacon Slot Count"), base.DEC)
f.ent_c_beacon_cnt = ProtoField.uint8 ("nw_2021.beacon.slot.c_beacon_cnt", T("中央信标时隙总数", "CCO Beacon Slot Count"), base.DEC)
f.ent_csma_line_cnt = ProtoField.uint8 ("nw_2021.beacon.slot.csma_line_cnt", T("CSMA时隙相线个数", "CSMA Slot Line Count"), base.DEC)
f.ent_proxy_beacon_cnt = ProtoField.uint8 ("nw_2021.beacon.slot.proxy_beacon_cnt", T("代理信标时隙总数", "Proxy Beacon Slot Count"), base.DEC)
f.ent_beacon_slot_len = ProtoField.uint16 ("nw_2021.beacon.slot.beacon_slot_len", T("信标时隙长度(×100us)", "Beacon Slot Length (×100us)"), base.DEC)
f.ent_csma_slot_size = ProtoField.uint8 ("nw_2021.beacon.slot.csma_slot_size", T("CSMA时隙大小(×10ms)", "CSMA Slot Size (×10ms)"), base.DEC)
f.ent_bcsma_line_cnt = ProtoField.uint8 ("nw_2021.beacon.slot.bcsma_line_cnt", T("绑定CSMA相线个数", "Binding CSMA Line Count"), base.DEC)
f.ent_bcsma_lid = ProtoField.uint8 ("nw_2021.beacon.slot.bcsma_lid", T("绑定CSMA链路标识符", "Binding CSMA Link ID"), base.DEC)
f.ent_tdma_slot_len = ProtoField.uint16 ("nw_2021.beacon.slot.tdma_slot_len", T("TDMA时隙长度(×100us)", "TDMA Slot Length (×100us)"), base.DEC)
f.ent_tdma_lid = ProtoField.uint8 ("nw_2021.beacon.slot.tdma_lid", T("TDMA链路标识符", "TDMA Link ID"), base.DEC)
f.ent_bp_start_ntb = ProtoField.uint32 ("nw_2021.beacon.slot.bp_start_ntb", T("信标周期起始网络基准时", "Beacon Period Start NTB"), base.HEX)
f.ent_bp_len = ProtoField.uint32 ("nw_2021.beacon.slot.bp_len", T("信标周期长度(×100us)", "Beacon Period Length (×100us)"), base.DEC)
f.ent_slot_rsv = ProtoField.uint32 ("nw_2021.beacon.slot.rsv", T("保留", "Reserved"), base.HEX)
-- 非中央信标信息 (2B/条)
f.ent_ncb_tei = ProtoField.uint16 ("nw_2021.beacon.ncb.tei", T("TEI", "TEI"), base.DEC)
f.ent_ncb_type = ProtoField.uint8 ("nw_2021.beacon.ncb.type", T("信标类型", "Beacon Type"), base.DEC, ncb_type_vals, 0x10)
f.ent_ncb_rsv = ProtoField.uint8 ("nw_2021.beacon.ncb.rsv", T("保留", "Reserved"), base.HEX, nil, 0xE0)
-- CSMA/绑定CSMA 时隙信息 (4B/条)
f.ent_csma_len = ProtoField.uint24 ("nw_2021.beacon.csma.len", T("CSMA时隙长度(×100us)", "CSMA Slot Length (×100us)"), base.DEC)
f.ent_csma_phase = ProtoField.uint8 ("nw_2021.beacon.csma.phase", T("CSMA时隙相线", "CSMA Slot Line"), base.DEC, phase_all_vals)
f.ent_bcsma_len = ProtoField.uint24 ("nw_2021.beacon.bcsma.len", T("绑定CSMA时隙长度(×100us)", "Binding CSMA Slot Length (×100us)"), base.DEC)
f.ent_bcsma_phase = ProtoField.uint8 ("nw_2021.beacon.bcsma.phase", T("绑定CSMA时隙相线", "Binding CSMA Slot Line"), base.DEC, phase_all_vals)

-- 路由参数条目 (0x06, 内容 32B)
f.ent_route_period = ProtoField.uint16 ("nw_2021.beacon.rp.route_period", T("路由周期(秒)", "Route Period (s)"), base.DEC)
f.ent_rp_rsv0 = ProtoField.uint16 ("nw_2021.beacon.rp.rsv0", T("保留", "Reserved"), base.HEX)
f.ent_route_remain = ProtoField.uint16 ("nw_2021.beacon.rp.route_remain", T("路由评估剩余时间(秒)", "Route Estimate Remaining (s)"), base.DEC)
f.ent_rp_rsv1 = ProtoField.bytes ("nw_2021.beacon.rp.rsv1", T("保留", "Reserved"), base.NONE)
f.ent_rp_cco_mac = ProtoField.ether ("nw_2021.beacon.rp.cco_mac", T("CCO MAC地址", "CCO MAC Address"))

-- 频段变更条目 (0x07, 内容 5B)
f.ent_target_band = ProtoField.uint8 ("nw_2021.beacon.bc.target_band", T("目标频段", "Target Band"), base.DEC, band_vals)
f.ent_band_remain = ProtoField.uint32 ("nw_2021.beacon.bc.band_remain", T("频段切换剩余时间(ms)", "Band Switch Remaining (ms)"), base.DEC)

-- 万年历同步条目 (0x0B, 内容 8B)
f.ent_cal_time = ProtoField.uint32 ("nw_2021.beacon.cal.time", T("CCO万年历(秒,自2000-01-01)", "CCO Calendar (s, since 2000-01-01)"), base.DEC)
f.ent_cal_ntb = ProtoField.uint32 ("nw_2021.beacon.cal.ntb", T("CCO万年历NTB(40ns/tick)", "CCO Calendar NTB (40ns/tick)"), base.DEC)

-- MAC 帧头 (标准帧 MSDU_BASE: 32B长/12B短; 单跳帧 MSDU_BASE_S: 4B)
f.mac_version = ProtoField.uint8 ("nw_2021.mac.version", T("MAC帧版本", "MAC Frame Version"), base.DEC, mac_version_vals, 0x06)
f.mac_head_flag = ProtoField.uint8 ("nw_2021.mac.head_flag", T("MAC帧头标志", "MAC Head Flag"), base.DEC, mac_head_flag_vals, 0x01)
f.mac_msdu_len = ProtoField.uint16 ("nw_2021.mac.msdu_len", T("MSDU长度", "MSDU Length"), base.DEC)
f.mac_dst_tei = ProtoField.uint16 ("nw_2021.mac.dst_tei", T("目的TEI", "Destination TEI"), base.DEC)
f.mac_src_tei = ProtoField.uint16 ("nw_2021.mac.src_tei", T("源TEI", "Source TEI"), base.DEC)
f.mac_restart_cnt = ProtoField.uint8 ("nw_2021.mac.restart_cnt", T("重启次数", "Restart Count"), base.DEC, nil, 0xF0)
f.mac_bcast_dir = ProtoField.uint8 ("nw_2021.mac.bcast_dir", T("广播方向", "Broadcast Direction"), base.DEC, nil, 0xF0)
f.mac_send_type = ProtoField.uint8 ("nw_2021.mac.send_type", T("发送类型", "Send Type"), base.DEC, nil, 0x07)
f.mac_msdu_seq = ProtoField.uint16 ("nw_2021.mac.msdu_seq", T("MSDU序列号", "MSDU Sequence Number"), base.DEC)
f.mac_hdr_rsv = ProtoField.bytes ("nw_2021.mac.hdr_rsv", T("保留", "Reserved"), base.NONE)
-- MSDU 帧头 (长 18B: MAC48b×2 + VLAN32b + 类型16b; 短 2B: VLAN8b + 类型8b)
f.msdu_dst_mac = ProtoField.ether ("nw_2021.msdu.dst_mac", T("原始目的MAC地址", "Original Destination MAC"))
f.msdu_src_mac = ProtoField.ether ("nw_2021.msdu.src_mac", T("原始源MAC地址", "Original Source MAC"))
f.msdu_vlan = ProtoField.uint32 ("nw_2021.msdu.vlan", T("VLAN标签(0x8100=管理消息)", "VLAN Tag (0x8100=Mgmt)"), base.HEX)
f.msdu_type = ProtoField.uint16 ("nw_2021.msdu.type", T("MSDU类型", "MSDU Type"), base.HEX, msdu_type_long_vals)
f.msdu_vlan_s = ProtoField.uint8 ("nw_2021.msdu.vlan_s", T("VLAN标签(短帧头)", "VLAN Tag (short head)"), base.HEX)
f.msdu_type_s = ProtoField.uint8 ("nw_2021.msdu.type_s", T("MSDU类型(短帧头)", "MSDU Type (short head)"), base.HEX, msdu_type_short_vals)
-- 单跳帧头 (4B)
f.sh_type = ProtoField.uint8 ("nw_2021.sh.msg_type", T("消息类型", "Message Type"), base.DEC)
f.sh_len = ProtoField.uint16 ("nw_2021.sh.msdu_len", T("MSDU长度", "MSDU Length"), base.DEC)
f.sh_data = ProtoField.bytes ("nw_2021.sh.data", T("MSDU载荷", "MSDU Payload"), base.NONE)
-- MSDU 帧尾 ICV (CRC32)
f.mac_icv = ProtoField.uint32 ("nw_2021.mac.icv", T("完整性校验值(ICV,CRC32)", "Integrity Check Value (ICV, CRC32)"), base.HEX)
f.mac_icv_calc = ProtoField.uint32 ("nw_2021.mac.icv_calc", T("ICV 计算值", "ICV Calculated"), base.HEX)
f.mac_icv_ok = ProtoField.bool ("nw_2021.mac.icv_ok", T("ICV 校验通过", "ICV Check Passed"), 8, nil, 0x01)

-- MMe 管理消息头 (6B)
f.mme_version = ProtoField.uint8 ("nw_2021.mme.version", T("管理消息版本", "Management Message Version"), base.HEX)
f.mme_type = ProtoField.uint16 ("nw_2021.mme.type", T("管理消息类型(MMType)", "Management Message Type (MMType)"), base.HEX, mme_type_vals)
f.mme_rsv = ProtoField.bytes ("nw_2021.mme.rsv", T("保留", "Reserved"), base.NONE)

-- 关联请求 MMeAssocReq (0x0030)
f.ar_sta_mac = ProtoField.ether ("nw_2021.assoc_req.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ar_proxy_tei = ProtoField.uint16 ("nw_2021.assoc_req.proxy_tei", T("候选代理TEI", "Candidate Proxy TEI"), base.DEC)
f.ar_link_type = ProtoField.bool ("nw_2021.assoc_req.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x10)
f.ar_link_rsv = ProtoField.uint8 ("nw_2021.assoc_req.link_rsv", T("保留", "Reserved"), base.HEX, nil, 0xE0)
f.ar_phase = ProtoField.uint8 ("nw_2021.assoc_req.phase", T("相线", "Line"), base.DEC, phase_unk_vals)
f.ar_dev_type = ProtoField.uint8 ("nw_2021.assoc_req.dev_type", T("设备类型", "Device Type"), base.DEC, device_type_vals)
f.ar_rsv = ProtoField.bytes ("nw_2021.assoc_req.rsv", T("保留", "Reserved"), base.NONE)
f.ar_mac_type = ProtoField.uint8 ("nw_2021.assoc_req.mac_type", T("MAC地址类型", "MAC Address Type"), base.DEC, mac_addr_type_vals)
f.ar_module = ProtoField.uint8 ("nw_2021.assoc_req.module", T("模块类型", "Module Type"), base.DEC, nil, 0x03)
f.ar_link = ProtoField.uint8 ("nw_2021.assoc_req.link", T("链路信息", "Link Info"), base.DEC, nil, 0x7C)
f.ar_assoc_rand = ProtoField.uint32 ("nw_2021.assoc_req.assoc_rand", T("站点关联随机数", "Association Random Number"), base.DEC)
f.ar_manuf_info = ProtoField.bytes ("nw_2021.assoc_req.manuf_info", T("厂家自定义信息", "Vendor Custom Info"), base.NONE)
f.ar_boot_reason = ProtoField.uint8 ("nw_2021.assoc_req.boot_reason", T("系统启动原因", "Boot Reason"), base.DEC, boot_reason_vals)
f.ar_boot_ver = ProtoField.uint8 ("nw_2021.assoc_req.boot_ver", T("BOOT版本号", "BOOT Version"), base.DEC)
f.ar_soft_ver = ProtoField.uint16 ("nw_2021.assoc_req.soft_ver", T("软件版本号", "Software Version"), base.DEC)
f.ar_ver_year = ProtoField.uint8 ("nw_2021.assoc_req.ver_year", T("版本时间-年(+2000)", "Version Year (+2000)"), base.DEC, nil, 0x7F)
f.ar_ver_month = ProtoField.uint8 ("nw_2021.assoc_req.ver_month", T("版本时间-月", "Version Month"), base.DEC, nil, 0x78)
f.ar_ver_day = ProtoField.uint8 ("nw_2021.assoc_req.ver_day", T("版本时间-日", "Version Day"), base.DEC, nil, 0xF8)
f.ar_manuf_id = ProtoField.uint16 ("nw_2021.assoc_req.manuf_id", T("厂家代码", "Manufacturer ID"), base.HEX)
f.ar_chip_code = ProtoField.uint16 ("nw_2021.assoc_req.chip_code", T("芯片代码", "Chip Code"), base.HEX)
f.ar_hard_rst = ProtoField.uint16 ("nw_2021.assoc_req.hard_rst", T("硬复位累积次数", "Hard Reset Count"), base.DEC)
f.ar_soft_rst = ProtoField.uint16 ("nw_2021.assoc_req.soft_rst", T("软复位累积次数", "Soft Reset Count"), base.DEC)
f.ar_proxy_type = ProtoField.uint8 ("nw_2021.assoc_req.proxy_type", T("代理类型", "Proxy Type"), base.DEC, proxy_type_vals)
f.ar_net_sn = ProtoField.uint8 ("nw_2021.assoc_req.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.ar_mme_ver = ProtoField.uint8 ("nw_2021.assoc_req.mme_ver", T("MMe版本号", "MMe Version"), base.DEC, nil, 0x1E)
f.ar_band_support = ProtoField.uint8 ("nw_2021.assoc_req.band_support", T("支持频段标识", "Band Support"), base.DEC, band_support_vals, 0x03)
f.ar_e2e_seq = ProtoField.uint32 ("nw_2021.assoc_req.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)

-- 关联确认 MMeAssocCnf (0x0031)
f.ac_sta_mac = ProtoField.ether ("nw_2021.assoc_cnf.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ac_result = ProtoField.uint8 ("nw_2021.assoc_cnf.result", T("结果", "Result"), base.HEX, assoc_cnf_result_vals)
f.ac_sta_level = ProtoField.uint8 ("nw_2021.assoc_cnf.sta_level", T("站点层级", "STA Level"), base.DEC)
f.ac_sta_tei = ProtoField.uint16 ("nw_2021.assoc_cnf.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.ac_proxy_tei = ProtoField.uint16 ("nw_2021.assoc_cnf.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.ac_total_pkgs = ProtoField.uint8 ("nw_2021.assoc_cnf.total_pkgs", T("总分包数", "Total Packages"), base.DEC)
f.ac_pkg_idx = ProtoField.uint8 ("nw_2021.assoc_cnf.pkg_idx", T("分包序号", "Package Index"), base.DEC)
f.ac_last_pkg = ProtoField.uint8 ("nw_2021.assoc_cnf.last_pkg", T("最后一个分包标识", "Last Packet Flag"), base.DEC, last_pkt_vals)
f.ac_link_type = ProtoField.bool ("nw_2021.assoc_cnf.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x01)
f.ac_carrier = ProtoField.uint8 ("nw_2021.assoc_cnf.carrier", T("载波频段", "Carrier Band"), base.DEC, band_vals, 0x06)
f.ac_assoc_rand = ProtoField.uint32 ("nw_2021.assoc_cnf.assoc_rand", T("站点关联随机数", "Association Random Number"), base.DEC)
f.ac_reassoc_time = ProtoField.uint32 ("nw_2021.assoc_cnf.reassoc_time", T("重新关联时间(ms)", "Re-association Time (ms)"), base.DEC)
f.ac_e2e_seq = ProtoField.uint32 ("nw_2021.assoc_cnf.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.ac_path_seq = ProtoField.uint32 ("nw_2021.assoc_cnf.path_seq", T("路径序号", "Path Sequence Number"), base.DEC)
f.ac_net_sn = ProtoField.uint8 ("nw_2021.assoc_cnf.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.ac_mme_ver = ProtoField.uint8 ("nw_2021.assoc_cnf.mme_ver", T("MMe版本号", "MMe Version"), base.DEC, nil, 0x0F)
f.ac_detech = ProtoField.bool ("nw_2021.assoc_cnf.detech", T("detechFlag", "detechFlag"), 8, nil, 0x10)
-- 路由信息 (关联确认/关联指示共用)
f.ri_straight_sta = ProtoField.uint16 ("nw_2021.route_info.straight_sta", T("直连站点数", "Direct STA Count"), base.DEC)
f.ri_straight_pco = ProtoField.uint16 ("nw_2021.route_info.straight_pco", T("直连代理数", "Direct PCO Count"), base.DEC)
f.ri_table_size = ProtoField.uint16 ("nw_2021.route_info.table_size", T("路由表大小", "Route Table Size"), base.DEC)
f.ri_rsv = ProtoField.uint16 ("nw_2021.route_info.rsv", T("保留", "Reserved"), base.HEX)
f.ri_sta_tei = ProtoField.uint16 ("nw_2021.route_info.sta_tei", T("直连站点TEI", "Direct STA TEI"), base.DEC)
f.ri_pco_tei = ProtoField.uint16 ("nw_2021.route_info.pco_tei", T("直连代理TEI", "Direct PCO TEI"), base.DEC)
f.ri_pco_child_cnt = ProtoField.uint16 ("nw_2021.route_info.pco_child_cnt", T("代理子站点数", "PCO Child Count"), base.DEC)
f.ri_child_tei = ProtoField.uint16 ("nw_2021.route_info.child_tei", T("子站点TEI", "Child STA TEI"), base.DEC)
f.ri_link_type = ProtoField.bool ("nw_2021.route_info.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x10)
f.ri_link_rsv = ProtoField.uint8 ("nw_2021.route_info.link_rsv", T("保留", "Reserved"), base.HEX, nil, 0xE0)

-- 代理变更请求 MMeChangeProxyReq (0x0032)
f.cpr_sta_tei = ProtoField.uint16 ("nw_2021.cproxy_req.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.cpr_new_proxy = ProtoField.uint16 ("nw_2021.cproxy_req.new_proxy", T("新代理TEI", "New Proxy TEI"), base.DEC)
f.cpr_link_type = ProtoField.bool ("nw_2021.cproxy_req.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x10)
f.cpr_link_rsv = ProtoField.uint8 ("nw_2021.cproxy_req.link_rsv", T("保留", "Reserved"), base.HEX, nil, 0xE0)
f.cpr_old_proxy = ProtoField.uint16 ("nw_2021.cproxy_req.old_proxy", T("旧代理TEI", "Old Proxy TEI"), base.DEC)
f.cpr_proxy_type = ProtoField.uint8 ("nw_2021.cproxy_req.proxy_type", T("代理类型", "Proxy Type"), base.DEC, proxy_type_vals)
f.cpr_reason = ProtoField.uint8 ("nw_2021.cproxy_req.reason", T("原因", "Reason"), base.DEC, proxy_change_reason_vals)
f.cpr_phase = ProtoField.uint8 ("nw_2021.cproxy_req.phase", T("站点相线", "STA Line"), base.DEC, phase_unk_vals)
f.cpr_link = ProtoField.uint8 ("nw_2021.cproxy_req.link", T("链路信息", "Link Info"), base.DEC, nil, 0x1F)
f.cpr_e2e_seq = ProtoField.uint32 ("nw_2021.cproxy_req.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.cpr_net_sn = ProtoField.uint8 ("nw_2021.cproxy_req.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.cpr_rsv = ProtoField.bytes ("nw_2021.cproxy_req.rsv", T("保留", "Reserved"), base.NONE)

-- 关联指示 MMeAssocInd (0x0034)
f.ai_result = ProtoField.uint8 ("nw_2021.assoc_ind.result", T("结果", "Result"), base.HEX, assoc_ind_result_vals)
f.ai_sta_level = ProtoField.uint8 ("nw_2021.assoc_ind.sta_level", T("站点层级", "STA Level"), base.DEC)
f.ai_sta_mac = ProtoField.ether ("nw_2021.assoc_ind.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ai_cco_mac = ProtoField.ether ("nw_2021.assoc_ind.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.ai_sta_tei = ProtoField.uint16 ("nw_2021.assoc_ind.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.ai_proxy_tei = ProtoField.uint16 ("nw_2021.assoc_ind.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.ai_link_type = ProtoField.bool ("nw_2021.assoc_ind.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x01)
f.ai_carrier = ProtoField.uint8 ("nw_2021.assoc_ind.carrier", T("载波频段", "Carrier Band"), base.DEC, band_vals, 0x06)
f.ai_rsv = ProtoField.bytes ("nw_2021.assoc_ind.rsv", T("保留", "Reserved"), base.NONE)
f.ai_total_pkgs = ProtoField.uint8 ("nw_2021.assoc_ind.total_pkgs", T("总分包数", "Total Packages"), base.DEC)
f.ai_pkg_idx = ProtoField.uint8 ("nw_2021.assoc_ind.pkg_idx", T("分包序号", "Package Index"), base.DEC)
f.ai_last_pkg = ProtoField.uint8 ("nw_2021.assoc_ind.last_pkg", T("最后一个分包标识", "Last Packet Flag"), base.DEC, last_pkt_vals)
f.ai_assoc_rand = ProtoField.uint32 ("nw_2021.assoc_ind.assoc_rand", T("站点关联随机数", "Association Random Number"), base.DEC)
f.ai_net_sn = ProtoField.uint8 ("nw_2021.assoc_ind.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.ai_reassoc_time = ProtoField.uint32 ("nw_2021.assoc_ind.reassoc_time", T("重新关联时间(ms)", "Re-association Time (ms)"), base.DEC)
f.ai_e2e_seq = ProtoField.uint32 ("nw_2021.assoc_ind.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)

-- 代理变更确认 MMeChangeProxyCnf (0x0037)
f.cpc_result = ProtoField.uint32 ("nw_2021.cproxy_cnf.result", T("结果", "Result"), base.DEC, proxy_change_result_vals)
f.cpc_total_pkgs = ProtoField.uint8 ("nw_2021.cproxy_cnf.total_pkgs", T("总分包数", "Total Packages"), base.DEC)
f.cpc_pkg_idx = ProtoField.uint8 ("nw_2021.cproxy_cnf.pkg_idx", T("分包序号", "Package Index"), base.DEC)
f.cpc_sta_tei = ProtoField.uint16 ("nw_2021.cproxy_cnf.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.cpc_proxy_tei = ProtoField.uint16 ("nw_2021.cproxy_cnf.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.cpc_child_cnt = ProtoField.uint16 ("nw_2021.cproxy_cnf.child_cnt", T("子站点数", "Child STA Count"), base.DEC)
f.cpc_net_sn = ProtoField.uint8 ("nw_2021.cproxy_cnf.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.cpc_link_type = ProtoField.bool ("nw_2021.cproxy_cnf.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x01)
f.cpc_rsv = ProtoField.bytes ("nw_2021.cproxy_cnf.rsv", T("保留", "Reserved"), base.NONE)
f.cpc_e2e_seq = ProtoField.uint32 ("nw_2021.cproxy_cnf.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.cpc_path_seq = ProtoField.uint32 ("nw_2021.cproxy_cnf.path_seq", T("路径序号", "Path Sequence Number"), base.DEC)
f.cpc_child_tei = ProtoField.uint16 ("nw_2021.cproxy_cnf.child_tei", T("子站点TEI", "Child STA TEI"), base.DEC)

-- 关联汇总指示 MMeAssocGatherInd (0x003A)
f.ag_result = ProtoField.uint8 ("nw_2021.assoc_gather.result", T("结果", "Result"), base.DEC, gather_result_vals)
f.ag_sta_level = ProtoField.uint8 ("nw_2021.assoc_gather.sta_level", T("站点层级", "STA Level"), base.DEC)
f.ag_cco_mac = ProtoField.ether ("nw_2021.assoc_gather.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.ag_proxy_tei = ProtoField.uint16 ("nw_2021.assoc_gather.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.ag_net_sn = ProtoField.uint8 ("nw_2021.assoc_gather.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.ag_gather_cnt = ProtoField.uint8 ("nw_2021.assoc_gather.gather_cnt", T("新站点数", "New STA Count"), base.DEC)
f.ag_carrier = ProtoField.uint8 ("nw_2021.assoc_gather.carrier", T("载波频段", "Carrier Band"), base.DEC, band_vals, 0x03)
f.ag_rsv = ProtoField.bytes ("nw_2021.assoc_gather.rsv", T("保留", "Reserved"), base.NONE)
f.ag_sta_mac = ProtoField.ether ("nw_2021.assoc_gather.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ag_sta_tei = ProtoField.uint16 ("nw_2021.assoc_gather.sta_tei", T("站点TEI", "STA TEI"), base.DEC)

-- 代理变更确认位图版 MMeChangeProxyBitMapCnf (0x003B)
f.cpb_result = ProtoField.uint32 ("nw_2021.cproxy_bmp.result", T("结果", "Result"), base.DEC, proxy_change_result_vals)
f.cpb_sta_tei = ProtoField.uint16 ("nw_2021.cproxy_bmp.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.cpb_proxy_tei = ProtoField.uint16 ("nw_2021.cproxy_bmp.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.cpb_net_sn = ProtoField.uint8 ("nw_2021.cproxy_bmp.net_sn", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.cpb_child_bmp = ProtoField.bytes ("nw_2021.cproxy_bmp.child_bmp", T("子站点位图(130B)", "Child STA Bitmap (130B)"), base.NONE)
f.cpb_link_type = ProtoField.bool ("nw_2021.cproxy_bmp.link_type", T("链路类型(1=无线)", "Link Type (1=RF)"), 8, nil, 0x01)
f.cpb_e2e_seq = ProtoField.uint32 ("nw_2021.cproxy_bmp.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.cpb_path_seq = ProtoField.uint32 ("nw_2021.cproxy_bmp.path_seq", T("路径序号", "Path Sequence Number"), base.DEC)

-- 离线指示 MMeLeaveInd (0x0049)
f.li_sta_tei = ProtoField.uint16 ("nw_2021.leave.sta_tei", T("离线站点TEI", "Leave STA TEI"), base.DEC)
f.li_reason = ProtoField.uint16 ("nw_2021.leave.reason", T("原因", "Reason"), base.DEC, leave_reason_vals)
f.li_sta_mac = ProtoField.ether ("nw_2021.leave.sta_mac", T("离线站点MAC地址", "Leave STA MAC Address"))
f.li_proxy_tei = ProtoField.uint16 ("nw_2021.leave.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.li_rsv = ProtoField.bytes ("nw_2021.leave.rsv", T("保留", "Reserved"), base.NONE)

-- 心跳检测 MMeHeartBeatCheck (0x0051)
f.hb_ostei = ProtoField.uint16 ("nw_2021.hb.ostei", T("原始源TEI", "Original Source TEI"), base.DEC)
f.hb_max_disc_tei = ProtoField.uint16 ("nw_2021.hb.max_disc_tei", T("发现站点数最大的站点TEI", "Max Discovery STA TEI"), base.DEC)
f.hb_max_disc_cnt = ProtoField.uint32 ("nw_2021.hb.max_disc_cnt", T("最大的发现站点数", "Max Discovery STA Count"), base.DEC)
f.hb_disc_bmp = ProtoField.bytes ("nw_2021.hb.disc_bmp", T("可发现站点位图(130B)", "Discoverable STA Bitmap (130B)"), base.NONE)
f.hb_rsv = ProtoField.uint8 ("nw_2021.hb.rsv", T("保留", "Reserved"), base.HEX)

-- 发现列表 MMeDiscoverNodeList (0x0055)
f.dl_tei = ProtoField.uint16 ("nw_2021.dl.tei", T("TEI", "TEI"), base.DEC)
f.dl_role = ProtoField.uint8 ("nw_2021.dl.role", T("角色", "Role"), base.DEC, role_vals)
f.dl_level = ProtoField.uint8 ("nw_2021.dl.level", T("层级", "Level"), base.DEC)
f.dl_mac = ProtoField.ether ("nw_2021.dl.mac", T("MAC地址", "MAC Address"))
f.dl_proxy_tei = ProtoField.uint16 ("nw_2021.dl.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.dl_rsv = ProtoField.bytes ("nw_2021.dl.rsv", T("保留", "Reserved"), base.NONE)
f.dl_rate_finish = ProtoField.uint8 ("nw_2021.dl.rate_finish", T("通信成功率计算完成标志", "Comm Rate Calc Finish"), base.DEC, rate_calc_vals, 0x80)
f.dl_proxy_rate = ProtoField.uint32 ("nw_2021.dl.proxy_rate", T("代理站点通信成功率(%)", "Proxy Comm Rate (%)"), base.DEC)
f.dl_proxy_dl_rate = ProtoField.uint32 ("nw_2021.dl.proxy_dl_rate", T("代理站点下行通信成功率(%)", "Proxy Downlink Comm Rate (%)"), base.DEC)
f.dl_sta_cnt = ProtoField.uint16 ("nw_2021.dl.sta_cnt", T("发现站点数", "Discovered STA Count"), base.DEC)
f.dl_send_cnt = ProtoField.uint16 ("nw_2021.dl.send_cnt", T("发送发现列表报文个数", "Discovery List TX Count"), base.DEC)
f.dl_up_route_cnt = ProtoField.uint16 ("nw_2021.dl.up_route_cnt", T("上行路由条目数", "Uplink Route Entry Count"), base.DEC)
f.dl_up_route_size = ProtoField.uint8 ("nw_2021.dl.up_route_size", T("上行路由条目大小(bit)", "Uplink Route Entry Size (bit)"), base.DEC)
f.dl_route_remain = ProtoField.uint16 ("nw_2021.dl.route_remain", T("路由周期到期剩余时间(秒)", "Route Period Remaining (s)"), base.DEC)
f.dl_phase0 = ProtoField.uint8 ("nw_2021.dl.phase0", T("相线0(高可信)", "Line 0 (high confidence)"), base.DEC, phase_unk_vals, 0x30)
f.dl_phase1 = ProtoField.uint8 ("nw_2021.dl.phase1", T("相线1(中可信)", "Line 1 (medium confidence)"), base.DEC, phase_unk_vals, 0x0C)
f.dl_phase2 = ProtoField.uint8 ("nw_2021.dl.phase2", T("相线2(低可信)", "Line 2 (low confidence)"), base.DEC, phase_unk_vals, 0x03)
f.dl_min_rate = ProtoField.uint8 ("nw_2021.dl.min_rate", T("最小通信成功率(%)", "Min Comm Rate (%)"), base.DEC)
f.dl_next_hop_tei = ProtoField.uint16 ("nw_2021.dl.next_hop_tei", T("下一跳站点TEI", "Next Hop STA TEI"), base.DEC)
f.dl_route_type = ProtoField.uint8 ("nw_2021.dl.route_type", T("路由类型", "Route Type"), base.DEC, route_type_vals)
f.dl_disc_bmp = ProtoField.bytes ("nw_2021.dl.disc_bmp", T("发现站点列表位图(128B)", "Discovery STA List Bitmap (128B)"), base.NONE)
f.dl_rcv_cnt = ProtoField.uint8 ("nw_2021.dl.rcv_cnt", T("接收发现列表数", "Received Discovery List Count"), base.DEC)
f.dl_rcv_item = ProtoField.string ("nw_2021.dl.rcv_item", T("接收发现列表数(按TEI)", "Received Discovery List Count (by TEI)"))

-- 延迟离线指示 MMeDelayLeaveInd (0x005D)
f.dl2_reason = ProtoField.uint16 ("nw_2021.delay_leave.reason", T("原因", "Reason"), base.DEC, delay_leave_reason_vals)
f.dl2_sta_cnt = ProtoField.uint16 ("nw_2021.delay_leave.sta_cnt", T("站点总数", "STA Count"), base.DEC)
f.dl2_delay = ProtoField.uint16 ("nw_2021.delay_leave.delay", T("延迟时间(秒)", "Delay Time (s)"), base.DEC)
f.dl2_rsv = ProtoField.bytes ("nw_2021.delay_leave.rsv", T("保留", "Reserved"), base.NONE)
f.dl2_sta_mac = ProtoField.ether ("nw_2021.delay_leave.sta_mac", T("站点MAC地址", "STA MAC Address"))

-- 通信成功率上报 MMeSuccessRateReport (0x005E)
f.sr_proxy_tei = ProtoField.uint16 ("nw_2021.sr.proxy_tei", T("代理站点TEI", "Proxy STA TEI"), base.DEC)
f.sr_sta_cnt = ProtoField.uint16 ("nw_2021.sr.sta_cnt", T("站点总数", "STA Count"), base.DEC)
f.sr_sta_tei = ProtoField.uint16 ("nw_2021.sr.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.sr_down_rate = ProtoField.uint8 ("nw_2021.sr.down_rate", T("下行通信成功率(%)", "Downlink Comm Rate (%)"), base.DEC)
f.sr_up_rate = ProtoField.uint8 ("nw_2021.sr.up_rate", T("上行通信成功率(%)", "Uplink Comm Rate (%)"), base.DEC)

-- 过零NTB采集指示 MMeZeroCrossNTBCollectInd (0x0062)
f.zc_quantity = ProtoField.uint8 ("nw_2021.zc.quantity", T("采集数量", "Collect Quantity"), base.DEC)
f.zc_seq = ProtoField.uint8 ("nw_2021.zc.seq", T("采集序列号", "Collect Sequence Number"), base.DEC)
f.zc_rsv = ProtoField.uint16 ("nw_2021.zc.rsv", T("保留", "Reserved"), base.HEX)

-- 过零NTB上报 MMeZeroCrossNTBReport (0x0063)
f.zr_tei = ProtoField.uint16 ("nw_2021.zr.tei", T("TEI", "TEI"), base.DEC)
f.zr_collect_mode = ProtoField.uint8 ("nw_2021.zr.collect_mode", T("相位特征采集方式", "Collect Mode"), base.DEC, collect_mode_vals, 0x30)
f.zr_seq = ProtoField.uint8 ("nw_2021.zr.seq", T("采集序列号", "Collect Sequence Number"), base.DEC)
f.zr_total_cnt = ProtoField.uint8 ("nw_2021.zr.total_cnt", T("告知总数量", "Report Total Count"), base.DEC)
f.zr_base_ntb = ProtoField.uint32 ("nw_2021.zr.base_ntb", T("基准NTB", "Base NTB"), base.DEC)
f.zr_rsv = ProtoField.uint8 ("nw_2021.zr.rsv", T("保留", "Reserved"), base.HEX)
f.zr_ph1_cnt = ProtoField.uint8 ("nw_2021.zr.ph1_cnt", T("相线1差值数量", "Line A Diff Count"), base.DEC)
f.zr_ph2_cnt = ProtoField.uint8 ("nw_2021.zr.ph2_cnt", T("相线2差值数量", "Line B Diff Count"), base.DEC)
f.zr_ph3_cnt = ProtoField.uint8 ("nw_2021.zr.ph3_cnt", T("相线3差值数量", "Line C Diff Count"), base.DEC)
f.zr_diff = ProtoField.uint16 ("nw_2021.zr.diff", T("过零NTB差值", "Zero-Cross NTB Diff"), base.DEC)

-- 网络诊断 MMeNetDiagnose (0x0064)
f.diag_chip_id = ProtoField.uint16 ("nw_2021.diag.chip_id", T("芯片厂商ID", "Chip Vendor ID"), base.DEC, chip_id_vals)
f.diag_custom = ProtoField.bytes ("nw_2021.diag.custom", T("诊断信息", "Diagnose Info"), base.NONE)

-- 无线信道冲突上报 MMeRFChannelConflictReport (0x0070)
f.rfccr_cco_mac = ProtoField.ether ("nw_2021.rfccr.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.rfccr_nbr_cnt = ProtoField.uint8 ("nw_2021.rfccr.nbr_cnt", T("邻居网络个数", "Neighbor Network Count"), base.DEC)
f.rfccr_nbr_ch = ProtoField.uint8 ("nw_2021.rfccr.nbr_ch", T("邻居网络信道号", "Neighbor Network RF Channel"), base.DEC)
f.rfccr_nbr_option = ProtoField.uint8 ("nw_2021.rfccr.nbr_option", T("邻居网络option", "Neighbor Network Option"), base.DEC, nil, 0x03)

-- 网络冲突上报 MMeNetworkConflictReport (0x005F)
f.ncr_cco_mac = ProtoField.ether ("nw_2021.ncr.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.ncr_nbr_cnt = ProtoField.uint8 ("nw_2021.ncr.nbr_cnt", T("邻居网络个数", "Neighbor Network Count"), base.DEC)
f.ncr_nbr_bmp = ProtoField.uint16 ("nw_2021.ncr.nbr_bmp", T("邻居SNID位图", "Neighbor SNID Bitmap"), base.HEX)

-- 应用层报文 APP_BASE (通道控制信息 4B + 业务报文头 8B)
f.app_port = ProtoField.uint8 ("nw_2021.app.port", T("报文端口号", "Port Number"), base.HEX, app_port_vals)
f.app_packet_id = ProtoField.uint16 ("nw_2021.app.packet_id", T("报文标识符", "Packet ID"), base.HEX, app_packet_id_vals)
f.app_rsv = ProtoField.uint8 ("nw_2021.app.rsv", T("保留", "Reserved"), base.HEX)
f.app_packet_type = ProtoField.uint8 ("nw_2021.app.packet_type", T("帧类型域", "Packet Type"), base.DEC, app_packet_type_vals, 0x0F)
f.app_ctrl_rsv0 = ProtoField.uint8 ("nw_2021.app.ctrl_rsv0", T("控制域保留", "Ctrl Reserved"), base.HEX, nil, 0xF0)
f.app_ctrl_rsv1 = ProtoField.uint8 ("nw_2021.app.ctrl_rsv1", T("控制域保留", "Ctrl Reserved"), base.HEX, nil, 0x0F)
f.app_ext_flag = ProtoField.bool ("nw_2021.app.ext_flag", T("业务扩展域标识位", "Extended Field Flag"), 8, nil, 0x10)
f.app_respond_flag = ProtoField.bool ("nw_2021.app.respond_flag", T("响应标识位", "Response Flag"), 8, nil, 0x20)
f.app_start_flag = ProtoField.bool ("nw_2021.app.start_flag", T("启动标志位", "Start Flag"), 8, nil, 0x40)
f.app_trans_dir = ProtoField.bool ("nw_2021.app.trans_dir", T("传输方向位", "Transmission Direction"), 8, nil, 0x80)
f.app_business_id = ProtoField.uint8 ("nw_2021.app.business_id", T("业务标识(BID)", "Business ID (BID)"), base.HEX)
f.app_bid_desc = ProtoField.string ("nw_2021.app.bid_desc", T("业务标识释义", "Business ID Description"))
f.app_version = ProtoField.uint8 ("nw_2021.app.version", T("应用版本号", "Application Version"), base.DEC)
f.app_packet_sn = ProtoField.uint16 ("nw_2021.app.packet_sn", T("帧序号", "Packet SN"), base.DEC)
f.app_packet_len = ProtoField.uint16 ("nw_2021.app.packet_len", T("帧长(字节)", "Packet Length (bytes)"), base.DEC)
f.app_payload = ProtoField.bytes ("nw_2021.app.payload", T("应用层载荷(APP Data)", "Application Payload (APP Data)"), base.NONE)


-- 媒介头 (串口混合采集 USER4/USER5 格式: [phr_mcs][option][channel][isRF], 之后纯 MPDU)
f.media_phr_mcs = ProtoField.uint8 ("nw_2021.media.phr_mcs", T("PHR MCS", "PHR MCS"), base.HEX)
f.media_option = ProtoField.uint8 ("nw_2021.media.option", T("option", "option"), base.HEX)
f.media_channel = ProtoField.uint8 ("nw_2021.media.channel", T("信道", "Channel"), base.DEC)
f.media_is_rf = ProtoField.uint8 ("nw_2021.media.is_rf", T("媒介(0=载波,非0=无线)", "Media (0=PLC, non-0=RF)"), base.DEC, {[0]=T("载波 HPLC", "PLC"), [1]=T("无线 RF", "RF")})

-- 原始目标地址列 (自定义列引用, 显示目的TEI + MAC 映射)
f.col_orig_dst = ProtoField.string ("nw_2021.col_orig_dst", T("原始目标地址", "Original Destination Address"))

-- 位图逐字节解析节点 (string, 承载 "值 (TEI列表)" 文本)
f.bmp_byte = ProtoField.string ("nw_2021.bmp.byte", T("位图字节", "Bitmap Byte"))

nw.fields = {
    f.fc_dt, f.fc_conind, f.fc_snid, f.fc_vf, f.fc_std_ver, f.fc_fccs,
    f.fc_fccs_calc, f.fc_fccs_ok,
    f.beacon_bts, f.beacon_bts_sec, f.beacon_bpc, f.beacon_src_tei,
    f.beacon_tmi, f.beacon_symbol_cnt, f.beacon_phase, f.beacon_pblen,
    f.sof_src_tei, f.sof_dst_tei, f.sof_lid, f.sof_pb_num, f.sof_tmi,
    f.sof_frame_len, f.sof_tmi_ext, f.sof_pblen,
    f.ack_ext_type, f.ack_rx_res, f.ack_rx_status, f.ack_dst_tei, f.ack_rx_pb_num,
    f.ack_dst_mac, f.ack_search_tei, f.ack_sync_ts, f.ack_sync_tei,
    f.ack_chan_qual, f.ack_sta_load,
    f.coord_nbr_bmp, f.coord_rf_ch, f.coord_rsv1, f.coord_duration,
    f.coord_rsv2, f.coord_band_end_flag, f.coord_option, f.coord_rsv3,
    f.coord_band_end_offset, f.coord_band_start_offset, f.coord_rsv0,
    f.pb_hdr, f.pb_size, f.pb_body, f.pb_rsv, f.pb_pbcs, f.pb_crc_ok, f.pb_crc_calc, f.pb_padding,
    f.beacon_type, f.beacon_net_cplt, f.beacon_multi_net, f.beacon_start_assoc,
    f.beacon_net_seq, f.beacon_snid, f.beacon_head_rsv,
    f.beacon_entry_cnt, f.beacon_ent_type, f.beacon_ent_len, f.beacon_ent_data,
    f.beacon_bpcs, f.beacon_bpcs_calc, f.beacon_bpcs_ok, f.beacon_bpcs_rsv,
    f.ent_net_level, f.ent_sta_line, f.ent_tei, f.ent_role, f.ent_beacon_usage,
    f.ent_sta_mac, f.ent_pco_tei, f.ent_path_rate, f.ent_cap_rsv,
    f.ent_nc_beacon_cnt, f.ent_c_beacon_cnt, f.ent_csma_line_cnt,
    f.ent_proxy_beacon_cnt, f.ent_beacon_slot_len, f.ent_csma_slot_size,
    f.ent_bcsma_line_cnt, f.ent_bcsma_lid, f.ent_tdma_slot_len, f.ent_tdma_lid,
    f.ent_bp_start_ntb, f.ent_bp_len, f.ent_slot_rsv,
    f.ent_ncb_tei, f.ent_ncb_type, f.ent_ncb_rsv,
    f.ent_csma_len, f.ent_csma_phase, f.ent_bcsma_len, f.ent_bcsma_phase,
    f.ent_route_period, f.ent_rp_rsv0, f.ent_route_remain, f.ent_rp_rsv1, f.ent_rp_cco_mac,
    f.ent_target_band, f.ent_band_remain,
    f.ent_cal_time, f.ent_cal_ntb,
    f.mac_version, f.mac_head_flag, f.mac_msdu_len, f.mac_dst_tei, f.mac_src_tei,
    f.mac_restart_cnt, f.mac_bcast_dir, f.mac_send_type, f.mac_msdu_seq, f.mac_hdr_rsv,
    f.msdu_dst_mac, f.msdu_src_mac, f.msdu_vlan, f.msdu_type, f.msdu_vlan_s, f.msdu_type_s,
    f.sh_type, f.sh_len, f.sh_data,
    f.mac_icv, f.mac_icv_calc, f.mac_icv_ok,
    f.mme_version, f.mme_type, f.mme_rsv,
    f.ar_sta_mac, f.ar_proxy_tei, f.ar_link_type, f.ar_link_rsv, f.ar_phase,
    f.ar_dev_type, f.ar_rsv, f.ar_mac_type, f.ar_module, f.ar_link,
    f.ar_assoc_rand, f.ar_manuf_info, f.ar_boot_reason, f.ar_boot_ver,
    f.ar_soft_ver, f.ar_ver_year, f.ar_ver_month, f.ar_ver_day,
    f.ar_manuf_id, f.ar_chip_code, f.ar_hard_rst, f.ar_soft_rst,
    f.ar_proxy_type, f.ar_net_sn, f.ar_mme_ver, f.ar_band_support, f.ar_e2e_seq,
    f.ac_sta_mac, f.ac_result, f.ac_sta_level, f.ac_sta_tei, f.ac_proxy_tei,
    f.ac_total_pkgs, f.ac_pkg_idx, f.ac_last_pkg, f.ac_link_type, f.ac_carrier,
    f.ac_assoc_rand, f.ac_reassoc_time, f.ac_e2e_seq, f.ac_path_seq,
    f.ac_net_sn, f.ac_mme_ver, f.ac_detech,
    f.ri_straight_sta, f.ri_straight_pco, f.ri_table_size, f.ri_rsv,
    f.ri_sta_tei, f.ri_pco_tei, f.ri_pco_child_cnt, f.ri_child_tei,
    f.ri_link_type, f.ri_link_rsv,
    f.cpr_sta_tei, f.cpr_new_proxy, f.cpr_link_type, f.cpr_link_rsv,
    f.cpr_old_proxy, f.cpr_proxy_type, f.cpr_reason, f.cpr_phase, f.cpr_link,
    f.cpr_e2e_seq, f.cpr_net_sn, f.cpr_rsv,
    f.ai_result, f.ai_sta_level, f.ai_sta_mac, f.ai_cco_mac, f.ai_sta_tei,
    f.ai_proxy_tei, f.ai_link_type, f.ai_carrier, f.ai_rsv,
    f.ai_total_pkgs, f.ai_pkg_idx, f.ai_last_pkg, f.ai_assoc_rand,
    f.ai_net_sn, f.ai_reassoc_time, f.ai_e2e_seq,
    f.cpc_result, f.cpc_total_pkgs, f.cpc_pkg_idx, f.cpc_sta_tei, f.cpc_proxy_tei,
    f.cpc_child_cnt, f.cpc_net_sn, f.cpc_link_type, f.cpc_rsv,
    f.cpc_e2e_seq, f.cpc_path_seq, f.cpc_child_tei,
    f.ag_result, f.ag_sta_level, f.ag_cco_mac, f.ag_proxy_tei, f.ag_net_sn,
    f.ag_gather_cnt, f.ag_carrier, f.ag_rsv, f.ag_sta_mac, f.ag_sta_tei,
    f.cpb_result, f.cpb_sta_tei, f.cpb_proxy_tei, f.cpb_net_sn, f.cpb_child_bmp,
    f.cpb_link_type, f.cpb_e2e_seq, f.cpb_path_seq,
    f.li_sta_tei, f.li_reason, f.li_sta_mac, f.li_proxy_tei, f.li_rsv,
    f.hb_ostei, f.hb_max_disc_tei, f.hb_max_disc_cnt, f.hb_disc_bmp, f.hb_rsv,
    f.dl_tei, f.dl_role, f.dl_level, f.dl_mac, f.dl_proxy_tei, f.dl_rsv,
    f.dl_rate_finish, f.dl_proxy_rate, f.dl_proxy_dl_rate,
    f.dl_sta_cnt, f.dl_send_cnt, f.dl_up_route_cnt, f.dl_up_route_size,
    f.dl_route_remain, f.dl_phase0, f.dl_phase1, f.dl_phase2, f.dl_min_rate,
    f.dl_next_hop_tei, f.dl_route_type, f.dl_disc_bmp, f.dl_rcv_cnt, f.dl_rcv_item,
    f.dl2_reason, f.dl2_sta_cnt, f.dl2_delay, f.dl2_rsv, f.dl2_sta_mac,
    f.sr_proxy_tei, f.sr_sta_cnt, f.sr_sta_tei, f.sr_down_rate, f.sr_up_rate,
    f.zc_quantity, f.zc_seq, f.zc_rsv,
    f.zr_tei, f.zr_collect_mode, f.zr_seq, f.zr_total_cnt, f.zr_base_ntb,
    f.zr_rsv, f.zr_ph1_cnt, f.zr_ph2_cnt, f.zr_ph3_cnt, f.zr_diff,
    f.diag_chip_id, f.diag_custom,
    f.rfccr_cco_mac, f.rfccr_nbr_cnt, f.rfccr_nbr_ch, f.rfccr_nbr_option,
    f.ncr_cco_mac, f.ncr_nbr_cnt, f.ncr_nbr_bmp,
    f.app_port, f.app_packet_id, f.app_rsv, f.app_packet_type,
    f.app_ctrl_rsv0, f.app_ctrl_rsv1, f.app_ext_flag, f.app_respond_flag,
    f.app_start_flag, f.app_trans_dir, f.app_business_id, f.app_bid_desc,
    f.app_version, f.app_packet_sn, f.app_packet_len, f.app_payload,
    f.media_phr_mcs, f.media_option, f.media_channel, f.media_is_rf,
    f.col_orig_dst,
    f.bmp_byte,
}

-- =========================================================================
-- 位域读取 helper (little-endian bit 序, 字节内 bit0=LSB, 跨字节连续)
-- =========================================================================
local function read_bits(tvb, start_bit, nbits)
    local val = 0
    for i = 0, nbits - 1 do
        local abs_bit = start_bit + i
        local byte_off = math.floor(abs_bit / 8)
        if byte_off < tvb:len() then
            local b = tvb(byte_off, 1):uint()
            local bit_in_byte = abs_bit % 8
            if band(b, 2 ^ bit_in_byte) ~= 0 then
                val = val + 2 ^ i
            end
        end
    end
    return val
end

-- 小端读多字节字段, 带显式值 (用于跨字节 bit 字段和 le 字段)
local function add_val(tree, field, tvb, off, len, value)
    return tree:add(field, tvb(off, len), value)
end

-- 小端读整字节字段 (逐字节累加: 兼容"块体逻辑视图", 跨块读取仍正确)
local function add_le(tree, field, tvb, off, len)
    local val = 0
    for i = 0, len - 1 do
        val = val + tvb(off + i, 1):uint() * 2 ^ (8 * i)
    end
    return tree:add(field, tvb(off, len), val)
end
-- 块体逻辑视图: 多物理块时把"重组坐标"(各块块体拼接)映射回原始帧坐标.
-- 逐字节读取(1B Range)总是字节精确; (off,len) 跨块时高亮范围顺带覆盖块间字节(值不受影响).
-- 不生成新数据源 → hex 窗口始终显示完整原始帧(PB头/块尾CRC 都在), 点击字段高亮原帧对应字节
local function make_block_view(tvb, base, pbsz, n)
    local body = pbsz - 8  -- NW_2021: PB头4B + 体 + 保留1B + CRC24 3B          -- 块体长
    local inter = pbsz - body      -- 块间字节数
    local total = n * body
    local function map(off)
        local i = math.floor(off / body)
        return base + i * pbsz + (off - i * body)
    end
    local v = setmetatable({}, {
        __call = function(_, off, len)
            off = off or 0
            len = len or (total - off)
            if off < 0 or len <= 0 or off + len > total then
                error("block view: Range is out of bounds")
            end
            local crossings = math.floor((off + len - 1) / body) - math.floor(off / body)
            return tvb(map(off), len + crossings * inter)
        end,
    })
    v.len = function() return total end
    v.body = body    -- 供 add_span 判断跨块分段
    return v
end

-- 逻辑视图的子视图 (坐标平移, 供管理消息体/APP 体; 替代 range:tvb() 子集)
local function view_slice(v, delta)
    local s = setmetatable({}, {
        __call = function(_, off, len)
            off = off or 0
            if len == nil then len = v.len() - delta - off end
            return v(off + delta, len)
        end,
    })
    s.len = function() return v.len() - delta end
    if type(v) == "table" then s.body = v.body end   -- 真实 tvb 无 body(无需分段)
    return s
end

-- 跨块字段分段高亮: 范围跨物理块时按块拆成多个树项(同一字段), 每项只高亮该块内的
-- 数据字节, 不覆盖块间 PB头/CRC; 未跨块/真实 tvb 时等价于普通 tree:add(field, v(off, len))
local function add_span(tree, field, v, off, len)
    if type(v) ~= "table" then
        -- 真实 tvb(单块): 连续, 直接加
        if len == nil then len = v:len() - off end
        return tree:add(field, v(off, len))
    end
    local body = v.body
    if len == nil then len = v.len() - off end
    if not body or math.floor((off + len - 1) / body) == math.floor(off / body) then
        return tree:add(field, v(off, len))
    end
    local last
    for i = math.floor(off / body), math.floor((off + len - 1) / body) do
        local seg_s = math.max(off, i * body)
        local seg_e = math.min(off + len, (i + 1) * body)
        last = tree:add(field, v(seg_s, seg_e - seg_s))
    end
    return last
end


-- 12bit TEI @ 字节 off 起 (bit0)
local function add_tei12(tree, field, tvb, off)
    return add_val(tree, field, tvb, off, 2, read_bits(tvb, off * 8, 12))
end

-- 12bit TEI @ 字节 off bit4 起 (右半字节起)
local function add_tei12h(tree, field, tvb, off)
    return add_val(tree, field, tvb, off, 2, read_bits(tvb, off * 8 + 4, 12))
end

-- 位图逐字节解析: bit 全局索引 = TEI (字节i的bit j → TEI = i*8+j)
-- 每字节输出 "值 (TEI列表)", 顶层节点用 bytes 字段, 逐字节子节点用 string
local function dissect_tei_bitmap(tvb, tree, off, size, field, label_zh, label_en)
    if size <= 0 or off + size > tvb:len() then return end
    local bmp_tree = add_span(tree, field, tvb, off, size)
    for i = 0, size - 1 do
        local b = tvb(off + i, 1):uint()
        local teis = {}
        for j = 0, 7 do
            if band(b, 2 ^ j) ~= 0 then
                teis[#teis + 1] = T("TEI", "TEI") .. (i * 8 + j)
            end
        end
        local txt
        if #teis > 0 then
            txt = string.format("0x%02X (%s)", b, table.concat(teis, ", "))
        else
            txt = string.format("0x%02X", b)
        end
        bmp_tree:add(f.bmp_byte, tvb(off + i, 1), string.format(T("%s[%d]: %s", "%s[%d]: %s"), T(label_zh, label_en), i, txt))
    end
end

-- =========================================================================
-- PB 块大小查表 (与 nw_2021_pb_table.h 一致, 勿与国网共享)
-- =========================================================================

-- 载波路径 TMI(载波映射表索引) → PB 块大小(字节); TMI 13-14 转查 TMI_EXT
local function plc_pb_size(tmi, tmi_ext)
    if tmi == 0 or tmi == 1 then return 520 end
    if tmi >= 2 and tmi <= 6 then return 136 end
    if tmi >= 7 and tmi <= 10 then return 520 end
    if tmi == 11 or tmi == 12 then return 264 end
    -- TMI 13-14: 转查扩展 TMI_EXT
    if tmi_ext >= 1 and tmi_ext <= 6 then return 520 end
    if tmi_ext >= 10 and tmi_ext <= 14 then return 136 end
    return -1
end

-- 无线路径「载荷 PB 大小」(表130) → PB 块大小(字节)
local function rf_pb_size(pblen)
    if pblen == 0 then return 16 end
    if pblen == 1 then return 40 end
    if pblen == 2 then return 72 end
    if pblen == 3 then return 136 end
    if pblen == 4 then return 264 end
    if pblen == 5 then return 520 end
    return -1
end

-- =========================================================================
-- FCH 可变区域解析 (字节1-12, 帧型各自坐标)
-- =========================================================================

-- 信标帧可变区域: BTS(4B) + BPC(4B) + 源TEI(12b) + [载波TMI/符号数/相线 | 无线载荷PB大小]
local function dissect_beacon_vf(tvb, tree, is_rf)
    local bts_raw = tvb(1, 4):le_uint()
    tree:add(f.beacon_bts, tvb(1, 4), bts_raw)
    -- NTB: 25MHz时钟, 40ns/tick, 换算秒 = NTB / 25e6
    tree:add(f.beacon_bts_sec, tvb(1, 4), bts_raw / 25000000.0)
    add_le(tree, f.beacon_bpc, tvb, 5, 4)
    add_tei12(tree, f.beacon_src_tei, tvb, 9)
    if is_rf then
        tree:add(f.beacon_pblen, tvb(11, 1))
    else
        tree:add(f.beacon_tmi, tvb(10, 1))
        add_val(tree, f.beacon_symbol_cnt, tvb, 11, 2, read_bits(tvb, 11 * 8, 9))
        tree:add(f.beacon_phase, tvb(12, 1))
    end
end

-- SOF 帧可变区域: 源TEI(12b) + 目的TEI(12b) + LID(8b) +
--   载波: PB个数(4b) + TMI(4b) @字节7 + 帧长(12b) @字节8 + TMI_EXT @字节12低4b
--   无线: 帧长(12b) @字节5 + 载荷PB大小(4b) @字节6高4b (仅1个物理块)
local function dissect_sof_vf(tvb, tree, is_rf)
    add_tei12(tree, f.sof_src_tei, tvb, 1)
    add_tei12h(tree, f.sof_dst_tei, tvb, 2)
    tree:add(f.sof_lid, tvb(4, 1))
    if is_rf then
        add_val(tree, f.sof_frame_len, tvb, 5, 2, read_bits(tvb, 5 * 8, 12))
        tree:add(f.sof_pblen, tvb(6, 1))
    else
        tree:add(f.sof_pb_num, tvb(7, 1))
        tree:add(f.sof_tmi, tvb(7, 1))
        add_val(tree, f.sof_frame_len, tvb, 8, 2, read_bits(tvb, 8 * 8, 12))
        tree:add(f.sof_tmi_ext, tvb(12, 1))
    end
end

-- ACK 帧可变区域: 按字节12低4bit 扩展帧类型分流
local function dissect_ack_vf(tvb, tree)
    local ext_type = read_bits(tvb, 12 * 8, 4)
    tree:add(f.ack_ext_type, tvb(12, 1))
    if ext_type == 0 then
        -- 常规 ACK
        tree:add(f.ack_rx_res, tvb(1, 1))
        tree:add(f.ack_rx_status, tvb(1, 1))
        add_tei12(tree, f.ack_dst_tei, tvb, 2)
        tree:add(f.ack_rx_pb_num, tvb(3, 1))
    elseif ext_type == 1 then
        -- 网络搜索帧: 目的地址(48b) + 搜索站点TEI(12b)
        tree:add(f.ack_dst_mac, tvb(1, 6))
        add_tei12(tree, f.ack_search_tei, tvb, 7)
    elseif ext_type == 2 then
        -- 同步帧: 同步时间戳(32b) + 同步站点TEI(12b)
        add_le(tree, f.ack_sync_ts, tvb, 1, 4)
        add_tei12(tree, f.ack_sync_tei, tvb, 5)
    elseif ext_type == 3 then
        -- 无线切频帧: 目的地址(48b) + 信道质量 + 站点负载
        tree:add(f.ack_dst_mac, tvb(1, 6))
        tree:add(f.ack_chan_qual, tvb(7, 1))
        tree:add(f.ack_sta_load, tvb(8, 1))
    end
    -- 10 时隙预约 / 11 测距响应 / 12 测距请求: 南网扩展, 暂不解析(与 Qt 监控器一致)
end

-- 网间协调帧可变区域: 邻居NID位图(16b) + 信道(8b) + 时长(14b,×40ms) +
--   带宽截止标志/option + 带宽截止/开始偏移(16b,×4ms)
local function dissect_coord_vf(tvb, tree)
    add_le(tree, f.coord_nbr_bmp, tvb, 1, 2)
    tree:add(f.coord_rf_ch, tvb(3, 1))
    add_val(tree, f.coord_rsv1, tvb, 4, 2, read_bits(tvb, 4 * 8, 10))
    add_val(tree, f.coord_duration, tvb, 5, 2, read_bits(tvb, 5 * 8 + 2, 14))
    tree:add(f.coord_rsv2, tvb(7, 1))
    tree:add(f.coord_band_end_flag, tvb(7, 1))
    tree:add(f.coord_option, tvb(7, 1))
    tree:add(f.coord_rsv3, tvb(7, 1))
    add_le(tree, f.coord_band_end_offset, tvb, 8, 2)
    add_le(tree, f.coord_band_start_offset, tvb, 10, 2)
    tree:add(f.coord_rsv0, tvb(12, 1))
end

-- =========================================================================
-- 信标帧载荷 (固定头 6B + 管理信息 + BPCS + 保留字节 + PB CRC24)
-- =========================================================================

-- 站点能力条目 (0x01, 内容 20B)
local function dissect_entry_sta_cap(tvb, tree, off)
    tree:add(f.ent_net_level, tvb(off, 1))
    tree:add(f.ent_sta_line, tvb(off, 1))
    local tei = read_bits(tvb, off * 8 + 8, 12)
    add_tei12(tree, f.ent_tei, tvb, off + 1)
    tree:add(f.ent_role, tvb(off + 2, 1))
    tree:add(f.ent_beacon_usage, tvb(off + 3, 1))
    tree:add(f.ent_sta_mac, tvb(off + 4, 6))
    -- 记录 TEI ↔ MAC 映射
    record_tei_mac(tei, mac_to_str(tvb, off + 4))
    add_tei12(tree, f.ent_pco_tei, tvb, off + 10)
    add_le(tree, f.ent_path_rate, tvb, off + 12, 4)
    tree:add(f.ent_cap_rsv, tvb(off + 16, 4))
end

-- 时隙分配条目 (0x02, 固定头 24B + 可变长区)
local function dissect_entry_slot_alloc(tvb, tree, off)
    local nc_beacon_cnt = tvb(off, 1):uint()
    local csma_cnt = tvb(off + 2, 1):uint()
    local bcsma_cnt = tvb(off + 7, 1):uint()
    tree:add(f.ent_nc_beacon_cnt, tvb(off, 1))
    tree:add(f.ent_c_beacon_cnt, tvb(off + 1, 1))
    tree:add(f.ent_csma_line_cnt, tvb(off + 2, 1))
    tree:add(f.ent_proxy_beacon_cnt, tvb(off + 3, 1))
    add_le(tree, f.ent_beacon_slot_len, tvb, off + 4, 2)
    tree:add(f.ent_csma_slot_size, tvb(off + 6, 1))
    tree:add(f.ent_bcsma_line_cnt, tvb(off + 7, 1))
    tree:add(f.ent_bcsma_lid, tvb(off + 8, 1))
    add_le(tree, f.ent_tdma_slot_len, tvb, off + 9, 2)
    tree:add(f.ent_tdma_lid, tvb(off + 11, 1))
    add_le(tree, f.ent_bp_start_ntb, tvb, off + 12, 4)
    add_le(tree, f.ent_bp_len, tvb, off + 16, 4)
    add_le(tree, f.ent_slot_rsv, tvb, off + 20, 4)
    -- 可变长区从固定头 24B 后开始
    local pos = off + 24
    -- 非中央信标信息 (2B/条): TEI 12b + 信标类型 1b + 保留 3b
    for i = 1, nc_beacon_cnt do
        if pos + 2 > tvb:len() then return end
        add_tei12(tree, f.ent_ncb_tei, tvb, pos)
        tree:add(f.ent_ncb_type, tvb(pos + 1, 1))
        tree:add(f.ent_ncb_rsv, tvb(pos + 1, 1))
        pos = pos + 2
    end
    -- CSMA 时隙信息 (4B/条): 时隙长度 24b + 相线 8b
    for i = 1, csma_cnt do
        if pos + 4 > tvb:len() then return end
        tree:add(f.ent_csma_len, tvb(pos, 3), tvb(pos, 3):le_uint())
        tree:add(f.ent_csma_phase, tvb(pos + 3, 1))
        pos = pos + 4
    end
    -- 绑定 CSMA 时隙信息 (4B/条)
    for i = 1, bcsma_cnt do
        if pos + 4 > tvb:len() then return end
        tree:add(f.ent_bcsma_len, tvb(pos, 3), tvb(pos, 3):le_uint())
        tree:add(f.ent_bcsma_phase, tvb(pos + 3, 1))
        pos = pos + 4
    end
end

-- 路由参数条目 (0x06, 内容 32B)
local function dissect_entry_route_param(tvb, tree, off)
    add_le(tree, f.ent_route_period, tvb, off, 2)
    add_le(tree, f.ent_rp_rsv0, tvb, off + 2, 2)
    add_le(tree, f.ent_route_remain, tvb, off + 4, 2)
    tree:add(f.ent_rp_rsv1, tvb(off + 6, 20))
    tree:add(f.ent_rp_cco_mac, tvb(off + 26, 6))
end

-- 频段变更条目 (0x07, 内容 5B)
local function dissect_entry_band_change(tvb, tree, off)
    tree:add(f.ent_target_band, tvb(off, 1))
    add_le(tree, f.ent_band_remain, tvb, off + 1, 4)
end

-- 万年历同步条目 (0x0B, 内容 8B)
local function dissect_entry_calendar(tvb, tree, off)
    add_le(tree, f.ent_cal_time, tvb, off, 4)
    add_le(tree, f.ent_cal_ntb, tvb, off + 4, 4)
end

-- 信标管理信息 (首字节条目数, 随后 头+长度+内容), 返回下一个 offset
-- 条目长度语义(与 Qt nw_2021_beaconparser 一致):
--   长度字段值 len_raw = 头(1B) + 长度字段(1/2B) + 内容
--   内容长 = len_raw - 2 (普通) / len_raw - 3 (0x02 时隙分配, 长度字段 2B)
local function dissect_beacon_mgmt_info(tvb, tree, off, mgmt_end)
    if off >= tvb:len() then return off end
    tree:add(f.beacon_entry_cnt, tvb(off, 1))
    local cnt = tvb(off, 1):uint()
    local pos = off + 1
    for i = 1, cnt do
        if pos >= mgmt_end then break end
        local etype = tvb(pos, 1):uint()
        tree:add(f.beacon_ent_type, tvb(pos, 1))
        pos = pos + 1
        -- 长度字段: 时隙分配(0x02)用 2B, 其余 1B (小端)
        local len_bytes = (etype == 0x02) and 2 or 1
        if pos + len_bytes > mgmt_end then break end
        local len_raw = tvb(pos, len_bytes):le_uint()
        tree:add(f.beacon_ent_len, tvb(pos, len_bytes), len_raw)
        pos = pos + len_bytes
        -- 内容长 = len_raw - (头1B + 长度字段len_bytes)
        local item_len = len_raw - (1 + len_bytes)
        if item_len <= 0 or pos + item_len > mgmt_end then break end
        tree:add(f.beacon_ent_data, tvb(pos, item_len))
        -- 按条目类型解析内容
        if etype == 0x01 then
            dissect_entry_sta_cap(tvb, tree, pos)
        elseif etype == 0x02 then
            dissect_entry_slot_alloc(tvb, tree, pos)
        elseif etype == 0x06 then
            dissect_entry_route_param(tvb, tree, pos)
        elseif etype == 0x07 then
            dissect_entry_band_change(tvb, tree, pos)
        elseif etype == 0x0B then
            dissect_entry_calendar(tvb, tree, pos)
        end
        -- 0x0A 频段探测 / 0x80-0xEF 厂家自定义 / 其它保留: 仅显示内容 hex
        pos = pos + item_len
    end
    return pos
end

-- 信标帧载荷 (固定头 6B 起始于 off, 整块 pbsize)
-- 结构(与 Qt nw_2021_beaconparser 一致):
--   帧载荷区 gb = [off, off+pbsize-3): 固定头(6B) + 管理区 + BPCS(CRC32,4B) + 保留字节(1B)
--   BPCS 覆盖 gb 前 gb_len-5 字节(固定头+管理区); 块尾 3B 为 PB CRC24
local function dissect_beacon_payload(tvb, tree, off, pbsize)
    local gb_end = off + pbsize - 3     -- 帧载荷区末尾(不含 PB CRC24)
    -- 固定头 (6B)
    tree:add(f.beacon_type, tvb(off, 1))
    tree:add(f.beacon_net_cplt, tvb(off, 1))
    tree:add(f.beacon_multi_net, tvb(off, 1))
    tree:add(f.beacon_start_assoc, tvb(off, 1))
    tree:add(f.beacon_net_seq, tvb(off + 1, 1))
    tree:add(f.beacon_snid, tvb(off + 2, 1))
    tree:add(f.beacon_head_rsv, tvb(off + 2, 4))

    -- 信标管理信息从 off+6 到 BPCS 起点(gb_end-5)
    local pos = dissect_beacon_mgmt_info(tvb, tree, off + 6, gb_end - 5)

    -- 填充区: 管理区消费终点到 BPCS 前
    if pos < gb_end - 5 then
        tree:add(f.pb_padding, tvb(pos, gb_end - 5 - pos))
    end

    -- BPCS CRC32 (帧载荷区倒数 5..2 字节): 校验 off..gb_end-5 (不含 BPCS 与保留字节)
    if gb_end - 5 >= off then
        local bpcs_rx = tvb(gb_end - 5, 4):le_uint()
        local bpcs_calc = crc32_le(tvb, off, pbsize - 4)
        tree:add(f.beacon_bpcs, tvb(gb_end - 5, 4), bpcs_rx)
        tree:add(f.beacon_bpcs_calc, tvb(gb_end - 5, 4), bpcs_calc)
        tree:add(f.beacon_bpcs_ok, tvb(gb_end - 5, 1), bpcs_calc == bpcs_rx)
    end
    -- 保留字节 (帧载荷区最后 1B)
    tree:add(f.beacon_bpcs_rsv, tvb(gb_end - 1, 1))
    -- PB CRC24 (块尾 3B): 覆盖块内前 pbsize-3 字节
    if off + pbsize <= tvb:len() then
        local crc_rx = tvb(off + pbsize - 3, 3):le_uint()
        local crc_calc = crc24_lsb(tvb, off, pbsize)
        tree:add(f.pb_pbcs, tvb(off + pbsize - 3, 3), crc_rx)
        tree:add(f.pb_crc_calc, tvb(off + pbsize - 3, 3), crc_calc)
        tree:add(f.pb_crc_ok, tvb(off + pbsize - 3, 1), crc_calc == crc_rx)
    end
end

-- =========================================================================
-- MMe 管理消息体解析 (偏移相对 MMe 起点: 0=MMVersion, 1-2=MMType, 消息体从 6 起)
-- =========================================================================

-- 关联请求 (0x0030)
local function dissect_mme_assoc_req(tvb, tree)
    tree:add(f.ar_sta_mac, tvb(6, 6))
    -- 候选代理 TEI x5, 每个 2 字节 (TEI 12b + 链路类型 1b + 保留 3b)
    for i = 0, 4 do
        local p = 12 + i * 2
        add_tei12(tree, f.ar_proxy_tei, tvb, p)
        tree:add(f.ar_link_type, tvb(p + 1, 1))
        tree:add(f.ar_link_rsv, tvb(p + 1, 1))
    end
    tree:add(f.ar_phase, tvb(22, 1))
    tree:add(f.ar_phase, tvb(23, 1))
    tree:add(f.ar_phase, tvb(24, 1))
    tree:add(f.ar_dev_type, tvb(25, 1))
    tree:add(f.ar_rsv, tvb(26, 2))
    tree:add(f.ar_mac_type, tvb(28, 1))
    tree:add(f.ar_module, tvb(29, 1))
    tree:add(f.ar_link, tvb(29, 1))
    add_le(tree, f.ar_assoc_rand, tvb, 30, 4)
    add_span(tree, f.ar_manuf_info, tvb, 34, 18)
    -- 站点版本信息 (10B @52)
    tree:add(f.ar_boot_reason, tvb(52, 1))
    tree:add(f.ar_boot_ver, tvb(53, 1))
    add_le(tree, f.ar_soft_ver, tvb, 54, 2)
    tree:add(f.ar_ver_year, tvb(56, 1))
    add_val(tree, f.ar_ver_month, tvb, 56, 2, read_bits(tvb, 56 * 8 + 7, 4))
    add_val(tree, f.ar_ver_day, tvb, 57, 1, read_bits(tvb, 57 * 8 + 3, 5))
    add_le(tree, f.ar_manuf_id, tvb, 58, 2)
    add_le(tree, f.ar_chip_code, tvb, 60, 2)
    add_le(tree, f.ar_hard_rst, tvb, 62, 2)
    add_le(tree, f.ar_soft_rst, tvb, 64, 2)
    tree:add(f.ar_proxy_type, tvb(66, 1))
    tree:add(f.ar_net_sn, tvb(67, 1))
    tree:add(f.ar_mme_ver, tvb(68, 1))
    tree:add(f.ar_band_support, tvb(69, 1))
    add_le(tree, f.ar_e2e_seq, tvb, 70, 4)
end

-- 路由信息 (关联确认/关联指示共用): 汇总头 8B + 直连站点(2B/条) + 直连代理(4B/条,含子站点)
local function dissect_route_info(tvb, tree, off)
    if off + 8 > tvb:len() then return end
    local sta_sum = read_bits(tvb, off * 8, 16)
    local pco_sum = read_bits(tvb, (off + 2) * 8, 16)
    add_le(tree, f.ri_straight_sta, tvb, off, 2)
    add_le(tree, f.ri_straight_pco, tvb, off + 2, 2)
    add_le(tree, f.ri_table_size, tvb, off + 4, 2)
    add_le(tree, f.ri_rsv, tvb, off + 6, 2)
    local pos = off + 8
    -- 直连站点条目: 每个 2 字节
    for i = 1, sta_sum do
        if pos + 2 > tvb:len() then return end
        add_tei12(tree, f.ri_sta_tei, tvb, pos)
        tree:add(f.ri_link_type, tvb(pos + 1, 1))
        tree:add(f.ri_link_rsv, tvb(pos + 1, 1))
        pos = pos + 2
    end
    -- 直连代理条目: 每个 4 字节 (PCO TEI + 链路 + 子站点数) + 子站点(2B/条)
    for i = 1, pco_sum do
        if pos + 4 > tvb:len() then return end
        add_tei12(tree, f.ri_pco_tei, tvb, pos)
        tree:add(f.ri_link_type, tvb(pos + 1, 1))
        tree:add(f.ri_link_rsv, tvb(pos + 1, 1))
        local child_sum = read_bits(tvb, (pos + 2) * 8, 16)
        add_le(tree, f.ri_pco_child_cnt, tvb, pos + 2, 2)
        pos = pos + 4
        for c = 1, child_sum do
            if pos + 2 > tvb:len() then return end
            add_tei12(tree, f.ri_child_tei, tvb, pos)
            pos = pos + 2
        end
    end
end

-- 关联确认 (0x0031)
local function dissect_mme_assoc_cnf(tvb, tree)
    tree:add(f.ac_sta_mac, tvb(6, 6))
    tree:add(f.ac_result, tvb(12, 1))
    tree:add(f.ac_sta_level, tvb(13, 1))
    local sta_tei = read_bits(tvb, 14 * 8, 12)
    add_tei12(tree, f.ac_sta_tei, tvb, 14)
    add_le(tree, f.ac_proxy_tei, tvb, 16, 2)
    tree:add(f.ac_total_pkgs, tvb(18, 1))
    tree:add(f.ac_pkg_idx, tvb(19, 1))
    tree:add(f.ac_last_pkg, tvb(20, 1))
    tree:add(f.ac_link_type, tvb(21, 1))
    tree:add(f.ac_carrier, tvb(21, 1))
    add_le(tree, f.ac_assoc_rand, tvb, 22, 4)
    add_le(tree, f.ac_reassoc_time, tvb, 26, 4)
    add_le(tree, f.ac_e2e_seq, tvb, 30, 4)
    add_le(tree, f.ac_path_seq, tvb, 34, 4)
    tree:add(f.ac_net_sn, tvb(38, 1))
    tree:add(f.ac_mme_ver, tvb(39, 1))
    tree:add(f.ac_detech, tvb(39, 1))
    -- 记录 TEI ↔ MAC 映射 (CCO 分配的站点 TEI 与站点 MAC)
    record_tei_mac(sta_tei, mac_to_str(tvb, 6))
    -- 路由信息 (@42)
    dissect_route_info(tvb, tree, 42)
end

-- 代理变更请求 (0x0032)
local function dissect_mme_cproxy_req(tvb, tree)
    add_le(tree, f.cpr_sta_tei, tvb, 6, 2)
    -- 新代理 TEI x5, 每个 2 字节
    for i = 0, 4 do
        local p = 8 + i * 2
        add_tei12(tree, f.cpr_new_proxy, tvb, p)
        tree:add(f.cpr_link_type, tvb(p + 1, 1))
        tree:add(f.cpr_link_rsv, tvb(p + 1, 1))
    end
    add_le(tree, f.cpr_old_proxy, tvb, 18, 2)
    tree:add(f.cpr_proxy_type, tvb(20, 1))
    tree:add(f.cpr_reason, tvb(21, 1))
    tree:add(f.cpr_phase, tvb(22, 1))
    tree:add(f.cpr_phase, tvb(23, 1))
    tree:add(f.cpr_phase, tvb(24, 1))
    tree:add(f.cpr_link, tvb(25, 1))
    add_le(tree, f.cpr_e2e_seq, tvb, 26, 4)
    tree:add(f.cpr_net_sn, tvb(30, 1))
    tree:add(f.cpr_rsv, tvb(31, 15))
end

-- 关联指示 (0x0034)
local function dissect_mme_assoc_ind(tvb, tree)
    tree:add(f.ai_result, tvb(6, 1))
    tree:add(f.ai_sta_level, tvb(7, 1))
    tree:add(f.ai_sta_mac, tvb(8, 6))
    tree:add(f.ai_cco_mac, tvb(14, 6))
    local sta_tei = read_bits(tvb, 20 * 8, 12)
    add_tei12(tree, f.ai_sta_tei, tvb, 20)
    add_le(tree, f.ai_proxy_tei, tvb, 22, 2)
    tree:add(f.ai_link_type, tvb(24, 1))
    tree:add(f.ai_carrier, tvb(24, 1))
    tree:add(f.ai_rsv, tvb(24, 3))
    tree:add(f.ai_total_pkgs, tvb(27, 1))
    tree:add(f.ai_pkg_idx, tvb(28, 1))
    tree:add(f.ai_last_pkg, tvb(29, 1))
    add_le(tree, f.ai_assoc_rand, tvb, 30, 4)
    tree:add(f.ai_net_sn, tvb(51, 1))
    tree:add(f.ai_rsv, tvb(52, 2))
    add_le(tree, f.ai_reassoc_time, tvb, 54, 4)
    add_le(tree, f.ai_e2e_seq, tvb, 58, 4)
    tree:add(f.ai_rsv, tvb(62, 8))
    -- 记录 TEI ↔ MAC 映射
    record_tei_mac(sta_tei, mac_to_str(tvb, 8))
    -- 路由信息 (@70)
    dissect_route_info(tvb, tree, 70)
end

-- 代理变更确认 (0x0037)
local function dissect_mme_cproxy_cnf(tvb, tree)
    add_le(tree, f.cpc_result, tvb, 6, 4)
    tree:add(f.cpc_total_pkgs, tvb(10, 1))
    tree:add(f.cpc_pkg_idx, tvb(11, 1))
    add_le(tree, f.cpc_sta_tei, tvb, 12, 2)
    add_le(tree, f.cpc_proxy_tei, tvb, 14, 2)
    local child_cnt = read_bits(tvb, 128, 16)
    add_le(tree, f.cpc_child_cnt, tvb, 16, 2)
    tree:add(f.cpc_rsv, tvb(18, 1))
    tree:add(f.cpc_net_sn, tvb(19, 1))
    tree:add(f.cpc_link_type, tvb(20, 1))
    tree:add(f.cpc_rsv, tvb(21, 1))
    add_le(tree, f.cpc_e2e_seq, tvb, 22, 4)
    add_le(tree, f.cpc_path_seq, tvb, 26, 4)
    tree:add(f.cpc_rsv, tvb(30, 8))
    -- 子站点条目 (@38): 每个 2 字节 (TEI 12b + 链路类型 + 保留)
    for i = 0, child_cnt - 1 do
        local p = 38 + i * 2
        if p + 2 > tvb:len() then break end
        add_tei12(tree, f.cpc_child_tei, tvb, p)
        tree:add(f.ri_link_type, tvb(p + 1, 1))
        tree:add(f.ri_link_rsv, tvb(p + 1, 1))
    end
end

-- 关联汇总指示 (0x003A)
local function dissect_mme_assoc_gather(tvb, tree)
    tree:add(f.ag_result, tvb(6, 1))
    tree:add(f.ag_sta_level, tvb(7, 1))
    tree:add(f.ag_cco_mac, tvb(8, 6))
    add_tei12(tree, f.ag_proxy_tei, tvb, 14)
    tree:add(f.ag_net_sn, tvb(16, 1))
    local cnt = tvb(17, 1):uint()
    tree:add(f.ag_gather_cnt, tvb(17, 1))
    tree:add(f.ag_carrier, tvb(18, 1))
    tree:add(f.ag_rsv, tvb(19, 15))
    -- 新站点信息 (@34): 每条 8 字节 (MAC 6B + TEI 12b + 保留 4b)
    for i = 0, cnt - 1 do
        local p = 34 + i * 8
        if p + 8 > tvb:len() then break end
        tree:add(f.ag_sta_mac, tvb(p, 6))
        local sta_tei = read_bits(tvb, (p + 6) * 8, 12)
        add_tei12(tree, f.ag_sta_tei, tvb, p + 6)
        -- 记录 TEI ↔ MAC 映射
        record_tei_mac(sta_tei, mac_to_str(tvb, p))
    end
end

-- 代理变更确认位图版 (0x003B)
local function dissect_mme_cproxy_bmp(tvb, tree)
    add_le(tree, f.cpb_result, tvb, 6, 4)
    add_le(tree, f.cpb_sta_tei, tvb, 10, 2)
    add_le(tree, f.cpb_proxy_tei, tvb, 12, 2)
    tree:add(f.cpb_net_sn, tvb(14, 1))
    -- 子站点位图 (@15, 130B)
    dissect_tei_bitmap(tvb, tree, 15, 130, f.cpb_child_bmp, T("子站点位图", "Child STA Bitmap"), "Child STA Bitmap")
    tree:add(f.cpb_link_type, tvb(145, 1))
    add_le(tree, f.cpb_e2e_seq, tvb, 146, 4)
    add_le(tree, f.cpb_path_seq, tvb, 150, 4)
end

-- 离线指示 (0x0049)
local function dissect_mme_leave(tvb, tree)
    add_le(tree, f.li_sta_tei, tvb, 6, 2)
    add_le(tree, f.li_reason, tvb, 8, 2)
    tree:add(f.li_sta_mac, tvb(10, 6))
    add_le(tree, f.li_proxy_tei, tvb, 16, 2)
    tree:add(f.li_rsv, tvb(18, 8))
end

-- 心跳检测 (0x0051)
local function dissect_mme_heartbeat(tvb, tree)
    add_le(tree, f.hb_ostei, tvb, 6, 2)
    add_le(tree, f.hb_max_disc_tei, tvb, 8, 2)
    add_le(tree, f.hb_max_disc_cnt, tvb, 10, 4)
    -- 可发现站点位图 (@20, 130B) + 保留 (@150)
    dissect_tei_bitmap(tvb, tree, 20, 130, f.hb_disc_bmp, T("可发现站点位图", "Discoverable STA Bitmap"), "Discoverable STA Bitmap")
    tree:add(f.hb_rsv, tvb(150, 1))
end

-- 发现列表 (0x0055)
local function dissect_mme_disc_list(tvb, tree)
    local tei = read_bits(tvb, 6 * 8, 16)
    add_le(tree, f.dl_tei, tvb, 6, 2)
    tree:add(f.dl_role, tvb(8, 1))
    tree:add(f.dl_level, tvb(9, 1))
    tree:add(f.dl_mac, tvb(10, 6))
    add_le(tree, f.dl_proxy_tei, tvb, 16, 2)
    tree:add(f.dl_rsv, tvb(18, 3))
    tree:add(f.dl_rate_finish, tvb(21, 1))
    add_le(tree, f.dl_proxy_rate, tvb, 22, 4)
    add_le(tree, f.dl_proxy_dl_rate, tvb, 26, 4)
    local node_num = read_bits(tvb, 240, 16)
    add_le(tree, f.dl_sta_cnt, tvb, 30, 2)
    add_le(tree, f.dl_send_cnt, tvb, 32, 2)
    local up_route_cnt = read_bits(tvb, 272, 16)
    add_le(tree, f.dl_up_route_cnt, tvb, 34, 2)
    tree:add(f.dl_up_route_size, tvb(36, 1))
    tree:add(f.dl_rsv, tvb(37, 2))
    add_le(tree, f.dl_route_remain, tvb, 39, 2)
    tree:add(f.dl_phase0, tvb(41, 1))
    tree:add(f.dl_phase1, tvb(41, 1))
    tree:add(f.dl_phase2, tvb(41, 1))
    tree:add(f.dl_min_rate, tvb(42, 1))
    tree:add(f.dl_rsv, tvb(43, 5))
    -- 记录 TEI ↔ MAC 映射 (发送发现列表报文的站点)
    record_tei_mac(read_bits(tvb, 6 * 8, 16), mac_to_str(tvb, 10))
    -- 上行路由条目 (@48): 每条 3 字节 (下一跳TEI 12b + 保留4b + 路由类型 8b)
    local pos = 48
    for i = 0, up_route_cnt - 1 do
        if pos + 3 > tvb:len() then break end
        add_tei12(tree, f.dl_next_hop_tei, tvb, pos)
        tree:add(f.ri_link_rsv, tvb(pos + 1, 1))
        tree:add(f.dl_route_type, tvb(pos + 2, 1))
        pos = pos + 3
    end
    -- 发现站点列表位图 (128B, bit 位置 = TEI)
    dissect_tei_bitmap(tvb, tree, pos, 128, f.dl_disc_bmp, T("发现站点列表位图", "Discovery STA List Bitmap"), "Discovery STA List Bitmap")
    -- 收到发现列表信息: 条目数 = 位图中置位 bit 总数, 每个 1 字节, 依次对应置位 TEI
    local rcv_pos = pos + 128
    if rcv_pos <= tvb:len() then
        local order = 0
        for bi = 0, 127 do
            local b = tvb(pos + bi, 1):uint()
            for j = 0, 7 do
                if band(b, 2 ^ j) ~= 0 then
                    local tei2 = bi * 8 + j
                    if rcv_pos + order < tvb:len() then
                        local v = tvb(rcv_pos + order, 1):uint()
                        tree:add(f.dl_rcv_cnt, tvb(rcv_pos + order, 1), v)
                        tree:add(f.dl_rcv_item, tvb(rcv_pos + order, 1),
                            string.format(T("接收发现列表数[%d] (TEI %d): %d", "Rcv Discovery List Count[%d] (TEI %d): %d"), order, tei2, v))
                    end
                    order = order + 1
                end
            end
        end
    end
end

-- 延迟离线指示 (0x005D)
local function dissect_mme_delay_leave(tvb, tree)
    add_le(tree, f.dl2_reason, tvb, 6, 2)
    local sta_cnt = read_bits(tvb, 64, 16)
    add_le(tree, f.dl2_sta_cnt, tvb, 8, 2)
    add_le(tree, f.dl2_delay, tvb, 10, 2)
    tree:add(f.dl2_rsv, tvb(12, 10))
    -- 离线站点 MAC (@22): 每条 6 字节
    for i = 0, sta_cnt - 1 do
        local p = 22 + i * 6
        if p + 6 > tvb:len() then break end
        tree:add(f.dl2_sta_mac, tvb(p, 6))
    end
end

-- 通信成功率上报 (0x005E)
local function dissect_mme_succ_rate(tvb, tree)
    add_le(tree, f.sr_proxy_tei, tvb, 6, 2)
    local cnt = read_bits(tvb, 64, 16)
    add_le(tree, f.sr_sta_cnt, tvb, 8, 2)
    -- 站点成功率条目 (@10): 每条 4 字节 (TEI 16b + 下行 8b + 上行 8b)
    for i = 0, cnt - 1 do
        local p = 10 + i * 4
        if p + 4 > tvb:len() then break end
        add_le(tree, f.sr_sta_tei, tvb, p, 2)
        tree:add(f.sr_down_rate, tvb(p + 2, 1))
        tree:add(f.sr_up_rate, tvb(p + 3, 1))
    end
end

-- 过零NTB采集指示 (0x0062)
local function dissect_mme_zc_collect(tvb, tree)
    tree:add(f.zc_quantity, tvb(6, 1))
    tree:add(f.zc_seq, tvb(7, 1))
    add_le(tree, f.zc_rsv, tvb, 8, 2)
end

-- 过零NTB上报 (0x0063): 相线1/2/3差值列表 (每差值 2B 16bit, 按相线顺序)
local function dissect_mme_zc_report(tvb, tree)
    add_tei12(tree, f.zr_tei, tvb, 6)
    tree:add(f.zr_collect_mode, tvb(7, 1))
    tree:add(f.zr_seq, tvb(8, 1))
    tree:add(f.zr_total_cnt, tvb(9, 1))
    add_le(tree, f.zr_base_ntb, tvb, 10, 4)
    tree:add(f.zr_rsv, tvb(14, 1))
    local n1 = tvb(15, 1):uint()
    local n2 = tvb(16, 1):uint()
    local n3 = tvb(17, 1):uint()
    tree:add(f.zr_ph1_cnt, tvb(15, 1))
    tree:add(f.zr_ph2_cnt, tvb(16, 1))
    tree:add(f.zr_ph3_cnt, tvb(17, 1))
    local pos = 18
    local function add_diffs(cnt)
        for i = 1, cnt do
            if pos + 2 > tvb:len() then return end
            add_le(tree, f.zr_diff, tvb, pos, 2)
            pos = pos + 2
        end
    end
    add_diffs(n1)
    add_diffs(n2)
    add_diffs(n3)
end

-- 网络诊断 (0x0064)
local function dissect_mme_diag(tvb, tree)
    add_le(tree, f.diag_chip_id, tvb, 6, 2)
    if tvb:len() > 8 then
        add_span(tree, f.diag_custom, tvb, 8, nil)
    end
end

-- 无线信道冲突上报 (0x0070)
local function dissect_mme_rfccr(tvb, tree)
    tree:add(f.rfccr_cco_mac, tvb(6, 6))
    local cnt = tvb(12, 1):uint()
    tree:add(f.rfccr_nbr_cnt, tvb(12, 1))
    -- 邻居网络条目 (@13, 交错): 每条 2B = 信道号 1B + option 2bit + 保留 6bit
    for i = 0, cnt - 1 do
        local p = 13 + 2 * i
        if p + 2 > tvb:len() then break end
        tree:add(f.rfccr_nbr_ch, tvb(p, 1))
        tree:add(f.rfccr_nbr_option, tvb(p + 1, 1))
    end
end

-- 网络冲突上报 (0x005F)
local function dissect_mme_ncr(tvb, tree)
    tree:add(f.ncr_cco_mac, tvb(6, 6))
    tree:add(f.ncr_nbr_cnt, tvb(12, 1))
    add_le(tree, f.ncr_nbr_bmp, tvb, 13, 2)
end

-- MMe 消息体分发
local function dissect_mme_body(tvb, tree, mmtype)
    if mmtype == 0x0030 then dissect_mme_assoc_req(tvb, tree)
    elseif mmtype == 0x0031 then dissect_mme_assoc_cnf(tvb, tree)
    elseif mmtype == 0x0032 then dissect_mme_cproxy_req(tvb, tree)
    elseif mmtype == 0x0034 then dissect_mme_assoc_ind(tvb, tree)
    elseif mmtype == 0x0037 then dissect_mme_cproxy_cnf(tvb, tree)
    elseif mmtype == 0x003A then dissect_mme_assoc_gather(tvb, tree)
    elseif mmtype == 0x003B then dissect_mme_cproxy_bmp(tvb, tree)
    elseif mmtype == 0x0049 then dissect_mme_leave(tvb, tree)
    elseif mmtype == 0x0051 then dissect_mme_heartbeat(tvb, tree)
    elseif mmtype == 0x0055 then dissect_mme_disc_list(tvb, tree)
    elseif mmtype == 0x005D then dissect_mme_delay_leave(tvb, tree)
    elseif mmtype == 0x005E then dissect_mme_succ_rate(tvb, tree)
    elseif mmtype == 0x005F then dissect_mme_ncr(tvb, tree)
    elseif mmtype == 0x0062 then dissect_mme_zc_collect(tvb, tree)
    elseif mmtype == 0x0063 then dissect_mme_zc_report(tvb, tree)
    elseif mmtype == 0x0064 then dissect_mme_diag(tvb, tree)
    elseif mmtype == 0x0070 then dissect_mme_rfccr(tvb, tree)
    end
    -- 0x0083/0x0084/0x00A0: 站点TEI列表请求/响应、汇聚数据上报, 暂不解析(与 Qt 监控器一致)
end

-- =========================================================================
-- APP 应用层报文 (通道控制信息 4B + 业务报文头 8B + APP Data)
-- =========================================================================
local function dissect_app(tvb, tree, pinfo)
    if tvb:len() < 12 then return end
    -- 通道控制信息 (4B)
    tree:add(f.app_port, tvb(0, 1))
    tree:add(f.app_packet_id, tvb(1, 2), read_bits(tvb, 8, 16))
    tree:add(f.app_rsv, tvb(3, 1))
    -- 业务报文头 (8B)
    local packet_type = band(tvb(4, 1):uint(), 0x0F)
    local bid = tvb(6, 1):uint()
    tree:add(f.app_packet_type, tvb(4, 1))
    tree:add(f.app_ctrl_rsv0, tvb(4, 1))
    tree:add(f.app_ctrl_rsv1, tvb(5, 1))
    tree:add(f.app_ext_flag, tvb(5, 1))
    tree:add(f.app_respond_flag, tvb(5, 1))
    tree:add(f.app_start_flag, tvb(5, 1))
    tree:add(f.app_trans_dir, tvb(5, 1))
    tree:add(f.app_business_id, tvb(6, 1))
    -- 业务标识释义 (依赖帧类型域, 表9)
    local port = tvb(0, 1):uint()
    local bid_name = business_id_name(port, packet_type, bid)
    if bid_name then
        tree:add(f.app_bid_desc, tvb(6, 1), bid_name)
    end
    tree:add(f.app_version, tvb(7, 1))
    add_le(tree, f.app_packet_sn, tvb, 8, 2)
    add_le(tree, f.app_packet_len, tvb, 10, 2)
    -- 应用层载荷 (业务报文头之后, 含业务扩展域)
    if tvb:len() > 12 then
        add_span(tree, f.app_payload, tvb, 12, nil)
    end
    -- info 列追加帧类型 + BID 释义
    local pt_name = app_packet_type_vals[packet_type] or string.format("0x%X", packet_type)
    local cur_info = tostring(pinfo.cols.info)
    if bid_name then
        pinfo.cols.info = cur_info .. string.format(" APP[%s BID=0x%02X %s]", pt_name, bid, bid_name)
    else
        pinfo.cols.info = cur_info .. string.format(" APP[%s BID=0x%02X]", pt_name, bid)
    end
end

-- =========================================================================
-- MAC 帧解析 (输入为 SOF 各物理块块体重组出的 MAC 帧 tvb)
-- =========================================================================
local function dissect_mac_frame(tvb, tree, pinfo, snid, sof_src_tei)
    local version = read_bits(tvb, 1, 2)   -- byte0 bits1-2: 1=标准帧, 2=单跳帧

    -- 单跳帧 (MSDU_BASE_S, 4B)
    if version == 2 then
        local mac_tree = tree:add(nw, tvb(), T("MAC 帧(单跳)", "MAC Frame (Single-hop)"))
        mac_tree:add(f.mac_version, tvb(0, 1))
        mac_tree:add(f.sh_type, tvb(1, 1))
        local msdu_len = read_bits(tvb, 16, 16)
        mac_tree:add(f.sh_len, tvb(2, 2), msdu_len)
        -- MSDU 载荷 + 帧尾 CRC32 (校验载荷, 不含单跳头)
        if msdu_len > 0 and 4 + msdu_len <= tvb:len() then
            add_span(mac_tree, f.sh_data, tvb, 4, msdu_len)
            local icv_off = 4 + msdu_len
            if icv_off + 4 <= tvb:len() then
                local icv_rx = read_bits(tvb, icv_off * 8, 32)
                local icv_calc = crc32_le(tvb, 4, msdu_len + 4)
                tree:add(f.mac_icv, tvb(icv_off, 4), icv_rx)
                tree:add(f.mac_icv_calc, tvb(icv_off, 4), icv_calc)
                tree:add(f.mac_icv_ok, tvb(icv_off, 1), icv_calc == icv_rx)
            end
        end
        return
    end

    if version ~= 1 then
        tree:add_expert_info(PI_MALFORMED, PI_WARN, T("未知 MAC 帧版本", "Unknown MAC frame version"))
        return
    end

    -- 标准帧 (MSDU_BASE): MACHeadFlag 决定长(32B)/短(12B)帧头
    local mac_tree = tree:add(nw, tvb(), T("MAC 帧", "MAC Frame"))
    local head_flag = read_bits(tvb, 0, 1)
    local msdu_len = read_bits(tvb, 16, 16)
    mac_tree:add(f.mac_version, tvb(0, 1))
    mac_tree:add(f.mac_head_flag, tvb(0, 1))
    add_val(mac_tree, f.mac_msdu_len, tvb, 2, 2, msdu_len)
    add_tei12(mac_tree, f.mac_dst_tei, tvb, 4)
    add_tei12h(mac_tree, f.mac_src_tei, tvb, 5)
    mac_tree:add(f.mac_restart_cnt, tvb(7, 1))
    mac_tree:add(f.mac_bcast_dir, tvb(8, 1))
    mac_tree:add(f.mac_send_type, tvb(9, 1))
    add_le(mac_tree, f.mac_msdu_seq, tvb, 10, 2)
    local hdr_len = 12
    if head_flag == 0 then
        mac_tree:add(f.mac_hdr_rsv, tvb(12, 20))
        hdr_len = 32
    end

    -- 方向判断 + 原始目标地址列: 源TEI=1(CCO) 且 目的TEI!=0xFFF → 下行; 否则上行
    -- 当前发出者 sof_src_tei != 源TEI → 中继 Relay
    local src_tei = read_bits(tvb, 5 * 8 + 4, 12)
    local dst_tei = read_bits(tvb, 4 * 8, 12)
    local ods
    if dst_tei == 0xFFF then
        ods = T("广播 (TEI 4095)", "Broadcast (TEI 4095)")
    elseif dst_tei == 1 then
        ods = T("CCO (TEI 1)", "CCO (TEI 1)")
    else
        ods = string.format(T("STA (TEI %d)", "STA (TEI %d)"), dst_tei)
    end
    local odmac = lookup_tei_mac(snid, dst_tei)
    if odmac then ods = ods .. " [" .. odmac .. "]" end
    tree:add(f.col_orig_dst, tvb(4, 2), ods)
    local dir_mark = ""
    if src_tei == 1 and dst_tei ~= 0xFFF then
        dir_mark = T(" ↓下行", " ↓Downlink")
    elseif src_tei ~= 1 then
        dir_mark = T(" ↑上行", " ↑Uplink")
    end
    if sof_src_tei and src_tei and sof_src_tei ~= src_tei then
        dir_mark = dir_mark .. " Relay"
    end
    if dir_mark ~= "" then
        pinfo.cols.info = tostring(pinfo.cols.info) .. dir_mark
    end

    -- MSDU 帧头
    local msdu_off = hdr_len
    local vlan, msdu_type
    if head_flag == 0 then
        -- 长帧头 MSDU_LONGHEAD (18B): 目的/源 MAC + VLAN(32b) + MSDU 类型(16b)
        if msdu_off + 18 > tvb:len() then return end
        mac_tree:add(f.msdu_dst_mac, tvb(msdu_off, 6))
        mac_tree:add(f.msdu_src_mac, tvb(msdu_off + 6, 6))
        vlan = read_bits(tvb, (msdu_off + 12) * 8, 32)
        add_val(mac_tree, f.msdu_vlan, tvb, msdu_off + 12, 4, vlan)
        msdu_type = read_bits(tvb, (msdu_off + 16) * 8, 16)
        add_val(mac_tree, f.msdu_type, tvb, msdu_off + 16, 2, msdu_type)
        msdu_off = msdu_off + 18
    else
        -- 短帧头 MSDU_SHORTHEAD (2B): VLAN(8b) + MSDU 类型(8b), 只携带应用层报文
        if msdu_off + 2 > tvb:len() then return end
        vlan = tvb(msdu_off, 1):uint()
        mac_tree:add(f.msdu_vlan_s, tvb(msdu_off, 1))
        msdu_type = tvb(msdu_off + 1, 1):uint()
        mac_tree:add(f.msdu_type_s, tvb(msdu_off + 1, 1))
        msdu_off = msdu_off + 2
    end

    -- MSDU 载荷: VLAN=0x8100 → MMe 管理消息; 否则 APP 应用层报文
    local payload_len = msdu_len - (msdu_off - hdr_len)
    if payload_len > 0 and msdu_off + payload_len <= tvb:len() then
        local body_tvb = view_slice(tvb, msdu_off)
        if vlan == 0x8100 then
            -- MMe: 头 6B (MMVersion + MMType 16b + RSV 24b)
            local mmt = mac_tree:add(nw, tvb(msdu_off, payload_len), T("管理消息(MMe)", "Management Message (MMe)"))
            if payload_len >= 6 then
                local mmtype = read_bits(body_tvb, 8, 16)
                mmt:add(f.mme_version, body_tvb(0, 1))
                mmt:add(f.mme_type, body_tvb(1, 2), mmtype)
                mmt:add(f.mme_rsv, body_tvb(3, 3))
                -- info 列追加管理消息类型
                local mt_name = mme_type_vals[mmtype] or ""
                local cur_info = tostring(pinfo.cols.info)
                if mt_name ~= "" then
                    pinfo.cols.info = cur_info .. string.format(" %s", mt_name)
                else
                    pinfo.cols.info = cur_info .. string.format(" MMType=0x%04x", mmtype)
                end
                -- 消息体 (MMe 头 6B 之后)
                if payload_len > 6 then
                    local ok, err = pcall(dissect_mme_body, body_tvb, mmt, mmtype)
                    if not ok then
                        mmt:add_expert_info(PI_MALFORMED, PI_WARN,
                            T("管理消息体解析失败(可能截断): ", "MMe body parse failed (possibly truncated): ") .. tostring(err))
                    end
                end
            end
        else
            local app_tree = mac_tree:add(nw, tvb(msdu_off, payload_len), T("应用层报文", "Application Layer Packet"))
            local ok, err = pcall(dissect_app, body_tvb, app_tree, pinfo)
            if not ok then
                app_tree:add_expert_info(PI_MALFORMED, PI_WARN,
                    T("应用层报文解析失败(可能截断): ", "APP parse failed (possibly truncated): ") .. tostring(err))
            end
        end
    end

    -- MSDU 帧尾 4B CRC32: 校验 MSDU 载荷(MAC 帧头之后, 不含帧头)
    local icv_off = hdr_len + msdu_len
    if msdu_len > 0 and icv_off + 4 <= tvb:len() then
        local icv_rx = read_bits(tvb, icv_off * 8, 32)
        local icv_calc = crc32_le(tvb, hdr_len, msdu_len + 4)
        tree:add(f.mac_icv, tvb(icv_off, 4), icv_rx)
        tree:add(f.mac_icv_calc, tvb(icv_off, 4), icv_calc)
        tree:add(f.mac_icv_ok, tvb(icv_off, 1), icv_calc == icv_rx)
    end
end

-- =========================================================================
-- 媒介类型 field extractor (必须在 dissector 注册前定义)
-- =========================================================================
local encap_type_f = Field.new("frame.encap_type")

function nw.dissector(tvb, pinfo, tree)
    -- 媒介判别: USER2(47)=载波 / USER3(48)=无线 / USER5(50)=串口混合采集(帧内媒介头)
    local ev = encap_type_f()
    local evs = ev and tostring(ev) or ""
    local is_rf = (evs == "48")
    if evs == "50" then
        -- USER5 混合格式: [phr_mcs][option][channel][isRF] + MPDU (载波/无线可混在一个抓包里,
        -- 信标/SOF 的载波与无线字段坐标不同, 逐帧按 isRF 自选)
        local mtree = tree:add(nw, tvb(0, 4), T("媒介头(串口采集)", "Media Header (serial capture)"))
        mtree:add(f.media_phr_mcs, tvb(0, 1))
        mtree:add(f.media_option, tvb(1, 1))
        mtree:add(f.media_channel, tvb(2, 1))
        mtree:add(f.media_is_rf, tvb(3, 1))
        is_rf = tvb(3, 1):uint() ~= 0
        tvb = tvb(4):tvb()
    end
    if is_rf then
        pinfo.cols.protocol = "NW-RF"
    else
        pinfo.cols.protocol = "NW-HPLC"
    end

    if tvb:len() < 16 then
        tree:add_expert_info(PI_MALFORMED, PI_WARN, T("帧长不足 16 字节(最小 MPDU 帧控制)", "Frame < 16 bytes (min FCH)"))
        return 0
    end

    local dt = band(tvb(0, 1):uint(), 0x07)
    local snid = read_bits(tvb, 4, 4)
    cur_snid = snid  -- 供 record_tei_mac 记录映射

    -- 源/目的地址列 (Source/Destination 列): 按帧类型提取 TEI
    local src_tei, dst_tei
    local dst_is_mac = false
    if dt == 0 then
        src_tei = read_bits(tvb, 9 * 8, 12)          -- 信标源TEI (FCH 字节9)
    elseif dt == 1 then
        src_tei = read_bits(tvb, 1 * 8, 12)          -- SOF 源TEI
        dst_tei = read_bits(tvb, 2 * 8 + 4, 12)      -- SOF 目的TEI
    elseif dt == 2 then
        local ext_type = read_bits(tvb, 12 * 8, 4)
        if ext_type == 0 then
            dst_tei = read_bits(tvb, 2 * 8, 12)      -- 常规 ACK 目的TEI
        elseif ext_type == 1 or ext_type == 3 then
            -- 搜索/切频帧: 目的为 48b MAC 地址, 直接显示
            dst_is_mac = true
            local mac = mac_to_str(tvb, 1)
            if mac then pinfo.cols.dst = mac end
        end
    elseif dt == 3 then
        src_tei = 1                                   -- 网间协调帧必然由 CCO 发出 (TEI 1)
    end
    -- 附加 MAC 地址 (历史帧已学到的 TEI↔MAC 映射)
    if src_tei then
        local base
        if src_tei == 1 then
            base = T("CCO (TEI 1)", "CCO (TEI 1)")
        else
            base = string.format(T("STA (TEI %d)", "STA (TEI %d)"), src_tei)
        end
        local mac = lookup_tei_mac(snid, src_tei)
        if mac then base = base .. " [" .. mac .. "]" end
        pinfo.cols.src = base
    else
        pinfo.cols.src = ""
    end
    if dst_tei then
        local base
        if dst_tei == 0xFFF then
            base = T("广播 (TEI 4095)", "Broadcast (TEI 4095)")
        else
            base = string.format("TEI %d", dst_tei)
        end
        local mac = lookup_tei_mac(snid, dst_tei)
        if mac then base = base .. " [" .. mac .. "]" end
        pinfo.cols.dst = base
    elseif not dst_is_mac then
        pinfo.cols.dst = ""
    end

    pinfo.cols.info = string.format("DT=%s SNID=0x%x", dt_vals[dt] or T("保留", "Reserved").."("..dt..")", snid)

    local root = tree:add(nw, tvb())

    -- MPDU 帧控制 16B
    local fc_tree = root:add(nw, tvb(0, 16), T("MPDU 帧控制 (FCH)", "MPDU Frame Control (FCH)"))
    fc_tree:add(f.fc_dt, tvb(0, 1))
    fc_tree:add(f.fc_conind, tvb(0, 1))
    fc_tree:add(f.fc_snid, tvb(0, 1))

    local vf_tree = fc_tree:add(f.fc_vf, tvb(1, 12))
    if dt == 0 then
        dissect_beacon_vf(tvb, vf_tree, is_rf)
    elseif dt == 1 then
        dissect_sof_vf(tvb, vf_tree, is_rf)
    elseif dt == 2 then
        dissect_ack_vf(tvb, vf_tree)
    elseif dt == 3 then
        dissect_coord_vf(tvb, vf_tree)
    end

    fc_tree:add(f.fc_std_ver, tvb(12, 1))
    -- FCCS CRC24: 校验 FCH 前 13 字节(0-12), 存储值字节 13-15
    local fccs_rx = tvb(13, 3):le_uint()
    local fccs_calc = crc24_lsb(tvb, 0, 16)
    fc_tree:add(f.fc_fccs, tvb(13, 3), fccs_rx)
    fc_tree:add(f.fc_fccs_calc, tvb(13, 3), fccs_calc)
    fc_tree:add(f.fc_fccs_ok, tvb(13, 1), fccs_calc == fccs_rx)

    -- 信标帧载荷 (dt=0): 物理块紧跟 FCH
    if dt == 0 and tvb:len() > 16 then
        local pbsize
        if is_rf then
            pbsize = rf_pb_size(read_bits(tvb, 11 * 8, 4))
        else
            pbsize = plc_pb_size(read_bits(tvb, 10 * 8 + 4, 4), 0)
        end
        if pbsize > 0 and 16 + pbsize <= tvb:len() then
            dissect_beacon_payload(tvb, root:add(nw, tvb(16, pbsize), T("信标帧载荷", "Beacon Payload")), 16, pbsize)
        else
            root:add_expert_info(PI_MALFORMED, PI_WARN, T("TMI/载荷PB大小 无效或帧长不足", "Invalid TMI/PBLen or frame too short"))
        end
    end

    -- SOF 帧载荷 (dt=1): 物理块 → 块体重组出 MAC 帧
    if dt == 1 and tvb:len() > 16 then
        local pb_num, pbsize
        if is_rf then
            pb_num = 1  -- 无线信道仅 1 个物理块
            pbsize = rf_pb_size(read_bits(tvb, 6 * 8 + 4, 4))
        else
            pb_num = read_bits(tvb, 7 * 8, 4)
            pbsize = plc_pb_size(read_bits(tvb, 7 * 8 + 4, 4), read_bits(tvb, 12 * 8, 4))
        end

        -- 逐 PB 块解析: 南网块 = PB头(4B) + 块体(pbsize-8) + 保留(1B) + CRC24(3B)
        -- MAC 帧统一用"块体逻辑视图"(单块/多块): 不生成新数据源, hex 始终完整显示原始帧
        if pbsize > 8 and pb_num >= 1 and pb_num <= 4 then
            local blocks_done = 0
            for i = 0, pb_num - 1 do
                local bstart = 16 + i * pbsize
                if bstart + pbsize > tvb:len() then break end
                local blk_tree = root:add(nw, tvb(bstart, pbsize),
                    string.format(T("物理块 %d", "PB %d"), i))
                blk_tree:add(f.pb_size, tvb(bstart, pbsize), pbsize)
                blk_tree:add(f.pb_hdr, tvb(bstart, 4))
                blk_tree:add(f.pb_body, tvb(bstart + 4, pbsize - 8))
                blk_tree:add(f.pb_rsv, tvb(bstart + pbsize - 4, 1))
                -- CRC24: 块尾 3 字节 (小端), 校验前 pbsize-3 字节 (PB头+块体+保留)
                local crc_rx = tvb(bstart + pbsize - 3, 3):le_uint()
                local crc_calc = crc24_lsb(tvb, bstart, pbsize)
                blk_tree:add(f.pb_pbcs, tvb(bstart + pbsize - 3, 3), crc_rx)
                blk_tree:add(f.pb_crc_calc, tvb(bstart + pbsize - 3, 3), crc_calc)
                blk_tree:add(f.pb_crc_ok, tvb(bstart + pbsize - 3, 1), crc_calc == crc_rx)
                blocks_done = blocks_done + 1
            end
            if blocks_done == pb_num then
                dissect_mac_frame(make_block_view(tvb, 20, pbsize, pb_num), root, pinfo, snid, src_tei)
            end
        else
            root:add_expert_info(PI_MALFORMED, PI_WARN, T("TMI/载荷PB大小 无效或 PB 个数越界", "Invalid TMI/PBLen or PB count out of range"))
        end
    end

    return tvb:len()
end

-- =========================================================================
-- 注册到 USER DLT (南网 NW_2021 专用, 与国网 GW_2022 的 USER0/USER1 区分):
--   USER2 (internal encap 47, pcap linktype 149) = 载波
--   USER3 (internal encap 48, pcap linktype 150) = 无线
-- =========================================================================
local wtap_encap = DissectorTable.get("wtap_encap")
wtap_encap:add(47, nw)
wtap_encap:add(48, nw)
-- USER5(50): 串口混合采集格式(媒介头+MPDU, 载波/无线混采), 见 bplc_serial_extcap.py
wtap_encap:add(50, nw)
