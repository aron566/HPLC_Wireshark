--[[
    packet-hplc_rf.lua
    双模通信(高速载波+无线) 数据链路层协议 dissector
    依据: 《双模通信互联互通技术规范 第4-2部分：数据链路层通信协议》2021-03-26

    ★ 字节序约定 (关键):
      - 字节内 bit0 = LSB
      - 跨字节 bit 字段(12bit TEI 等): little-endian bit 序, 用 read_bits()
      - 整字节多字节字段(16/24/32bit): little-endian, 用 le_uint()/le_uint24()
      - 时间戳(BTS/基准NTB)=NTB值, 25MHz时钟, 40ns/tick, 约171.8s回绕

    覆盖:
      - MPDU 帧控制 16B (5.1.2)                       [完整]
      - 信标帧载荷 + 信标管理信息条目 (5.1.2.4)        [完整]
      - SOF 帧: 物理块 + MAC帧 + 管理消息 (5.1.1/5.1.3)[完整]
      - SACK / 网间协调帧 (5.1.2)                     [完整]
      - 单跳帧 + 无线发现列表 (5.1.1.4/5.1.3.23)      [完整]
      - ICV / BPCS / FCCS CRC 字段                     [显示原始值]

    使用:
      tshark -r capture.pcap -X lua_script:packet-hplc_rf.lua -V
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
-- 与 Qt fieldspec.h crc24_lsb / Python MPDU_Class.cal_crc24 一致
-- 查找表法: 预计算 256 项, 每字节 O(1)
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
-- 与 Qt fieldspec.h crc32_le / Python cal_crc32 一致 (MSDU/信标载荷 ICV/BPCS 同算法)
-- 查找表法: 预计算 256 项, 每字节 O(1)
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

-- 语言开关: 读取 Wireshark 界面语言 (Edit→Preferences→Appearance→Language)
-- Wireshark 4.x 把界面语言存于 %APPDATA%\Wireshark\language 文件 (内容如 "language: en")
-- Lua pref 在脚本加载阶段恒为默认值, 无法用于决定字段名; 直接读该文件最可靠

-- 判断系统语言是否为中文: 优先 LANG 环境变量, 其次 Windows 注册表 (GUI 从桌面启动时无 LANG)
local function system_is_chinese()
    local lc = os.getenv("LANG") or os.getenv("LANGUAGE") or ""
    if lc:lower():match("^zh") then return true end
    -- Windows 注册表 LocaleName (如 zh-CN)
    local h = io.popen('reg query "HKCU\\Control Panel\\International" /v LocaleName 2>nul')
    if h then
        local out = h:read("*a")
        h:close()
        if out:lower():match("zh%-") then return true end
    end
    return false
end

local function detect_english()
    -- 1) 环境变量覆盖 (HPLC_RF_LANG=en/zh)
    local env = os.getenv("HPLC_RF_LANG")
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
            -- en / en_US 等 → 英文; zh / zh_CN 等 → 中文; system/auto 跟随系统
            if lang:lower():match("^zh") then return false end
            if lang:lower():match("^en") then return true end
            -- system/auto: 回退到系统语言
            return not system_is_chinese()
        end
    end
    -- 3) 回退: 系统语言
    return not system_is_chinese()
end

local hplc_english = detect_english()

-- 双语显示名: 按界面语言返回中文或英文
local function T(zh, en)
    if hplc_english then return en end
    return zh
end

local hplc = Proto("hplc_rf", T("双模通信 数据链路层协议", "Dual-Mode Communication Data Link Layer Protocol"))

-- =========================================================================
-- TEI ↔ MAC 映射表 (按 NID 分组)
-- 从历史帧学习: 关联确认/关联汇总/站点能力条目/发现列表 都携带 TEI+MAC
-- 之后在 Source/Destination 列附加显示 MAC 地址
-- =========================================================================
local tei_mac_map = {}   -- tei_mac_map[nid][tei] = "xx:xx:xx:xx:xx:xx"
local cur_nid = 0        -- 当前解析帧的 NID (主 dissector 设置)

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
    if not tei_mac_map[cur_nid] then tei_mac_map[cur_nid] = {} end
    tei_mac_map[cur_nid][tei] = mac_str
end

-- 查询 TEI → MAC, 无则返回 nil
local function lookup_tei_mac(nid, tei)
    local m = tei_mac_map[nid]
    if not m then return nil end
    return m[tei]
end

-- =========================================================================
-- value_string 表
-- =========================================================================
local dt_vals = {
    [0] = T("Beacon 信标帧", "Beacon"), [1] = T("SOF 数据帧", "SOF"),
    [2] = T("SACK 选择确认帧", "SACK"), [3] = T("网间协调帧", "Coordination"),
}
local network_type_vals = { [0] = T("用电信息采集系统", "Electricity usage information collection system") }
local std_ver_vals = { [0] = T("本标准", "This standard") }
local send_type_vals = {
    [0] = T("单播", "Unicast"), [1] = T("全网广播", "Full-network broadcast"), [2] = T("本地广播", "Local broadcast"), [3] = T("代理广播", "Proxy broadcast"),
}
local broadcast_dir_vals = {
    [0] = T("双向广播", "Bidirectional broadcast"), [1] = T("下行广播(CCO->STA)", "Downlink broadcast (CCO->STA)"), [2] = T("上行广播(STA->CCO)", "Uplink broadcast (STA->CCO)"),
}
local msdu_type_vals = {
    [0] = T("网络管理消息", "Network management message"), [48] = T("应用层报文", "Application layer packet"), [49] = T("IP 报文", "IP packet"),
}
local msg_type_vals = {
    [0] = T("发现列表消息", "Discovery list message"), [128] = T("应用层报文", "Application layer packet"), [129] = T("IPv4 报文", "IPv4 packet"),
}
-- 应用层报文 APP_BASE: 端口号 / 报文ID (依据 Qt 监控器 msduparser.cpp)
local app_port_vals = {
    [0x11] = T("管理/抄表端口", "Management/Meter-reading port"),
    [0x12] = T("升级端口", "Upgrade port"),
    [0x1A] = T("安全端口", "Security port"),
}
local app_packet_id_vals = {
    [0x0001] = T("终端主动抄表", "Terminal active meter-reading"),
    [0x0002] = T("路由主动抄表", "Routing active meter-reading"),
    [0x0003] = T("终端主动并发抄表", "Terminal active concurrent meter-reading"),
    [0x00B3] = T("终端主动并发抄表", "Terminal active concurrent meter-reading"),
    [0x0004] = T("校时", "Clock calibration"),
    [0x0006] = T("通信测试", "Communication test"),
    [0x0008] = T("事件上报", "Event report"),
    [0x0011] = T("查询从节点主动注册", "Query slave active registration"),
    [0x0012] = T("启动从节点主动注册", "Start slave active registration"),
    [0x0013] = T("停止从节点主动注册", "Stop slave active registration"),
    [0x0020] = T("确认/否认", "Ack/Nak"),
}
local beacon_type_vals = {
    [0] = T("发现信标", "Discovery Beacon"), [1] = T("代理信标", "Proxy Beacon"), [2] = T("中央信标", "Central Beacon"),
}
local link_type_vals = { [0] = T("高速载波链路", "PLC carrier link"), [1] = T("无线链路", "RF link") }
local phase_vals = { [0] = T("未知", "Unknown"), [1] = T("A相线", "Line A"), [2] = T("B相线", "Line B"), [3] = T("C相线", "Line C") }
local device_type_vals = {
    [1] = T("抄控器", "CCO device"), [2] = T("集中器本地通信单元", "Concentrator local comm unit"), [3] = T("电表通信单元", "Meter comm unit"),
    [4] = T("中继器", "Repeater"), [5] = T("II型采集器", "Type-II collector"), [6] = T("I型采集器单元", "Type-I collector unit"),
    [7] = T("三相电表通信单元", "Three-phase meter comm unit"),
}
local mac_addr_type_vals = {
    [0] = T("电能表地址作为入网MAC", "Meter address as MAC"), [1] = T("通信模块本身MAC作为入网MAC", "Module MAC as MAC"),
}
local module_type_vals = {
    [0] = T("高速载波单模", "PLC single-mode"), [1] = T("双模(载波+无线)", "Dual-mode (PLC+RF)"), [2] = T("无线单模", "RF single-mode"),
}
local assoc_result_vals = {
    [0x00] = T("关联请求成功", "Association succeeded"), [0x01] = T("站点不在白名单中", "STA not in whitelist"),
    [0x02] = T("站点在黑名单中", "STA in blacklist"), [0x03] = T("站点个数超过上限", "STA count over limit"),
    [0x04] = T("没有设置白名单列表", "No whitelist configured"), [0x05] = T("代理站点个数超过上限", "Proxy count over limit"),
    [0x06] = T("子站点个数超过上限", "Child STA count over limit"), [0x08] = T("重复的MAC地址", "Duplicate MAC address"),
    [0x09] = T("超过拓扑层级", "Exceeds topology level"), [0x0A] = T("站点再次关联请求入网成功", "STA re-association succeeded"),
    [0x0B] = T("新站点试图以自己子站点为代理入网", "New STA uses own child as proxy"), [0x0C] = T("组网拓扑中存在环路", "Loop in topology"),
    [0x0D] = T("CCO端未知原因出错", "CCO unknown error"), [0x0E] = T("无线代理达到上限", "RF proxy over limit"),
}
local carrier_band_vals = {
    [0] = T("1.953~11.96MHz", "1.953~11.96MHz"), [1] = T("2.441~5.615MHz", "2.441~5.615MHz"),
    [2] = T("0.781~2.930MHz", "0.781~2.930MHz"), [3] = T("1.758~2.930MHz", "1.758~2.930MHz"),
}
local route_type_vals = {
    [0] = T("错误", "Error"), [1] = T("同级路由", "Same-level route"), [2] = T("上级路由", "Upper-level route"),
    [3] = T("代理主路径路由", "Proxy main path route"), [4] = T("上上级路由", "Upper-upper-level route"),
}
local mgmt_type_vals = {
    [0x0000] = T("关联请求 AssocReq", "AssocReq"),
    [0x0001] = T("关联确认 AssocCnf", "AssocCnf"),
    [0x0002] = T("关联汇总指示 AssocGatherInd", "AssocGatherInd"),
    [0x0003] = T("代理变更请求 ChangeProxyReq", "ChangeProxyReq"),
    [0x0004] = T("代理变更确认 ChangeProxyCnf", "ChangeProxyCnf"),
    [0x0005] = T("代理变更确认(位图版) ChangeProxyBitMapCnf", "ChangeProxyBitMapCnf"),
    [0x0006] = T("离线指示 LeaveInd", "LeaveInd"),
    [0x0007] = T("心跳检测 HeartBeatCheck", "HeartBeatCheck"),
    [0x0008] = T("发现列表 DiscoverNodeList", "DiscoverNodeList"),
    [0x0009] = T("通信成功率上报 SuccessRateReport", "SuccessRateReport"),
    [0x000A] = T("网络冲突上报 NetworkConflictReport", "NetworkConflictReport"),
    [0x000B] = T("过零NTB采集指示 ZeroCrossNTBCollectInd", "ZeroCrossNTBCollectInd"),
    [0x000C] = T("过零NTB上报 ZeroCrossNTBReport", "ZeroCrossNTBReport"),
    [0x004F] = T("网络诊断报文 Diagnose", "Diagnose"),
    [0x0050] = T("路由请求 RouteRequest", "RouteRequest"),
    [0x0051] = T("路由回复 RouteReply", "RouteReply"),
    [0x0052] = T("路由错误 RouteError", "RouteError"),
    [0x0053] = T("路由应答 RouteAck", "RouteAck"),
    [0x0054] = T("链路确认请求 LinkConfirmRequest", "LinkConfirmRequest"),
    [0x0055] = T("链路确认回应 LinkConfirmResponse", "LinkConfirmResponse"),
    [0x0080] = T("无线信道冲突上报 RFChannelConflictReport", "RFChannelConflictReport"),
}
local beacon_entry_type_vals = {
    [0x00] = T("站点能力条目", "STA Capability Item"), [0x01] = T("路由参数条目", "Route Parameter Item"),
    [0x02] = T("频段变更条目", "Band Change Item"), [0x03] = T("无线路由参数条目", "RF Route Parameter Item"),
    [0x04] = T("无线信道变更条目", "RF Channel Change Item"), [0x05] = T("精简信标站点信息及时隙条目", "Lite STA Info & Slot Item"),
    [0xC0] = T("时隙分配条目", "Time Slot Allocation Item"),
}
local role_vals = {
    [0x0] = T("未知", "Unknown"), [0x1] = "STA", [0x2] = "PCO", [0x4] = "CCO",
}
local boot_reason_vals = {
    [0x0] = T("正常启动", "Normal boot"), [0x1] = T("断电重启", "Power-cycle reboot"), [0x2] = T("看门狗复位", "Watchdog reset"), [0x3] = T("程序指针异常", "Program counter fault"),
}
local chip_vendor_vals = {
    [0x0001] = "HS", [0x0002] = "ES", [0x0003] = "TC", [0x0004] = "LH",
    [0x0005] = "HT", [0x0006] = "RS", [0x0007] = "SW", [0x0008] = "SC",
    [0x0009] = "YM", [0x000A] = "QJ", [0x000B] = "HZ", [0x000C] = "ZC",
    [0x000D] = "SP", [0x000E] = "PE", [0x000F] = "NR", [0x0010] = "SL",
    [0x0011] = "MT", [0x0012] = "SI", [0x0013] = "RS", [0x0014] = "XY",
}
local ie_type_vals = {
    [0] = T("站点属性信息", "STA attribute info"), [1] = T("站点路由信息", "STA route info"),
    [2] = T("邻居节点信道信息(非位图版)", "Neighbor channel info (non-bitmap)"), [3] = T("邻居节点信道信息(位图版)", "Neighbor channel info (bitmap)"),
}
local len_type_vals = { [0] = T("1字节", "1 byte"), [1] = T("2字节", "2 bytes") }
local collect_site_vals = { [0] = T("指定单站点采集", "Single STA collect"), [1] = T("指定全网站点采集", "All STA collect") }
local collect_period_vals = { [0] = T("二分之一电力线周期", "Half power-line cycle"), [1] = T("一个电力线周期", "One power-line cycle") }
local payload_type_vals = { [0] = T("未携带负载数据", "No payload data"), [1] = T("传播路径列表", "Propagation path list") }
local leave_reason_vals = {
    [0] = T("CCO通知站点立即离线", "CCO orders immediate leave"), [1] = T("网络拓扑层级超过上限", "Topology level exceeds limit"),
    [2] = T("站点不在最新白名单中", "STA not in latest whitelist"),
}

-- =========================================================================
-- ProtoField 定义 (第一参数 = filter abbr)
-- =========================================================================
local f = {}

-- MPDU 帧控制 (表13)
f.fc_dt = ProtoField.uint8 ("hplc_rf.fc.dt", T("定界符类型", "Delimiter Type"), base.DEC, dt_vals, 0x07)
f.fc_net_type = ProtoField.uint8 ("hplc_rf.fc.net_type", T("网络类型", "Network Type"),   base.DEC, network_type_vals, 0xF8)
f.fc_nid = ProtoField.uint24 ("hplc_rf.fc.nid", T("网络标识(NID)", "Network ID (NID)"), base.HEX)
f.fc_vf = ProtoField.bytes ("hplc_rf.fc.vf", T("可变区域", "Variant Field"), base.NONE)
f.fc_std_ver = ProtoField.uint8 ("hplc_rf.fc.std_ver", T("标准版本号", "Standard Version"), base.DEC, std_ver_vals, 0xF0)
f.fc_fccs = ProtoField.uint24 ("hplc_rf.fc.fccs", T("帧控制校验序列(FCCS,CRC24)", "Frame Control Check Sequence (FCCS, CRC24)"), base.HEX)
f.fc_fccs_calc = ProtoField.uint24 ("hplc_rf.fc.fccs_calc", T("FCCS 计算值", "FCCS Calculated"), base.HEX)
f.fc_fccs_ok = ProtoField.bool ("hplc_rf.fc.fccs_ok", T("FCCS 校验通过", "FCCS Check Passed"), 8, nil, 0x01)

-- 物理块头 (表37)
f.pb_seq = ProtoField.uint8 ("hplc_rf.pb.seq", T("序列号", "Sequence Number"), base.DEC, nil, 0x3F)
f.pb_sof = ProtoField.bool ("hplc_rf.pb.sof", T("帧起始标志", "Start of Frame Flag"), 8, nil, 0x40)
f.pb_eof = ProtoField.bool ("hplc_rf.pb.eof", T("帧结束标志", "End of Frame Flag"), 8, nil, 0x80)
f.pb_pbcs = ProtoField.uint24 ("hplc_rf.pb.pbcs", T("物理块检查序列(PBCS,CRC24)", "PB Check Sequence (PBCS, CRC24)"), base.HEX)
f.pb_size = ProtoField.uint16 ("hplc_rf.pb.size", T("物理块大小(字节)", "PB Size (bytes)"), base.DEC)
f.pb_crc_ok = ProtoField.bool ("hplc_rf.pb.crc_ok", T("CRC24校验通过", "CRC24 Check Passed"), 8, nil, 0x01)
f.pb_crc_calc = ProtoField.uint24 ("hplc_rf.pb.crc_calc", T("CRC24计算值", "CRC24 Calculated"), base.HEX)
f.pb_body = ProtoField.bytes ("hplc_rf.pb.body", T("物理块体(PB Body)", "PB Body"), base.NONE)
f.pb_padding = ProtoField.bytes ("hplc_rf.pb.padding", T("填充(Padding)", "Padding"), base.NONE)

-- 信标帧可变区域 (表17 载波)
f.beacon_bts = ProtoField.uint32 ("hplc_rf.beacon.bts", T("信标时间戳(BTS,原始NTB)", "Beacon Timestamp (BTS, raw NTB)"), base.DEC)
f.beacon_bts_sec = ProtoField.double ("hplc_rf.beacon.bts_sec", T("信标时间戳(秒)", "Beacon Timestamp (seconds)"), base.DEC)
f.beacon_src_tei = ProtoField.uint16 ("hplc_rf.beacon.src_tei", T("源TEI", "Source TEI"), base.DEC)
f.beacon_div_mode = ProtoField.uint8 ("hplc_rf.beacon.div_mode", T("分集拷贝基本模式", "Diversity Copy Basic Mode"), base.DEC, nil, 0xF0)
f.beacon_symbol_cnt = ProtoField.uint16 ("hplc_rf.beacon.symbol_cnt", T("符号数", "Symbol Count"), base.DEC)
f.beacon_phase = ProtoField.uint8 ("hplc_rf.beacon.phase", T("相线", "Line"), base.DEC, phase_vals, 0x06)

-- 信标帧载荷 (表38/56)
f.beacon_type = ProtoField.uint8 ("hplc_rf.beacon.type", T("信标类型", "Beacon Type"), base.DEC, beacon_type_vals, 0x07)
f.beacon_net_cplt = ProtoField.bool ("hplc_rf.beacon.net_cplt", T("组网标志位", "Networking Flag"), 8, nil, 0x08)
f.beacon_simple = ProtoField.bool ("hplc_rf.beacon.simple", T("精简信标标志", "Lite Beacon Flag"), 8, nil, 0x10)
f.beacon_start_assoc = ProtoField.bool ("hplc_rf.beacon.start_assoc", T("开始关联标志", "Start Association Flag"), 8, nil, 0x40)
f.beacon_use_flag = ProtoField.bool ("hplc_rf.beacon.use_flag", T("信标使用标志", "Beacon Use Flag"), 8, nil, 0x80)
f.beacon_net_seq = ProtoField.uint8 ("hplc_rf.beacon.net_seq", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.beacon_cco_mac = ProtoField.ether ("hplc_rf.beacon.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.beacon_bpc = ProtoField.uint32 ("hplc_rf.beacon.bpc", T("信标周期计数(BPC)", "Beacon Period Count (BPC)"), base.DEC)
f.beacon_rf_ch = ProtoField.uint8 ("hplc_rf.beacon.rf_ch", T("本网络无线信道编号", "RF Channel Number"), base.DEC)
f.beacon_entry_cnt = ProtoField.uint8 ("hplc_rf.beacon.entry_cnt", T("信标条目数", "Beacon Item Count"), base.DEC)
f.beacon_bpcs = ProtoField.uint32 ("hplc_rf.beacon.bpcs", T("帧载荷校验序列(BPCS,CRC32)", "Beacon Payload Check Sequence (BPCS, CRC32)"), base.HEX)
f.beacon_bpcs_calc = ProtoField.uint32 ("hplc_rf.beacon.bpcs_calc", T("BPCS 计算值", "BPCS Calculated"), base.HEX)
f.beacon_bpcs_ok = ProtoField.bool ("hplc_rf.beacon.bpcs_ok", T("BPCS 校验通过", "BPCS Check Passed"), 8, nil, 0x01)

-- 信标管理信息条目 (表46-57)
f.beacon_ent_type = ProtoField.uint8 ("hplc_rf.beacon.ent.type", T("信标条目头", "Beacon Item Head"), base.HEX, beacon_entry_type_vals)
f.beacon_ent_len = ProtoField.uint16 ("hplc_rf.beacon.ent.len", T("信标条目长度", "Beacon Item Length"), base.DEC)
f.beacon_ent_data = ProtoField.bytes ("hplc_rf.beacon.ent.data", T("信标条目内容", "Beacon Item Content"), base.NONE)

-- 站点能力条目 (表47)
f.ent_tei = ProtoField.uint16 ("hplc_rf.beacon.cap.tei", T("TEI", "TEI"), base.DEC)
f.ent_proxy_tei = ProtoField.uint16 ("hplc_rf.beacon.cap.proxy_tei", T("代理站点TEI", "Proxy TEI"), base.DEC)
f.ent_path_rate = ProtoField.uint8 ("hplc_rf.beacon.cap.path_rate", T("路径最低通信成功率(%)", "Path Min Comm Rate (%)"), base.DEC)
f.ent_sta_mac = ProtoField.ether ("hplc_rf.beacon.cap.sta_mac", T("发送信标站点MAC", "Beacon TX STA MAC"), base.NONE)
f.ent_role = ProtoField.uint8 ("hplc_rf.beacon.cap.role", T("角色", "Role"), base.DEC, role_vals, 0x0F)
f.ent_level = ProtoField.uint8 ("hplc_rf.beacon.cap.level", T("层级数", "Level"), base.DEC, nil, 0xF0)
f.ent_proxy_qual = ProtoField.uint8 ("hplc_rf.beacon.cap.proxy_qual", T("代理站点信道质量(dB)", "Proxy Channel Quality (dB)"), base.DEC)
f.ent_phase = ProtoField.uint8 ("hplc_rf.beacon.cap.phase", T("相线", "Line"), base.DEC, phase_vals, 0x03)
f.ent_rf_hop = ProtoField.uint8 ("hplc_rf.beacon.cap.rf_hop", T("链路上RF跳数", "RF Hops on Path"), base.DEC, nil, 0x3C)

-- 路由参数条目 (表48)
f.ent_route_period = ProtoField.uint16 ("hplc_rf.beacon.rp.route_period", T("路由周期(秒)", "Route Period (s)"), base.DEC)
f.ent_route_remain = ProtoField.uint16 ("hplc_rf.beacon.rp.route_remain", T("路由评估剩余时间(秒)", "Route Estimate Remaining (s)"), base.DEC)
f.ent_proxy_dl_period = ProtoField.uint16 ("hplc_rf.beacon.rp.proxy_dl_period", T("代理站点发现列表周期(秒)", "Proxy Discovery List Period (s)"), base.DEC)
f.ent_disc_dl_period = ProtoField.uint16 ("hplc_rf.beacon.rp.disc_dl_period", T("发现站点发现列表周期(秒)", "STA Discovery List Period (s)"), base.DEC)

-- 频段变更条目 (表49)
f.ent_target_band = ProtoField.uint8 ("hplc_rf.beacon.bc.target_band", T("目标频段", "Target Band"), base.HEX)
f.ent_band_remain = ProtoField.uint32 ("hplc_rf.beacon.bc.band_remain", T("频段切换剩余时间(ms)", "Band Switch Remaining (ms)"), base.DEC)

-- 时隙分配条目 (表50)
f.ent_nc_beacon_cnt = ProtoField.uint8 ("hplc_rf.beacon.slot.nc_beacon_cnt", T("非中央信标时隙总数", "Non-CCO Beacon Slot Count"), base.DEC)
f.ent_c_beacon_cnt = ProtoField.uint8 ("hplc_rf.beacon.slot.c_beacon_cnt", T("中央信标时隙总数", "CCO Beacon Slot Count"), base.DEC, nil, 0x0F)
f.ent_csma_phase_cnt = ProtoField.uint8 ("hplc_rf.beacon.slot.csma_phase_cnt", T("CSMA时隙支持相线个数", "CSMA Slot Line Count"), base.DEC, nil, 0x30)
f.ent_proxy_beacon_cnt = ProtoField.uint8 ("hplc_rf.beacon.slot.proxy_beacon_cnt", T("代理信标时隙总数", "Proxy Beacon Slot Count"), base.DEC)
f.ent_beacon_slot_len = ProtoField.uint8 ("hplc_rf.beacon.slot.beacon_slot_len", T("信标时隙长度(ms)", "Beacon Slot Length (ms)"), base.DEC)
f.ent_csma_slice_len = ProtoField.uint8 ("hplc_rf.beacon.slot.csma_slice_len", T("CSMA时隙分片长度(10ms)", "CSMA Slot Slice Length (10ms)"), base.DEC)
f.ent_bcsma_phase_cnt = ProtoField.uint8 ("hplc_rf.beacon.slot.bcsma_phase_cnt", T("绑定CSMA时隙相线个数", "Binding CSMA Slot Line Count"), base.DEC)
f.ent_bcsma_lid = ProtoField.uint8 ("hplc_rf.beacon.slot.bcsma_lid", T("绑定CSMA时隙链路标识符", "Binding CSMA Slot LID"), base.DEC)
f.ent_tdma_slot_len = ProtoField.uint8 ("hplc_rf.beacon.slot.tdma_slot_len", T("TDMA时隙长度(ms)", "TDMA Slot Length (ms)"), base.DEC)
f.ent_tdma_lid = ProtoField.uint8 ("hplc_rf.beacon.slot.tdma_lid", T("TDMA时隙链路标识符", "TDMA Slot LID"), base.DEC)
f.ent_bp_start_ntb = ProtoField.uint32 ("hplc_rf.beacon.slot.bp_start_ntb", T("信标周期起始网络基准时", "Beacon Period Start NTB"), base.DEC)
f.ent_bp_len = ProtoField.uint32 ("hplc_rf.beacon.slot.bp_len", T("信标周期长度(ms)", "Beacon Period Length (ms)"), base.DEC)
f.ent_rf_beacon_len = ProtoField.uint16 ("hplc_rf.beacon.slot.rf_beacon_len", T("RF信标时隙长度(ms)", "RF Beacon Slot Length (ms)"), base.DEC)

-- 非中央信标信息 (表51)
f.ent_ncb_tei = ProtoField.uint16 ("hplc_rf.beacon.ncb.tei", T("TEI", "TEI"), base.DEC)
f.ent_ncb_type = ProtoField.uint8 ("hplc_rf.beacon.ncb.type", T("信标类型", "Beacon Type"), base.DEC, {[0]=T("发现信标", "Discovery Beacon"),[1]=T("代理信标", "Proxy Beacon")}, 0x10)
f.ent_ncb_rf_flag = ProtoField.uint8 ("hplc_rf.beacon.ncb.rf_flag", T("无线信标标志", "RF Beacon Flag"), base.DEC, nil, 0xE0)

-- CSMA时隙信息 (表52)
f.ent_csma_len = ProtoField.uint24 ("hplc_rf.beacon.csma.len", T("CSMA时隙长度(ms)", "CSMA Slot Length (ms)"), base.DEC)
f.ent_csma_phase = ProtoField.uint8 ("hplc_rf.beacon.csma.phase", T("CSMA时隙相线", "CSMA Slot Line"), base.DEC, phase_vals, 0x03)

-- 绑定CSMA时隙信息 (表53)
f.ent_bcsma_len = ProtoField.uint24 ("hplc_rf.beacon.bcsma.len", T("绑定CSMA时隙长度(ms)", "Binding CSMA Slot Length (ms)"), base.DEC)
f.ent_bcsma_phase = ProtoField.uint8 ("hplc_rf.beacon.bcsma.phase", T("绑定CSMA时隙相线", "Binding CSMA Slot Line"), base.DEC, phase_vals, 0x03)

-- 无线路由参数条目 (表54)
f.ent_rf_dl_period = ProtoField.uint8 ("hplc_rf.beacon.rfp.dl_period", T("无线发现列表周期(秒)", "RF Discovery List Period (s)"), base.DEC)
f.ent_rf_age_cnt = ProtoField.uint8 ("hplc_rf.beacon.rfp.age_cnt", T("无线接收率老化周期个数", "RF Rate Age Period Count"), base.DEC)

-- 无线信道变更条目 (表55)
f.ent_target_ch = ProtoField.uint8 ("hplc_rf.beacon.rcc.target_ch", T("目标信道", "Target Channel"), base.DEC)
f.ent_ch_remain = ProtoField.uint32 ("hplc_rf.beacon.rcc.ch_remain", T("信道切换剩余时间(ms)", "Channel Switch Remaining (ms)"), base.DEC)

-- 精简信标站点信息及时隙条目 (表57)
f.ent_sb_csma_start = ProtoField.uint32 ("hplc_rf.beacon.sb.csma_start", T("CSMA时隙开始时间(NTB)", "CSMA Slot Start Time (NTB)"), base.DEC)
f.ent_sb_csma_len = ProtoField.uint16 ("hplc_rf.beacon.sb.csma_len", T("CSMA时隙长度(ms)", "CSMA Slot Length (ms)"), base.DEC)

-- SOF 帧可变区域 (表19 载波 / 表30 无线)
f.sof_src_tei = ProtoField.uint16 ("hplc_rf.sof.src_tei", T("源TEI", "Source TEI"), base.DEC)
f.sof_dst_tei = ProtoField.uint16 ("hplc_rf.sof.dst_tei", T("目的TEI", "Destination TEI"), base.DEC)
f.sof_lid = ProtoField.uint8 ("hplc_rf.sof.lid", T("链路标识符(LID)", "Link Identifier (LID)"), base.DEC)
f.sof_frame_len = ProtoField.uint16 ("hplc_rf.sof.frame_len", T("帧长(×10us)", "Frame Length (×10us)"), base.DEC)
f.sof_pb_count = ProtoField.uint8 ("hplc_rf.sof.pb_count", T("物理块个数", "PB Count"), base.DEC, nil, 0xF0)
f.sof_symbol_cnt = ProtoField.uint16 ("hplc_rf.sof.symbol_cnt", T("符号数", "Symbol Count"), base.DEC)
f.sof_bcast = ProtoField.bool ("hplc_rf.sof.bcast", T("广播标志", "Broadcast Flag"), 8, nil, 0x02)
f.sof_retrans = ProtoField.bool ("hplc_rf.sof.retrans", T("重传标志", "Retransmit Flag"), 8, nil, 0x04)
f.sof_encrypt = ProtoField.bool ("hplc_rf.sof.encrypt", T("加密标志(预留)", "Encryption Flag (reserved)"), 8, nil, 0x08)
f.sof_div_mode = ProtoField.uint8 ("hplc_rf.sof.div_mode", T("分集拷贝基本模式", "Diversity Copy Basic Mode"), base.DEC, nil, 0xF0)
f.sof_div_ext = ProtoField.uint8 ("hplc_rf.sof.div_ext", T("分集拷贝扩展模式", "Diversity Copy Extended Mode"), base.DEC, nil, 0x0F)
f.sof_pb_size = ProtoField.uint8 ("hplc_rf.sof.pb_size", T("载荷PB块大小(无线)", "Payload PB Size (RF)"), base.DEC, nil, 0xF0)
f.sof_mcs = ProtoField.uint8 ("hplc_rf.sof.mcs", T("MCS(无线)", "MCS (RF)"), base.DEC, nil, 0x0F)

-- SACK 帧可变区域 (表23/34)
f.sack_recv_result = ProtoField.uint8 ("hplc_rf.sack.recv_result", T("接收结果", "Receive Result"), base.DEC, nil, 0x0F)
f.sack_recv_status = ProtoField.uint8 ("hplc_rf.sack.recv_status", T("接收状态", "Receive Status"), base.HEX, nil, 0xF0)
f.sack_src_tei = ProtoField.uint16 ("hplc_rf.sack.src_tei", T("源TEI", "Source TEI"), base.DEC)
f.sack_dst_tei = ProtoField.uint16 ("hplc_rf.sack.dst_tei", T("目的TEI", "Destination TEI"), base.DEC)
f.sack_pb_count = ProtoField.uint8 ("hplc_rf.sack.pb_count", T("接收物理块个数", "Received PB Count"), base.DEC, nil, 0x07)
f.sack_chan_qual = ProtoField.uint8 ("hplc_rf.sack.chan_qual", T("信道质量(SNR,dB)", "Channel Quality (SNR, dB)"), base.DEC)
f.sack_site_load = ProtoField.uint8 ("hplc_rf.sack.site_load", T("站点负载(缓存报文数)", "STA Load (buffered packets)"), base.DEC)
f.sack_ext_type = ProtoField.uint8 ("hplc_rf.sack.ext_type", T("扩展帧类型", "Extended Frame Type"), base.DEC, nil, 0x0F)

-- 网间协调帧可变区域 (表26)
f.coord_duration = ProtoField.uint16 ("hplc_rf.coord.duration", T("持续时间(1ms)", "Duration (1ms)"), base.DEC)
f.coord_bw_offset = ProtoField.uint16 ("hplc_rf.coord.bw_offset", T("带宽开始偏移(1ms)", "Bandwidth Start Offset (1ms)"), base.DEC)
f.coord_nbr_nid = ProtoField.uint24 ("hplc_rf.coord.nbr_nid", T("接收到的邻居网络号", "Received Neighbor Network ID"), base.HEX)
f.coord_rf_ch = ProtoField.uint8 ("hplc_rf.coord.rf_ch", T("本网络无线信道编号", "RF Channel Number"), base.DEC)

-- 标准 MAC 帧头 (表4)
f.mac_version = ProtoField.uint8 ("hplc_rf.mac.version", T("版本", "Version"), base.DEC, nil, 0x0F)
f.mac_ostei = ProtoField.uint16 ("hplc_rf.mac.ostei", T("原始源TEI", "Original Source TEI"), base.DEC)
f.mac_odtei = ProtoField.uint16 ("hplc_rf.mac.odtei", T("原始目的TEI", "Original Destination TEI"), base.DEC)
f.mac_send_type = ProtoField.uint8 ("hplc_rf.mac.send_type", T("发送类型", "Send Type"), base.DEC, send_type_vals, 0xF0)
f.mac_retry_limit = ProtoField.uint8 ("hplc_rf.mac.retry_limit", T("发送次数限值", "Retry Limit"), base.DEC, nil, 0x1F)
f.mac_rsv0 = ProtoField.uint8 ("hplc_rf.mac.rsv0", T("保留", "Reserved"), base.HEX, nil, 0xE0)
f.mac_msdu_seq = ProtoField.uint16 ("hplc_rf.mac.msdu_seq", T("MSDU序列号", "MSDU Sequence Number"), base.DEC)
f.mac_msdu_type = ProtoField.uint8 ("hplc_rf.mac.msdu_type", T("MSDU类型", "MSDU Type"), base.DEC, msdu_type_vals)
f.mac_msdu_len = ProtoField.uint16 ("hplc_rf.mac.msdu_len", T("MSDU长度", "MSDU Length"), base.DEC)
f.mac_restart_cnt = ProtoField.uint8 ("hplc_rf.mac.restart_cnt", T("重启次数", "Restart Count"), base.DEC, nil, 0x78)
f.mac_proxy_main = ProtoField.bool ("hplc_rf.mac.proxy_main", T("代理主路径标识", "Proxy Main Path Flag"), 8, nil, 0x80)
f.mac_route_total = ProtoField.uint8 ("hplc_rf.mac.route_total", T("路由总跳数", "Total Hops"), base.DEC, nil, 0x0F)
f.mac_route_left = ProtoField.uint8 ("hplc_rf.mac.route_left", T("路由剩余跳数", "Remaining Hops"), base.DEC, nil, 0xF0)
f.mac_bcast_dir = ProtoField.uint8 ("hplc_rf.mac.bcast_dir", T("广播方向", "Broadcast Direction"), base.DEC, broadcast_dir_vals, 0x03)
f.mac_path_repair = ProtoField.bool ("hplc_rf.mac.path_repair", T("路径修复标志", "Path Repair Flag"), 8, nil, 0x04)
f.mac_addr_flag = ProtoField.bool ("hplc_rf.mac.addr_flag", T("MAC地址标志", "MAC Address Flag"), 8, nil, 0x08)
f.mac_rsv1 = ProtoField.uint8 ("hplc_rf.mac.rsv1", T("保留", "Reserved"), base.HEX, nil, 0xF0)
f.mac_rsv2 = ProtoField.uint8 ("hplc_rf.mac.rsv2", T("保留", "Reserved"), base.HEX)
f.mac_net_seq = ProtoField.uint8 ("hplc_rf.mac.net_seq", T("组网序列号", "Networking Sequence Number"), base.DEC)
f.mac_rsv3 = ProtoField.uint8 ("hplc_rf.mac.rsv3", T("保留", "Reserved"), base.HEX)
f.mac_rsv4 = ProtoField.uint8 ("hplc_rf.mac.rsv4", T("保留", "Reserved"), base.HEX)
f.mac_osmac = ProtoField.ether ("hplc_rf.mac.osmac", T("原始源MAC地址", "Original Source MAC"))
f.mac_odmac = ProtoField.ether ("hplc_rf.mac.odmac", T("原始目的MAC地址", "Original Destination MAC"))
f.mac_icv = ProtoField.uint32 ("hplc_rf.mac.icv", T("完整性校验值(ICV,CRC32)", "Integrity Check Value (ICV, CRC32)"), base.HEX)
f.mac_icv_calc = ProtoField.uint32 ("hplc_rf.mac.icv_calc", T("ICV 计算值", "ICV Calculated"), base.HEX)
f.mac_icv_ok = ProtoField.bool ("hplc_rf.mac.icv_ok", T("ICV 校验通过", "ICV Check Passed"), 8, nil, 0x01)

-- 原始目标地址列 (自定义列引用, 显示 ODTEI + MAC 映射)
f.col_orig_dst = ProtoField.string ("hplc_rf.col_orig_dst", T("原始目标地址", "Original Destination Address"))

-- 通用保留字段 (各帧共用)
f.rsvd = ProtoField.uint8 ("hplc_rf.rsvd", T("保留", "Reserved"), base.HEX)
-- 信标帧可变区域保留位 (字节11 bit3-7)
f.beacon_vf_rsv = ProtoField.uint8 ("hplc_rf.beacon.vf.rsv", T("保留", "Reserved"), base.HEX, nil, 0xF8)
-- SACK 帧保留 (字节8 bit3-7 和 字节11)
f.sack_rsv = ProtoField.uint8 ("hplc_rf.sack.rsv", T("保留", "Reserved"), base.HEX)
-- 协调帧保留 (字节12 bit0-3)
f.coord_rsv = ProtoField.uint8 ("hplc_rf.coord.rsv", T("保留", "Reserved"), base.HEX, nil, 0x0F)

-- 单跳 MAC 帧头 (表11)
f.sh_version = ProtoField.uint8 ("hplc_rf.sh.version", T("版本", "Version"), base.DEC, nil, 0x0F)
f.sh_msgtype = ProtoField.uint8 ("hplc_rf.sh.msg_type", T("消息类型", "Message Type"), base.DEC, msg_type_vals)
f.sh_msdulen = ProtoField.uint16 ("hplc_rf.sh.msdu_len", T("MSDU长度", "MSDU Length"), base.DEC)

-- 管理消息头 (表58)
f.mgmt_mmtype = ProtoField.uint16 ("hplc_rf.mgmt.mmtype", T("管理消息类型(MMTYPE)", "Management Message Type (MMTYPE)"), base.HEX, mgmt_type_vals)
f.mgmt_resv = ProtoField.uint16 ("hplc_rf.mgmt.reserved", T("保留", "Reserved"), base.HEX)

-- 关联请求 (表60-68)
f.ar_sta_mac = ProtoField.ether ("hplc_rf.assoc_req.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ar_proxy_tei = ProtoField.uint16 ("hplc_rf.assoc_req.proxy_tei", T("候选代理TEI", "Candidate Proxy TEI"), base.DEC)
f.ar_link_type = ProtoField.uint8 ("hplc_rf.assoc_req.link_type", T("链路类型", "Link Type"), base.DEC, link_type_vals, 0x10)
f.ar_phase = ProtoField.uint8 ("hplc_rf.assoc_req.phase", T("相线", "Line"), base.DEC, phase_vals, 0x03)
f.ar_dev_type = ProtoField.uint8 ("hplc_rf.assoc_req.dev_type", T("设备类型", "Device Type"), base.DEC, device_type_vals)
f.ar_mac_type = ProtoField.uint8 ("hplc_rf.assoc_req.mac_type", T("MAC地址类型", "MAC Address Type"), base.DEC, mac_addr_type_vals)
f.ar_module = ProtoField.uint8 ("hplc_rf.assoc_req.module", T("模块类型", "Module Type"), base.DEC, module_type_vals, 0x03)
f.ar_assoc_rand = ProtoField.uint32 ("hplc_rf.assoc_req.assoc_rand", T("站点关联随机数", "Association Random Number"), base.HEX)
f.ar_vendor = ProtoField.bytes ("hplc_rf.assoc_req.vendor", T("厂家自定义信息", "Vendor Custom Info"), base.NONE)
f.ar_boot_reason = ProtoField.uint8 ("hplc_rf.assoc_req.boot_reason", T("系统启动原因", "Boot Reason"), base.DEC, boot_reason_vals)
f.ar_boot_ver = ProtoField.uint8 ("hplc_rf.assoc_req.boot_ver", T("BOOT版本号", "BOOT Version"), base.DEC)
f.ar_soft_ver = ProtoField.uint16 ("hplc_rf.assoc_req.soft_ver", T("软件版本号(BCD)", "Software Version (BCD)"), base.HEX)
f.ar_ver_time = ProtoField.uint16 ("hplc_rf.assoc_req.ver_time", T("版本时间(BIN)", "Version Time (BIN)"), base.HEX)
f.ar_vendor_code = ProtoField.uint16 ("hplc_rf.assoc_req.vendor_code", T("厂商代码(ASCII)", "Vendor Code (ASCII)"), base.DEC)
f.ar_chip_code = ProtoField.uint16 ("hplc_rf.assoc_req.chip_code", T("芯片代码(ASCII)", "Chip Code (ASCII)"), base.DEC)
f.ar_hard_rst = ProtoField.uint16 ("hplc_rf.assoc_req.hard_rst", T("硬复位累积次数", "Hard Reset Count"), base.DEC)
f.ar_soft_rst = ProtoField.uint16 ("hplc_rf.assoc_req.soft_rst", T("软复位累积次数", "Soft Reset Count"), base.DEC)
f.ar_proxy_type = ProtoField.uint8 ("hplc_rf.assoc_req.proxy_type", T("代理类型", "Proxy Type"), base.DEC, proxy_type_vals)
f.ar_e2e_seq = ProtoField.uint32 ("hplc_rf.assoc_req.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.ar_mgmt_id = ProtoField.bytes ("hplc_rf.assoc_req.mgmt_id", T("管理ID信息(24B)", "Management ID Info (24B)"), base.NONE)

-- 关联确认 (表70-75)
f.ac_sta_mac = ProtoField.ether ("hplc_rf.assoc_cnf.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ac_cco_mac = ProtoField.ether ("hplc_rf.assoc_cnf.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.ac_result = ProtoField.uint8 ("hplc_rf.assoc_cnf.result", T("结果", "Result"), base.HEX, assoc_result_vals)
f.ac_sta_level = ProtoField.uint8 ("hplc_rf.assoc_cnf.sta_level", T("站点层级", "STA Level"), base.DEC)
f.ac_sta_tei = ProtoField.uint16 ("hplc_rf.assoc_cnf.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.ac_link_type = ProtoField.uint8 ("hplc_rf.assoc_cnf.link_type", T("链路类型", "Link Type"), base.DEC, link_type_vals, 0x10)
f.ac_carrier_band = ProtoField.uint8 ("hplc_rf.assoc_cnf.carrier_band", T("载波频段", "Carrier Band"), base.DEC, carrier_band_vals, 0x60)
f.ac_proxy_tei = ProtoField.uint16 ("hplc_rf.assoc_cnf.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.ac_total_pkgs = ProtoField.uint8 ("hplc_rf.assoc_cnf.total_pkgs", T("总分包数", "Total Packages"), base.DEC)
f.ac_pkg_idx = ProtoField.uint8 ("hplc_rf.assoc_cnf.pkg_idx", T("分包序号", "Package Index"), base.DEC)
f.ac_assoc_rand = ProtoField.uint32 ("hplc_rf.assoc_cnf.assoc_rand", T("站点关联随机数", "Association Random Number"), base.HEX)
f.ac_reassoc_time = ProtoField.uint32 ("hplc_rf.assoc_cnf.reassoc_time", T("重新关联时间(ms)", "Re-association Time (ms)"), base.DEC)
f.ac_e2e_seq = ProtoField.uint32 ("hplc_rf.assoc_cnf.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.ac_path_seq = ProtoField.uint32 ("hplc_rf.assoc_cnf.path_seq", T("路径序号", "Path Sequence Number"), base.DEC)
f.ac_direct_sta = ProtoField.uint16 ("hplc_rf.assoc_cnf.direct_sta", T("直连站点数", "Direct STA Count"), base.DEC)
f.ac_direct_proxy = ProtoField.uint16 ("hplc_rf.assoc_cnf.direct_proxy", T("直连代理数", "Direct Proxy Count"), base.DEC)
f.ac_route_size = ProtoField.uint16 ("hplc_rf.assoc_cnf.route_size", T("路由表大小", "Route Table Size"), base.DEC)

-- 关联汇总指示 (表76-78)
f.ag_result = ProtoField.uint8 ("hplc_rf.assoc_gather.result", T("结果", "Result"), base.DEC)
f.ag_sta_level = ProtoField.uint8 ("hplc_rf.assoc_gather.sta_level", T("站点层级", "STA Level"), base.DEC)
f.ag_cco_mac = ProtoField.ether ("hplc_rf.assoc_gather.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.ag_proxy_tei = ProtoField.uint16 ("hplc_rf.assoc_gather.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.ag_carrier_band = ProtoField.uint8 ("hplc_rf.assoc_gather.carrier_band", T("载波频段", "Carrier Band"), base.DEC, carrier_band_vals, 0x30)
f.ag_gather_cnt = ProtoField.uint8 ("hplc_rf.assoc_gather.gather_cnt", T("汇总站点数", "Gathered STA Count"), base.DEC)
f.ag_sta_mac = ProtoField.ether ("hplc_rf.assoc_gather.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.ag_sta_tei = ProtoField.uint16 ("hplc_rf.assoc_gather.sta_tei", T("站点TEI", "STA TEI"), base.DEC)

-- 代理变更请求 (表79-83)
f.cpr_sta_tei = ProtoField.uint16 ("hplc_rf.cproxy_req.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.cpr_new_proxy = ProtoField.uint16 ("hplc_rf.cproxy_req.new_proxy", T("新代理TEI", "New Proxy TEI"), base.DEC)
f.cpr_link_type = ProtoField.uint8 ("hplc_rf.cproxy_req.link_type", T("链路类型", "Link Type"), base.DEC, link_type_vals, 0x10)
f.cpr_old_proxy = ProtoField.uint16 ("hplc_rf.cproxy_req.old_proxy", T("旧代理TEI", "Old Proxy TEI"), base.DEC)
f.cpr_proxy_type = ProtoField.uint8 ("hplc_rf.cproxy_req.proxy_type", T("代理类型", "Proxy Type"), base.DEC)
f.cpr_reason = ProtoField.uint8 ("hplc_rf.cproxy_req.reason", T("原因", "Reason"), base.DEC)
f.cpr_e2e_seq = ProtoField.uint32 ("hplc_rf.cproxy_req.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.cpr_phase = ProtoField.uint8 ("hplc_rf.cproxy_req.phase", T("站点相线", "STA Line"), base.DEC, phase_vals, 0x03)

-- 代理变更确认 (表84-87)
f.cpc_result = ProtoField.uint8 ("hplc_rf.cproxy_cnf.result", T("结果", "Result"), base.DEC)
f.cpc_total_pkgs = ProtoField.uint8 ("hplc_rf.cproxy_cnf.total_pkgs", T("总分包数", "Total Packages"), base.DEC)
f.cpc_pkg_idx = ProtoField.uint8 ("hplc_rf.cproxy_cnf.pkg_idx", T("分包序号", "Package Index"), base.DEC)
f.cpc_sta_tei = ProtoField.uint16 ("hplc_rf.cproxy_cnf.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.cpc_link_type = ProtoField.uint8 ("hplc_rf.cproxy_cnf.link_type", T("链路类型", "Link Type"), base.DEC, link_type_vals, 0x10)
f.cpc_proxy_tei = ProtoField.uint16 ("hplc_rf.cproxy_cnf.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.cpc_e2e_seq = ProtoField.uint32 ("hplc_rf.cproxy_cnf.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.cpc_path_seq = ProtoField.uint32 ("hplc_rf.cproxy_cnf.path_seq", T("路径序号", "Path Sequence Number"), base.DEC)
f.cpc_child_cnt = ProtoField.uint16 ("hplc_rf.cproxy_cnf.child_cnt", T("子站点数", "Child STA Count"), base.DEC)
f.cpc_child_tei = ProtoField.uint16 ("hplc_rf.cproxy_cnf.child_tei", T("子站点TEI", "Child STA TEI"), base.DEC)

-- 代理变更确认位图版 (表88-90)
f.cpb_result = ProtoField.uint8 ("hplc_rf.cproxy_bmp.result", T("结果", "Result"), base.DEC)
f.cpb_bitmap_size = ProtoField.uint16 ("hplc_rf.cproxy_bmp.bitmap_size", T("位图大小(字节)", "Bitmap Size (bytes)"), base.DEC)
f.cpb_sta_tei = ProtoField.uint16 ("hplc_rf.cproxy_bmp.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.cpb_link_type = ProtoField.uint8 ("hplc_rf.cproxy_bmp.link_type", T("链路类型", "Link Type"), base.DEC, link_type_vals, 0x10)
f.cpb_proxy_tei = ProtoField.uint16 ("hplc_rf.cproxy_bmp.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.cpb_e2e_seq = ProtoField.uint32 ("hplc_rf.cproxy_bmp.e2e_seq", T("端到端序列号", "End-to-End Sequence Number"), base.DEC)
f.cpb_path_seq = ProtoField.uint32 ("hplc_rf.cproxy_bmp.path_seq", T("路径序号", "Path Sequence Number"), base.DEC)
f.cpb_child_bmp = ProtoField.bytes ("hplc_rf.cproxy_bmp.child_bmp", T("子站点位图", "Child STA Bitmap"), base.NONE)

-- 离线指示 (表91-93)
f.li_reason = ProtoField.uint16 ("hplc_rf.leave.reason", T("原因", "Reason"), base.DEC, leave_reason_vals)
f.li_sta_cnt = ProtoField.uint16 ("hplc_rf.leave.sta_cnt", T("站点总数", "STA Count"), base.DEC)
f.li_delay = ProtoField.uint16 ("hplc_rf.leave.delay", T("延迟时间(秒)", "Delay Time (s)"), base.DEC)
f.li_sta_mac = ProtoField.ether ("hplc_rf.leave.sta_mac", T("站点MAC地址", "STA MAC Address"))

-- 心跳检测 (表94)
f.hb_ostei = ProtoField.uint16 ("hplc_rf.hb.ostei", T("原始源TEI", "Original Source TEI"), base.DEC)
f.hb_max_disc_tei = ProtoField.uint16 ("hplc_rf.hb.max_disc_tei", T("发现站点数最大的站点TEI", "Max Discovery STA TEI"), base.DEC)
f.hb_max_disc_cnt = ProtoField.uint16 ("hplc_rf.hb.max_disc_cnt", T("最大的发现站点数", "Max Discovery STA Count"), base.DEC)
f.hb_bitmap_size = ProtoField.uint16 ("hplc_rf.hb.bitmap_size", T("位图大小(字节)", "Bitmap Size (bytes)"), base.DEC)
f.hb_disc_bmp = ProtoField.bytes ("hplc_rf.hb.disc_bmp", T("发现站点位图", "Discovery STA Bitmap"), base.NONE)

-- 发现列表 (表95-99)
f.dl_tei = ProtoField.uint16 ("hplc_rf.dl.tei", T("TEI", "TEI"), base.DEC)
f.dl_proxy_tei = ProtoField.uint16 ("hplc_rf.dl.proxy_tei", T("代理TEI", "Proxy TEI"), base.DEC)
f.dl_role = ProtoField.uint8 ("hplc_rf.dl.role", T("角色", "Role"), base.DEC, role_vals, 0x0F)
f.dl_level = ProtoField.uint8 ("hplc_rf.dl.level", T("层级", "Level"), base.DEC, nil, 0xF0)
f.dl_mac = ProtoField.ether ("hplc_rf.dl.mac", T("MAC地址", "MAC Address"))
f.dl_cco_mac = ProtoField.ether ("hplc_rf.dl.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.dl_phase = ProtoField.uint8 ("hplc_rf.dl.phase", T("相线", "Line"), base.DEC, nil, 0x3F)
f.dl_proxy_qual = ProtoField.uint8 ("hplc_rf.dl.proxy_qual", T("代理站点信道质量(dB)", "Proxy Channel Quality (dB)"), base.DEC)
f.dl_proxy_rate = ProtoField.uint8 ("hplc_rf.dl.proxy_rate", T("代理站点通信成功率(%)", "Proxy Comm Rate (%)"), base.DEC)
f.dl_proxy_dl_rate = ProtoField.uint8 ("hplc_rf.dl.proxy_dl_rate", T("代理站点下行通信成功率(%)", "Proxy Downlink Comm Rate (%)"), base.DEC)
f.dl_sta_cnt = ProtoField.uint16 ("hplc_rf.dl.sta_cnt", T("站点总数", "STA Count"), base.DEC)
f.dl_send_cnt = ProtoField.uint8 ("hplc_rf.dl.send_cnt", T("发送发现列表报文个数", "Discovery List TX Count"), base.DEC)
f.dl_up_route_cnt = ProtoField.uint8 ("hplc_rf.dl.up_route_cnt", T("上行路由条目总数", "Uplink Route Entry Count"), base.DEC)
f.dl_route_remain = ProtoField.uint16 ("hplc_rf.dl.route_remain", T("路由周期到期剩余时间(秒)", "Route Period Remaining (s)"), base.DEC)
f.dl_bitmap_size = ProtoField.uint16 ("hplc_rf.dl.bitmap_size", T("位图大小(字节)", "Bitmap Size (bytes)"), base.DEC)
f.dl_min_rate = ProtoField.uint8 ("hplc_rf.dl.min_rate", T("最小通信成功率(%)", "Min Comm Rate (%)"), base.DEC)
f.dl_next_hop_tei = ProtoField.uint16 ("hplc_rf.dl.next_hop_tei", T("下一跳站点TEI", "Next Hop STA TEI"), base.DEC)
f.dl_route_type = ProtoField.uint8 ("hplc_rf.dl.route_type", T("路由类型", "Route Type"), base.DEC, route_type_vals, 0xF0)
f.dl_disc_bmp = ProtoField.bytes ("hplc_rf.dl.disc_bmp", T("发现站点列表位图", "Discovery STA List Bitmap"), base.NONE)
f.dl_rcv_cnt = ProtoField.uint8 ("hplc_rf.dl.rcv_cnt", T("接收发现列表数", "Received Discovery List Count"), base.DEC)
f.dl_rcv_item = ProtoField.string ("hplc_rf.dl.rcv_item", T("接收发现列表数(按TEI)", "Received Discovery List Count (by TEI)"))

-- 通信成功率上报 (表100-101)
f.sr_tei = ProtoField.uint16 ("hplc_rf.sr.tei", T("TEI", "TEI"), base.DEC)
f.sr_sta_cnt = ProtoField.uint16 ("hplc_rf.sr.sta_cnt", T("站点总数", "STA Count"), base.DEC)
f.sr_sta_tei = ProtoField.uint16 ("hplc_rf.sr.sta_tei", T("站点TEI", "STA TEI"), base.DEC)
f.sr_down_rate = ProtoField.uint8 ("hplc_rf.sr.down_rate", T("下行通信成功率(%)", "Downlink Comm Rate (%)"), base.DEC)
f.sr_up_rate = ProtoField.uint8 ("hplc_rf.sr.up_rate", T("上行通信成功率(%)", "Uplink Comm Rate (%)"), base.DEC)

-- 网络冲突上报 (表102-103)
f.ncr_cco_mac = ProtoField.ether ("hplc_rf.ncr.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.ncr_nbr_cnt = ProtoField.uint8 ("hplc_rf.ncr.nbr_cnt", T("邻居网络个数", "Neighbor Network Count"), base.DEC)
f.ncr_nid_width = ProtoField.uint8 ("hplc_rf.ncr.nid_width", T("网络号字节宽度", "Network ID Byte Width"), base.DEC)
f.ncr_nbr_nid = ProtoField.uint24 ("hplc_rf.ncr.nbr_nid", T("邻居网络号", "Neighbor Network ID"), base.HEX)

-- 过零NTB采集指示 (表104-106)
f.zc_tei = ProtoField.uint16 ("hplc_rf.zc.tei", T("TEI", "TEI"), base.DEC)
f.zc_collect_site = ProtoField.uint8 ("hplc_rf.zc.collect_site", T("采集站点", "Collect Site"), base.DEC, collect_site_vals)
f.zc_collect_period = ProtoField.uint8 ("hplc_rf.zc.collect_period", T("采集周期", "Collect Period"), base.DEC, collect_period_vals)
f.zc_collect_cnt = ProtoField.uint8 ("hplc_rf.zc.collect_cnt", T("采集数量", "Collect Count"), base.DEC)

-- 过零NTB上报 (表107-108)
f.zr_tei = ProtoField.uint16 ("hplc_rf.zr.tei", T("TEI", "TEI"), base.DEC)
f.zr_total_cnt = ProtoField.uint8 ("hplc_rf.zr.total_cnt", T("告知总数量", "Report Total Count"), base.DEC)
f.zr_ph1_cnt = ProtoField.uint8 ("hplc_rf.zr.ph1_cnt", T("相线1差值告知数量", "Line 1 Diff Report Count"), base.DEC)
f.zr_ph2_cnt = ProtoField.uint8 ("hplc_rf.zr.ph2_cnt", T("相线2差值告知数量", "Line 2 Diff Report Count"), base.DEC)
f.zr_ph3_cnt = ProtoField.uint8 ("hplc_rf.zr.ph3_cnt", T("相线3差值告知数量", "Line 3 Diff Report Count"), base.DEC)
f.zr_base_ntb = ProtoField.uint32 ("hplc_rf.zr.base_ntb", T("基准NTB", "Base NTB"), base.DEC)
f.zr_ph1_diff = ProtoField.uint16 ("hplc_rf.zr.ph1_diff", T("相线1过零NTB差值", "Line 1 Zero-Cross NTB Diff"), base.DEC)
f.zr_ph2_diff = ProtoField.uint16 ("hplc_rf.zr.ph2_diff", T("相线2过零NTB差值", "Line 2 Zero-Cross NTB Diff"), base.DEC)
f.zr_ph3_diff = ProtoField.uint16 ("hplc_rf.zr.ph3_diff", T("相线3过零NTB差值", "Line 3 Zero-Cross NTB Diff"), base.DEC)

-- 网络诊断 (表109-110)
f.diag_vendor_id = ProtoField.uint16 ("hplc_rf.diag.vendor_id", T("芯片厂商ID", "Chip Vendor ID"), base.HEX, chip_vendor_vals)
f.diag_custom = ProtoField.bytes ("hplc_rf.diag.custom", T("厂家自定义", "Vendor Custom"), base.NONE)

-- 路由请求 (表111-113)
f.rreq_version = ProtoField.uint8 ("hplc_rf.rreq.version", T("版本", "Version"), base.DEC)
f.rreq_seq = ProtoField.uint32 ("hplc_rf.rreq.seq", T("路由请求序列号", "Route Request Sequence Number"), base.HEX)
f.rreq_path_pref = ProtoField.bool ("hplc_rf.rreq.path_pref", T("路径优选标志", "Path Preference Flag"), 8, nil, 0x10)
f.rreq_payload_type = ProtoField.uint8 ("hplc_rf.rreq.payload_type", T("负载数据类型", "Payload Data Type"), base.DEC, payload_type_vals, 0x0F)
f.rreq_payload_len = ProtoField.uint8 ("hplc_rf.rreq.payload_len", T("负载数据长度", "Payload Data Length"), base.DEC)

-- 路由回复 (表114-116)
f.rrep_version = ProtoField.uint8 ("hplc_rf.rrep.version", T("版本", "Version"), base.DEC)
f.rrep_seq = ProtoField.uint32 ("hplc_rf.rrep.seq", T("路由请求序列号", "Route Request Sequence Number"), base.HEX)
f.rrep_payload_type = ProtoField.uint8 ("hplc_rf.rrep.payload_type", T("负载数据类型", "Payload Data Type"), base.DEC, payload_type_vals, 0x0F)
f.rrep_payload_len = ProtoField.uint8 ("hplc_rf.rrep.payload_len", T("负载数据长度", "Payload Data Length"), base.DEC)

-- 路由错误 (表117)
f.rerr_version = ProtoField.uint8 ("hplc_rf.rerr.version", T("版本", "Version"), base.DEC)
f.rerr_seq = ProtoField.uint32 ("hplc_rf.rerr.seq", T("路由请求序列号", "Route Request Sequence Number"), base.HEX)
f.rerr_unreach_cnt = ProtoField.uint8 ("hplc_rf.rerr.unreach_cnt", T("不可达站点数量", "Unreachable STA Count"), base.DEC)
f.rerr_unreach_tei = ProtoField.uint16 ("hplc_rf.rerr.unreach_tei", T("不可达站点TEI", "Unreachable STA TEI"), base.DEC)

-- 路由应答 (表118)
f.rack_version = ProtoField.uint8 ("hplc_rf.rack.version", T("版本", "Version"), base.DEC)
f.rack_seq = ProtoField.uint32 ("hplc_rf.rack.seq", T("路由请求序列号", "Route Request Sequence Number"), base.HEX)

-- 链路确认请求 (表119)
f.lcreq_version = ProtoField.uint8 ("hplc_rf.lcreq.version", T("版本", "Version"), base.DEC)
f.lcreq_seq = ProtoField.uint32 ("hplc_rf.lcreq.seq", T("路由请求序列号", "Route Request Sequence Number"), base.HEX)
f.lcreq_sta_cnt = ProtoField.uint8 ("hplc_rf.lcreq.sta_cnt", T("确认站点数量", "Confirm STA Count"), base.DEC)
f.lcreq_sta_tei = ProtoField.uint16 ("hplc_rf.lcreq.sta_tei", T("确认站点TEI", "Confirm STA TEI"), base.DEC)

-- 链路确认回应 (表120)
f.lcrsp_version = ProtoField.uint8 ("hplc_rf.lcrsp.version", T("版本", "Version"), base.DEC)
f.lcrsp_level = ProtoField.uint8 ("hplc_rf.lcrsp.level", T("层级", "Level"), base.DEC)
f.lcrsp_chan_qual = ProtoField.uint8 ("hplc_rf.lcrsp.chan_qual", T("信道质量", "Channel Quality"), base.DEC)
f.lcrsp_path_pref = ProtoField.bool ("hplc_rf.lcrsp.path_pref", T("路径优选标志", "Path Preference Flag"), 8, nil, 0x01)
f.lcrsp_seq = ProtoField.uint32 ("hplc_rf.lcrsp.seq", T("路由请求序列号", "Route Request Sequence Number"), base.HEX)

-- 无线信道冲突上报 (表121-122)
f.rfccr_cco_mac = ProtoField.ether ("hplc_rf.rfccr.cco_mac", T("CCO MAC地址", "CCO MAC Address"))
f.rfccr_nbr_cnt = ProtoField.uint8 ("hplc_rf.rfccr.nbr_cnt", T("邻居网络个数", "Neighbor Network Count"), base.DEC)
f.rfccr_nbr_ch = ProtoField.uint8 ("hplc_rf.rfccr.nbr_ch", T("邻居网络无线信道号", "Neighbor Network RF Channel"), base.DEC)

-- 无线发现列表 (表123-137)
f.rfdl_sta_mac = ProtoField.ether ("hplc_rf.rfdl.sta_mac", T("站点MAC地址", "STA MAC Address"))
f.rfdl_seq = ProtoField.uint8 ("hplc_rf.rfdl.seq", T("统计序号", "Statistics Sequence Number"), base.DEC)
f.rfdl_ie_type = ProtoField.uint8 ("hplc_rf.rfdl.ie.type", T("信息单元类型", "Information Element Type"), base.DEC, ie_type_vals, 0x7F)
f.rfdl_ie_ltype = ProtoField.uint8 ("hplc_rf.rfdl.ie.ltype", T("长度类型", "Length Type"), base.DEC, len_type_vals, 0x80)
f.rfdl_ie_len = ProtoField.uint16 ("hplc_rf.rfdl.ie.len", T("信息单元长度", "Information Element Length"), base.DEC)
f.rfdl_ie_data = ProtoField.bytes ("hplc_rf.rfdl.ie.data", T("信息单元内容", "Information Element Content"), base.NONE)

-- 应用层报文 APP_BASE (msdu_type=48, 表 报文ID)
f.app_port = ProtoField.uint8 ("hplc_rf.app.port", T("端口号", "Port Number"), base.HEX, app_port_vals)
f.app_packet_id = ProtoField.uint16 ("hplc_rf.app.packet_id", T("报文ID", "Packet ID"), base.HEX, app_packet_id_vals)
f.app_ctrl_word = ProtoField.uint8 ("hplc_rf.app.ctrl_word", T("报文控制字", "Packet Control Word"), base.HEX)
f.app_payload = ProtoField.bytes ("hplc_rf.app.payload", T("应用层载荷(Payload)", "Application Payload"), base.NONE)

-- 位图逐字节解析节点 (string, 承载 "值 (TEI列表)" 文本)
f.bmp_byte = ProtoField.string ("hplc_rf.bmp.byte", T("位图字节", "Bitmap Byte"))

hplc.fields = {
    f.fc_dt, f.fc_net_type, f.fc_nid, f.fc_vf, f.fc_std_ver, f.fc_fccs,
    f.fc_fccs_calc, f.fc_fccs_ok,
    f.pb_seq, f.pb_sof, f.pb_eof, f.pb_pbcs, f.pb_size, f.pb_crc_ok, f.pb_crc_calc,
    f.pb_body, f.pb_padding,
    f.beacon_bts, f.beacon_bts_sec, f.beacon_src_tei, f.beacon_div_mode,
    f.beacon_symbol_cnt, f.beacon_phase, f.beacon_type, f.beacon_net_cplt,
    f.beacon_simple, f.beacon_start_assoc, f.beacon_use_flag, f.beacon_net_seq,
    f.beacon_cco_mac, f.beacon_bpc, f.beacon_rf_ch, f.beacon_entry_cnt, f.beacon_bpcs,
    f.beacon_bpcs_calc, f.beacon_bpcs_ok,
    f.beacon_ent_type, f.beacon_ent_len, f.beacon_ent_data,
    f.ent_tei, f.ent_proxy_tei, f.ent_path_rate, f.ent_sta_mac, f.ent_role,
    f.ent_level, f.ent_proxy_qual, f.ent_phase, f.ent_rf_hop,
    f.ent_route_period, f.ent_route_remain, f.ent_proxy_dl_period, f.ent_disc_dl_period,
    f.ent_target_band, f.ent_band_remain,
    f.ent_nc_beacon_cnt, f.ent_c_beacon_cnt, f.ent_csma_phase_cnt,
    f.ent_proxy_beacon_cnt, f.ent_beacon_slot_len, f.ent_csma_slice_len,
    f.ent_bcsma_phase_cnt, f.ent_bcsma_lid, f.ent_tdma_slot_len, f.ent_tdma_lid,
    f.ent_bp_start_ntb, f.ent_bp_len, f.ent_rf_beacon_len,
    f.ent_ncb_tei, f.ent_ncb_type, f.ent_ncb_rf_flag,
    f.ent_csma_len, f.ent_csma_phase, f.ent_bcsma_len, f.ent_bcsma_phase,
    f.ent_rf_dl_period, f.ent_rf_age_cnt, f.ent_target_ch, f.ent_ch_remain,
    f.ent_sb_csma_start, f.ent_sb_csma_len,
    f.sof_src_tei, f.sof_dst_tei, f.sof_lid, f.sof_frame_len, f.sof_pb_count,
    f.sof_symbol_cnt, f.sof_bcast, f.sof_retrans, f.sof_encrypt,
    f.sof_div_mode, f.sof_div_ext, f.sof_pb_size, f.sof_mcs,
    f.sack_recv_result, f.sack_recv_status, f.sack_src_tei, f.sack_dst_tei,
    f.sack_pb_count, f.sack_chan_qual, f.sack_site_load, f.sack_ext_type,
    f.coord_duration, f.coord_bw_offset, f.coord_nbr_nid, f.coord_rf_ch,
    f.mac_version, f.mac_ostei, f.mac_odtei, f.mac_send_type,
    f.mac_retry_limit, f.mac_rsv0, f.mac_msdu_seq, f.mac_msdu_type, f.mac_msdu_len,
    f.mac_restart_cnt, f.mac_proxy_main, f.mac_route_total, f.mac_route_left,
    f.mac_bcast_dir, f.mac_path_repair, f.mac_addr_flag, f.mac_rsv1, f.mac_rsv2, f.mac_net_seq,
    f.mac_rsv3, f.mac_rsv4, f.mac_osmac, f.mac_odmac, f.mac_icv,
    f.mac_icv_calc, f.mac_icv_ok,
    f.col_orig_dst,
    f.rsvd, f.beacon_vf_rsv, f.sack_rsv, f.coord_rsv,
    f.sh_version, f.sh_msgtype, f.sh_msdulen,
    f.mgmt_mmtype, f.mgmt_resv,
    f.ar_sta_mac, f.ar_proxy_tei, f.ar_link_type, f.ar_phase, f.ar_dev_type,
    f.ar_mac_type, f.ar_module, f.ar_assoc_rand, f.ar_vendor,
    f.ar_boot_reason, f.ar_boot_ver, f.ar_soft_ver, f.ar_ver_time,
    f.ar_vendor_code, f.ar_chip_code, f.ar_hard_rst, f.ar_soft_rst,
    f.ar_proxy_type, f.ar_e2e_seq, f.ar_mgmt_id,
    f.ac_sta_mac, f.ac_cco_mac, f.ac_result, f.ac_sta_level, f.ac_sta_tei,
    f.ac_link_type, f.ac_carrier_band, f.ac_proxy_tei, f.ac_total_pkgs,
    f.ac_pkg_idx, f.ac_assoc_rand, f.ac_reassoc_time, f.ac_e2e_seq,
    f.ac_path_seq, f.ac_direct_sta, f.ac_direct_proxy, f.ac_route_size,
    f.ag_result, f.ag_sta_level, f.ag_cco_mac, f.ag_proxy_tei,
    f.ag_carrier_band, f.ag_gather_cnt, f.ag_sta_mac, f.ag_sta_tei,
    f.cpr_sta_tei, f.cpr_new_proxy, f.cpr_link_type, f.cpr_old_proxy,
    f.cpr_proxy_type, f.cpr_reason, f.cpr_e2e_seq, f.cpr_phase,
    f.cpc_result, f.cpc_total_pkgs, f.cpc_pkg_idx, f.cpc_sta_tei,
    f.cpc_link_type, f.cpc_proxy_tei, f.cpc_e2e_seq, f.cpc_path_seq,
    f.cpc_child_cnt, f.cpc_child_tei,
    f.cpb_result, f.cpb_bitmap_size, f.cpb_sta_tei, f.cpb_link_type,
    f.cpb_proxy_tei, f.cpb_e2e_seq, f.cpb_path_seq, f.cpb_child_bmp,
    f.li_reason, f.li_sta_cnt, f.li_delay, f.li_sta_mac,
    f.hb_ostei, f.hb_max_disc_tei, f.hb_max_disc_cnt, f.hb_bitmap_size, f.hb_disc_bmp,
    f.dl_tei, f.dl_proxy_tei, f.dl_role, f.dl_level, f.dl_mac, f.dl_cco_mac,
    f.dl_phase, f.dl_proxy_qual, f.dl_proxy_rate, f.dl_proxy_dl_rate,
    f.dl_sta_cnt, f.dl_send_cnt, f.dl_up_route_cnt, f.dl_route_remain,
    f.dl_bitmap_size, f.dl_min_rate, f.dl_next_hop_tei, f.dl_route_type, f.dl_disc_bmp,
    f.dl_rcv_cnt, f.dl_rcv_item,
    f.sr_tei, f.sr_sta_cnt, f.sr_sta_tei, f.sr_down_rate, f.sr_up_rate,
    f.ncr_cco_mac, f.ncr_nbr_cnt, f.ncr_nid_width, f.ncr_nbr_nid,
    f.zc_tei, f.zc_collect_site, f.zc_collect_period, f.zc_collect_cnt,
    f.zr_tei, f.zr_total_cnt, f.zr_ph1_cnt, f.zr_ph2_cnt, f.zr_ph3_cnt,
    f.zr_base_ntb, f.zr_ph1_diff, f.zr_ph2_diff, f.zr_ph3_diff,
    f.diag_vendor_id, f.diag_custom,
    f.rreq_version, f.rreq_seq, f.rreq_path_pref, f.rreq_payload_type, f.rreq_payload_len,
    f.rrep_version, f.rrep_seq, f.rrep_payload_type, f.rrep_payload_len,
    f.rerr_version, f.rerr_seq, f.rerr_unreach_cnt, f.rerr_unreach_tei,
    f.rack_version, f.rack_seq,
    f.lcreq_version, f.lcreq_seq, f.lcreq_sta_cnt, f.lcreq_sta_tei,
    f.lcrsp_version, f.lcrsp_level, f.lcrsp_chan_qual, f.lcrsp_path_pref, f.lcrsp_seq,
    f.rfccr_cco_mac, f.rfccr_nbr_cnt, f.rfccr_nbr_ch,
    f.rfdl_sta_mac, f.rfdl_seq, f.rfdl_ie_type, f.rfdl_ie_ltype,
    f.rfdl_ie_len, f.rfdl_ie_data,
    f.app_port, f.app_packet_id, f.app_ctrl_word, f.app_payload,
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

-- 小端读整字节字段
local function add_le(tree, field, tvb, off, len)
    return tree:add(field, tvb(off, len), tvb(off, len):le_uint())
end

-- 位图逐字节解析: bit 全局索引 = TEI (字节i的bit j → TEI = i*8+j)
-- 每字节输出 "值 (TEI列表)", 如 "2 (TEI1)" / "0x83 (TEI0,TEI1,TEI7)"
-- 顶层节点用 bytes 字段 field (显示原始 hex), 逐字节子节点用 string
local function dissect_tei_bitmap(tvb, tree, off, size, field, label_zh, label_en)
    if size <= 0 or off + size > tvb:len() then return end
    local bmp_tree = tree:add(field, tvb(off, size))
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
-- 段解析函数
-- =========================================================================

-- TMI → PB 块大小 (字节)。与 Qt beacon_pb_size 一致 (51242 物理块大小表)
-- TMI 即 FCH 字节9 高4bit 的"分集拷贝基本模式"
local function beacon_pb_size(tmi)
    if tmi == 0 or tmi == 1 then return 520 end
    if tmi >= 2 and tmi <= 6 then return 136 end
    if tmi >= 7 and tmi <= 10 then return 520 end
    if tmi == 11 or tmi == 12 then return 264 end
    if tmi == 13 or tmi == 14 then return 72 end
    return -1
end

-- SOF PB 块大小: TMI 主表 + TMI_EXT 扩展 (与 sofparser.cpp sof_pb_size 一致)
local function sof_pb_size(tmi, tmi_ext)
    local s = beacon_pb_size(tmi)
    if s > 0 then return s end
    if tmi_ext >= 1 and tmi_ext <= 6 then return 520 end
    if tmi_ext >= 10 and tmi_ext <= 14 then return 136 end
    return -1
end

-- 信标帧可变区域 (表17 载波, 起始于 FCH 字节4)
local function dissect_beacon_vf(tvb, tree)
    local bts_raw = tvb(4, 4):le_uint()
    tree:add(f.beacon_bts, tvb(4, 4), bts_raw)
    -- NTB: 25MHz时钟, 40ns/tick, 换算秒 = NTB / 25e6
    tree:add(f.beacon_bts_sec, tvb(4, 4), bts_raw / 25000000.0)
    add_val(tree, f.beacon_src_tei, tvb, 8, 2, read_bits(tvb, 8*8, 12))
    tree:add(f.beacon_div_mode, tvb(9, 1))
    add_val(tree, f.beacon_symbol_cnt, tvb, 10, 2, read_bits(tvb, 10*8, 9))
    tree:add(f.beacon_phase, tvb(11, 1))
    tree:add(f.beacon_vf_rsv, tvb(11, 1))       -- 字节11 bit3-7 保留
end

-- SOF 帧可变区域 (表19 载波, 起始于 FCH 字节4)
local function dissect_sof_vf(tvb, tree)
    add_val(tree, f.sof_src_tei, tvb, 4, 2, read_bits(tvb, 4*8, 12))
    add_val(tree, f.sof_dst_tei, tvb, 5, 2, read_bits(tvb, 5*8+4, 12))
    tree:add(f.sof_lid, tvb(7, 1))
    add_val(tree, f.sof_frame_len, tvb, 8, 2, read_bits(tvb, 8*8, 12))
    tree:add(f.sof_pb_count, tvb(9, 1))
    add_val(tree, f.sof_symbol_cnt, tvb, 10, 2, read_bits(tvb, 10*8, 9))
    tree:add(f.sof_bcast, tvb(11, 1))
    tree:add(f.sof_retrans, tvb(11, 1))
    tree:add(f.sof_encrypt, tvb(11, 1))
    tree:add(f.sof_div_mode, tvb(11, 1))
    tree:add(f.sof_div_ext, tvb(12, 1))
end

-- SACK 帧可变区域 (表23 载波, 起始于 FCH 字节4)
local function dissect_sack_vf(tvb, tree)
    tree:add(f.sack_recv_result, tvb(4, 1))
    tree:add(f.sack_recv_status, tvb(4, 1))
    add_val(tree, f.sack_src_tei, tvb, 5, 2, read_bits(tvb, 5*8, 12))
    add_val(tree, f.sack_dst_tei, tvb, 6, 2, read_bits(tvb, 6*8+4, 12))
    tree:add(f.sack_pb_count, tvb(8, 1))
    tree:add(f.sack_rsv, tvb(8, 1))       -- 字节8 bit3-7 保留
    tree:add(f.sack_chan_qual, tvb(9, 1))
    tree:add(f.sack_site_load, tvb(10, 1))
    tree:add(f.sack_rsv, tvb(11, 1))      -- 字节11 保留
    tree:add(f.sack_ext_type, tvb(12, 1))
end

-- 网间协调帧可变区域 (表26, 起始于 FCH 字节4)
local function dissect_coord_vf(tvb, tree)
    add_le(tree, f.coord_duration, tvb, 4, 2)
    add_le(tree, f.coord_bw_offset, tvb, 6, 2)
    tree:add(f.coord_nbr_nid, tvb(8, 3), tvb(8, 3):le_uint())
    tree:add(f.coord_rf_ch, tvb(11, 1))
    tree:add(f.coord_rsv, tvb(12, 1))     -- 字节12 bit0-3 保留
end

-- 站点能力条目 (表47)
local function dissect_entry_sta_cap(tvb, tree, off)
    local tei = read_bits(tvb, off*8, 12)
    add_val(tree, f.ent_tei, tvb, off, 2, tei)
    add_val(tree, f.ent_proxy_tei, tvb, off+1, 2, read_bits(tvb, off*8+12, 12))
    tree:add(f.ent_path_rate, tvb(off+3, 1))
    tree:add(f.ent_sta_mac, tvb(off+4, 6))
    -- 记录 TEI ↔ MAC 映射
    record_tei_mac(tei, mac_to_str(tvb, off+4))
    tree:add(f.ent_role, tvb(off+10, 1))
    tree:add(f.ent_level, tvb(off+10, 1))
    tree:add(f.ent_proxy_qual, tvb(off+11, 1))
    tree:add(f.ent_phase, tvb(off+12, 1))
    tree:add(f.ent_rf_hop, tvb(off+12, 1))
end

-- 路由参数条目 (表48)
local function dissect_entry_route_param(tvb, tree, off)
    add_le(tree, f.ent_route_period, tvb, off, 2)
    add_le(tree, f.ent_route_remain, tvb, off+2, 2)
    add_le(tree, f.ent_proxy_dl_period, tvb, off+4, 2)
    add_le(tree, f.ent_disc_dl_period, tvb, off+6, 2)
end

-- 频段变更条目 (表49)
local function dissect_entry_band_change(tvb, tree, off)
    tree:add(f.ent_target_band, tvb(off, 1))
    add_le(tree, f.ent_band_remain, tvb, off+1, 4)
end

-- 时隙分配条目 (表50)
local function dissect_entry_slot_alloc(tvb, tree, off)
    local nc_beacon_cnt = tvb(off, 1):uint()
    local csma_phase_cnt = band(tvb(off+1, 1):uint(), 0x30) / 16  -- bit4-5 → 1-3
    local bcsma_phase_cnt = tvb(off+6, 1):uint()
    tree:add(f.ent_nc_beacon_cnt, tvb(off, 1))
    tree:add(f.ent_c_beacon_cnt, tvb(off+1, 1))
    tree:add(f.ent_csma_phase_cnt, tvb(off+1, 1))
    tree:add(f.ent_proxy_beacon_cnt, tvb(off+3, 1))
    tree:add(f.ent_beacon_slot_len, tvb(off+4, 1))
    tree:add(f.ent_csma_slice_len, tvb(off+5, 1))
    tree:add(f.ent_bcsma_phase_cnt, tvb(off+6, 1))
    tree:add(f.ent_bcsma_lid, tvb(off+7, 1))
    tree:add(f.ent_tdma_slot_len, tvb(off+8, 1))
    tree:add(f.ent_tdma_lid, tvb(off+9, 1))
    add_le(tree, f.ent_bp_start_ntb, tvb, off+10, 4)
    add_le(tree, f.ent_bp_len, tvb, off+14, 4)
    add_val(tree, f.ent_rf_beacon_len, tvb, off+18, 2, read_bits(tvb, (off+18)*8, 10))
    -- 非中央信标信息: 每条 2 字节 (表51: TEI 12b + 信标类型1b + RF标志3b)
    local pos = off + 20
    for i = 1, nc_beacon_cnt do
        add_val(tree, f.ent_ncb_tei, tvb, pos, 2, read_bits(tvb, pos*8, 12))
        tree:add(f.ent_ncb_type, tvb(pos+1, 1))
        tree:add(f.ent_ncb_rf_flag, tvb(pos+1, 1))
        pos = pos + 2
    end
    -- CSMA时隙信息: 每条 4 字节
    for i = 1, csma_phase_cnt do
        tree:add(f.ent_csma_len, tvb(pos, 3), tvb(pos, 3):le_uint())
        tree:add(f.ent_csma_phase, tvb(pos+3, 1))
        pos = pos + 4
    end
    -- 绑定CSMA时隙信息: 每条 4 字节
    for i = 1, bcsma_phase_cnt do
        tree:add(f.ent_bcsma_len, tvb(pos, 3), tvb(pos, 3):le_uint())
        tree:add(f.ent_bcsma_phase, tvb(pos+3, 1))
        pos = pos + 4
    end
end

-- 无线路由参数条目 (表54)
local function dissect_entry_rf_route(tvb, tree, off)
    tree:add(f.ent_rf_dl_period, tvb(off, 1))
    tree:add(f.ent_rf_age_cnt, tvb(off+1, 1))
end

-- 无线信道变更条目 (表55)
local function dissect_entry_rf_ch(tvb, tree, off)
    tree:add(f.ent_target_ch, tvb(off, 1))
    add_le(tree, f.ent_ch_remain, tvb, off+1, 4)
end

-- 精简信标站点信息及时隙条目 (表57)
local function dissect_entry_simple_beacon(tvb, tree, off)
    add_val(tree, f.ent_tei, tvb, off, 2, read_bits(tvb, off*8, 12))
    add_val(tree, f.ent_proxy_tei, tvb, off+1, 2, read_bits(tvb, off*8+12, 12))
    tree:add(f.ent_role, tvb(off+3, 1))
    tree:add(f.ent_level, tvb(off+3, 1))
    tree:add(f.ent_sta_mac, tvb(off+4, 6))
    tree:add(f.ent_rf_hop, tvb(off+10, 1))
    add_le(tree, f.ent_sb_csma_start, tvb, off+11, 4)
    add_le(tree, f.ent_sb_csma_len, tvb, off+15, 2)
end

-- 信标管理信息 (表44), 返回下一个 offset (填充区前位置)
-- 条目长度语义(与 Qt beaconparser 一致):
--   长度字段值 len_raw = 头(1B) + 长度字段(1/2B) + 内容
--   内容长 = len_raw - 2 (普通) / len_raw - 3 (0xC0, 长度字段2B)
local function dissect_beacon_mgmt_info(tvb, tree, off)
    if off >= tvb:len() then return off end
    tree:add(f.beacon_entry_cnt, tvb(off, 1))
    local pos = off + 1
    local cnt = tvb(off, 1):uint()
    for i = 1, cnt do
        if pos >= tvb:len() then break end
        local etype = tvb(pos, 1):uint()
        tree:add(f.beacon_ent_type, tvb(pos, 1))
        pos = pos + 1
        -- 长度字段: 0xC0 及以上为 2 字节, 其余 1 字节 (小端)
        local len_bytes = (etype >= 0xC0) and 2 or 1
        if pos + len_bytes > tvb:len() then break end
        local len_raw = tvb(pos, len_bytes):le_uint()
        tree:add(f.beacon_ent_len, tvb(pos, len_bytes), len_raw)
        pos = pos + len_bytes
        -- 内容长 = len_raw - (头1B + 长度字段len_bytes)
        local item_len = len_raw - (1 + len_bytes)
        if item_len <= 0 then break end
        tree:add(f.beacon_ent_data, tvb(pos, item_len))
        -- 按条目类型解析内容
        if etype == 0x00 then
            dissect_entry_sta_cap(tvb, tree, pos)
        elseif etype == 0x01 then
            dissect_entry_route_param(tvb, tree, pos)
        elseif etype == 0x02 then
            dissect_entry_band_change(tvb, tree, pos)
        elseif etype == 0x03 then
            dissect_entry_rf_route(tvb, tree, pos)
        elseif etype == 0x04 then
            dissect_entry_rf_ch(tvb, tree, pos)
        elseif etype == 0x05 then
            dissect_entry_simple_beacon(tvb, tree, pos)
        elseif etype == 0xC0 then
            dissect_entry_slot_alloc(tvb, tree, pos)
        end
        pos = pos + item_len
    end
    return pos
end

-- 标准信标帧载荷 (表38)。off = 载荷起始(16, FCH 之后)
-- 结构(与 Qt beaconparser 一致):
--   整帧 MPDU = FCH(16B) + PB块(PBSize, 由 FCH 字节9 高4bit TMI 查表)
--   PB块 = 帧载荷(pbsize-3) + PB CRC24(3B)
--   帧载荷 = 固定头(标准20B) + 管理区 + PB Padding + CRC32(尾4B)
local function dissect_beacon_payload(tvb, tree, off)
    local tmi = read_bits(tvb, 9*8+4, 4)   -- FCH 字节9 高4bit
    local pbsize = beacon_pb_size(tmi)
    if pbsize <= 0 then
        tree:add_expert_info(PI_MALFORMED, PI_WARN, "TMI 无效")
        return off
    end
    local gb_end = off + pbsize - 3   -- 帧载荷区末尾(不含 PB CRC24)

    -- 固定头 (标准: 0..19)
    tree:add(f.beacon_type, tvb(off, 1))
    tree:add(f.beacon_net_cplt, tvb(off, 1))
    tree:add(f.beacon_simple, tvb(off, 1))
    tree:add(f.beacon_start_assoc, tvb(off, 1))
    tree:add(f.beacon_use_flag, tvb(off, 1))
    tree:add(f.beacon_net_seq, tvb(off+1, 1))
    tree:add(f.beacon_cco_mac, tvb(off+2, 6))
    add_le(tree, f.beacon_bpc, tvb, off+8, 4)
    tree:add(f.beacon_rf_ch, tvb(off+12, 1))

    -- 信标管理信息从 off+20 开始, 消费到 padding 起点
    local pos = dissect_beacon_mgmt_info(tvb, tree, off + 20)

    -- PB Padding: 管理区消费终点到 CRC32 前
    if pos < gb_end - 4 then
        tree:add(f.beacon_ent_data, tvb(pos, gb_end - 4 - pos))
    end

    -- BPCS CRC32 (帧载荷尾 4B): 校验帧载荷 off..gb_end-4 (不含 CRC32 本身)
    if gb_end - 4 >= off then
        local bpcs_rx = tvb(gb_end - 4, 4):le_uint()
        local bpcs_calc = crc32_le(tvb, off, gb_end - off)
        tree:add(f.beacon_bpcs, tvb(gb_end - 4, 4), bpcs_rx)
        tree:add(f.beacon_bpcs_calc, tvb(gb_end - 4, 4), bpcs_calc)
        tree:add(f.beacon_bpcs_ok, tvb(gb_end - 4, 1), bpcs_calc == bpcs_rx)
    end
    -- PB CRC24 (块尾 3B)
    if off + pbsize <= tvb:len() then
        tree:add(f.pb_pbcs, tvb(off + pbsize - 3, 3), tvb(off + pbsize - 3, 3):le_uint())
    end
    return off + pbsize
end

-- 精简信标帧载荷 (表56)。固定头仅 12B, 管理区从 off+12 起, 无信道编号/保留段
local function dissect_simple_beacon_payload(tvb, tree, off)
    local tmi = read_bits(tvb, 9*8+4, 4)
    local pbsize = beacon_pb_size(tmi)
    if pbsize <= 0 then
        tree:add_expert_info(PI_MALFORMED, PI_WARN, "TMI 无效")
        return off
    end
    local gb_end = off + pbsize - 3

    tree:add(f.beacon_type, tvb(off, 1))
    tree:add(f.beacon_net_cplt, tvb(off, 1))
    tree:add(f.beacon_simple, tvb(off, 1))
    tree:add(f.beacon_start_assoc, tvb(off, 1))
    tree:add(f.beacon_use_flag, tvb(off, 1))
    tree:add(f.beacon_net_seq, tvb(off+1, 1))
    tree:add(f.beacon_cco_mac, tvb(off+2, 6))
    add_le(tree, f.beacon_bpc, tvb, off+8, 4)

    local pos = dissect_beacon_mgmt_info(tvb, tree, off + 12)
    if pos < gb_end - 4 then
        tree:add(f.beacon_ent_data, tvb(pos, gb_end - 4 - pos))
    end
    if gb_end - 4 >= off then
        local bpcs_rx = tvb(gb_end - 4, 4):le_uint()
        local bpcs_calc = crc32_le(tvb, off, gb_end - off)
        tree:add(f.beacon_bpcs, tvb(gb_end - 4, 4), bpcs_rx)
        tree:add(f.beacon_bpcs_calc, tvb(gb_end - 4, 4), bpcs_calc)
        tree:add(f.beacon_bpcs_ok, tvb(gb_end - 4, 1), bpcs_calc == bpcs_rx)
    end
    if off + pbsize <= tvb:len() then
        tree:add(f.pb_pbcs, tvb(off + pbsize - 3, 3), tvb(off + pbsize - 3, 3):le_uint())
    end
    return off + pbsize
end

-- 标准 MAC 帧头 (表4), 返回头长
local function dissect_std_mac_hdr(tvb, tree, off)
    local addr_flag = read_bits(tvb, off*8 + 11*8 + 3, 1)
    tree:add(f.mac_version, tvb(off, 1))
    add_val(tree, f.mac_ostei, tvb, off, 2, read_bits(tvb, off*8+4, 12))
    add_val(tree, f.mac_odtei, tvb, off+2, 2, read_bits(tvb, off*8+2*8, 12))
    tree:add(f.mac_send_type, tvb(off+3, 1))
    tree:add(f.mac_retry_limit, tvb(off+4, 1))
    tree:add(f.mac_rsv0, tvb(off+4, 1))          -- 字节4 bit5-7 保留
    add_le(tree, f.mac_msdu_seq, tvb, off+5, 2)
    tree:add(f.mac_msdu_type, tvb(off+7, 1))
    add_val(tree, f.mac_msdu_len, tvb, off+8, 2, read_bits(tvb, off*8+8*8, 11))
    tree:add(f.mac_restart_cnt, tvb(off+9, 1))
    tree:add(f.mac_proxy_main, tvb(off+9, 1))
    tree:add(f.mac_route_total, tvb(off+10, 1))
    tree:add(f.mac_route_left, tvb(off+10, 1))
    tree:add(f.mac_bcast_dir, tvb(off+11, 1))
    tree:add(f.mac_path_repair, tvb(off+11, 1))
    tree:add(f.mac_addr_flag, tvb(off+11, 1))
    tree:add(f.mac_rsv1, tvb(off+11, 1))         -- 字节11 bit4-7 保留
    tree:add(f.mac_rsv2, tvb(off+12, 1))         -- 字节12 保留
    tree:add(f.mac_net_seq, tvb(off+13, 1))
    tree:add(f.mac_rsv3, tvb(off+14, 1))         -- 字节14 保留
    tree:add(f.mac_rsv4, tvb(off+15, 1))         -- 字节15 保留
    local hdr_len = 16
    if addr_flag == 1 then
        tree:add(f.mac_osmac, tvb(off+16, 6))
        tree:add(f.mac_odmac, tvb(off+22, 6))
        hdr_len = 28
    end
    return hdr_len
end

-- 单跳 MAC 帧头 (表11)
local function dissect_sh_mac_hdr(tvb, tree, off)
    tree:add(f.sh_version, tvb(off, 1))
    tree:add(f.sh_msgtype, tvb(off+1, 1))
    add_val(tree, f.sh_msdulen, tvb, off+2, 2, read_bits(tvb, off*8+2*8, 11))
    return 4
end

-- =========================================================================
-- 管理消息体解析
-- =========================================================================

-- 关联请求 (表60)
local function dissect_assoc_req(tvb, tree, off)
    tree:add(f.ar_sta_mac, tvb(off, 6))
    -- 候选代理 TEI x5, 每个 2 字节
    for i = 0, 4 do
        local p = off + 6 + i * 2
        add_val(tree, f.ar_proxy_tei, tvb, p, 2, read_bits(tvb, p*8, 12))
        tree:add(f.ar_link_type, tvb(p+1, 1))
    end
    tree:add(f.ar_phase, tvb(off+16, 1))
    tree:add(f.ar_dev_type, tvb(off+17, 1))
    tree:add(f.ar_mac_type, tvb(off+18, 1))
    tree:add(f.ar_module, tvb(off+19, 1))
    add_le(tree, f.ar_assoc_rand, tvb, off+20, 4)
    tree:add(f.ar_vendor, tvb(off+24, 18))
    -- 站点版本信息 (表66)
    local vp = off + 42
    tree:add(f.ar_boot_reason, tvb(vp, 1))
    tree:add(f.ar_boot_ver, tvb(vp+1, 1))
    add_le(tree, f.ar_soft_ver, tvb, vp+2, 2)
    add_le(tree, f.ar_ver_time, tvb, vp+4, 2)
    add_le(tree, f.ar_vendor_code, tvb, vp+6, 2)
    add_le(tree, f.ar_chip_code, tvb, vp+8, 2)
    add_le(tree, f.ar_hard_rst, tvb, off+52, 2)
    add_le(tree, f.ar_soft_rst, tvb, off+54, 2)
    tree:add(f.ar_proxy_type, tvb(off+56, 1))
    add_le(tree, f.ar_e2e_seq, tvb, off+60, 4)
    tree:add(f.ar_mgmt_id, tvb(off+64, 24))
end

-- 关联确认 (表70)
local function dissect_assoc_cnf(tvb, tree, off)
    tree:add(f.ac_sta_mac, tvb(off, 6))
    tree:add(f.ac_cco_mac, tvb(off+6, 6))
    tree:add(f.ac_result, tvb(off+12, 1))
    tree:add(f.ac_sta_level, tvb(off+13, 1))
    local sta_tei = read_bits(tvb, (off+14)*8, 12)
    add_val(tree, f.ac_sta_tei, tvb, off+14, 2, sta_tei)
    tree:add(f.ac_link_type, tvb(off+15, 1))
    tree:add(f.ac_carrier_band, tvb(off+15, 1))
    add_val(tree, f.ac_proxy_tei, tvb, off+16, 2, read_bits(tvb, (off+16)*8, 12))
    tree:add(f.ac_total_pkgs, tvb(off+18, 1))
    tree:add(f.ac_pkg_idx, tvb(off+19, 1))
    add_le(tree, f.ac_assoc_rand, tvb, off+20, 4)
    add_le(tree, f.ac_reassoc_time, tvb, off+24, 4)
    add_le(tree, f.ac_e2e_seq, tvb, off+28, 4)
    add_le(tree, f.ac_path_seq, tvb, off+32, 4)
    -- 记录 TEI ↔ MAC 映射 (CCO 分配的站点 TEI 与站点 MAC)
    record_tei_mac(sta_tei, mac_to_str(tvb, off))
    -- 路由表信息 (表74)
    if off + 40 + 8 <= tvb:len() then
        add_le(tree, f.ac_direct_sta, tvb, off+40, 2)
        add_le(tree, f.ac_direct_proxy, tvb, off+42, 2)
        add_le(tree, f.ac_route_size, tvb, off+44, 2)
    end
end

-- 关联汇总指示 (表76)
local function dissect_assoc_gather(tvb, tree, off)
    tree:add(f.ag_result, tvb(off, 1))
    tree:add(f.ag_sta_level, tvb(off+1, 1))
    tree:add(f.ag_cco_mac, tvb(off+2, 6))
    add_val(tree, f.ag_proxy_tei, tvb, off+8, 2, read_bits(tvb, (off+8)*8, 12))
    tree:add(f.ag_carrier_band, tvb(off+9, 1))
    local cnt = tvb(off+11, 1):uint()
    tree:add(f.ag_gather_cnt, tvb(off+11, 1))
    -- 站点信息: 每条 8 字节 (MAC 6B + TEI 12bit + 保留4bit)
    for i = 0, cnt - 1 do
        local p = off + 16 + i * 8
        tree:add(f.ag_sta_mac, tvb(p, 6))
        local sta_tei = read_bits(tvb, (p+6)*8, 12)
        add_val(tree, f.ag_sta_tei, tvb, p+6, 2, sta_tei)
        -- 记录 TEI ↔ MAC 映射
        record_tei_mac(sta_tei, mac_to_str(tvb, p))
    end
end

-- 代理变更请求 (表79)
local function dissect_cproxy_req(tvb, tree, off)
    add_val(tree, f.cpr_sta_tei, tvb, off, 2, read_bits(tvb, off*8, 12))
    for i = 0, 4 do
        local p = off + 2 + i * 2
        add_val(tree, f.cpr_new_proxy, tvb, p, 2, read_bits(tvb, p*8, 12))
        tree:add(f.cpr_link_type, tvb(p+1, 1))
    end
    add_val(tree, f.cpr_old_proxy, tvb, off+12, 2, read_bits(tvb, (off+12)*8, 12))
    tree:add(f.cpr_proxy_type, tvb(off+14, 1))
    tree:add(f.cpr_reason, tvb(off+15, 1))
    add_le(tree, f.cpr_e2e_seq, tvb, off+16, 4)
    tree:add(f.cpr_phase, tvb(off+20, 1))
end

-- 代理变更确认 (表84)
local function dissect_cproxy_cnf(tvb, tree, off)
    tree:add(f.cpc_result, tvb(off, 1))
    tree:add(f.cpc_total_pkgs, tvb(off+1, 1))
    tree:add(f.cpc_pkg_idx, tvb(off+2, 1))
    add_val(tree, f.cpc_sta_tei, tvb, off+4, 2, read_bits(tvb, (off+4)*8, 12))
    tree:add(f.cpc_link_type, tvb(off+5, 1))
    add_val(tree, f.cpc_proxy_tei, tvb, off+6, 2, read_bits(tvb, (off+6)*8, 12))
    add_le(tree, f.cpc_e2e_seq, tvb, off+8, 4)
    add_le(tree, f.cpc_path_seq, tvb, off+12, 4)
    local child_cnt = tvb(off+16, 2):le_uint()
    tree:add(f.cpc_child_cnt, tvb(off+16, 2), child_cnt)
    -- 子站点条目: 每个 2 字节
    for i = 0, child_cnt - 1 do
        local p = off + 20 + i * 2
        add_val(tree, f.cpc_child_tei, tvb, p, 2, read_bits(tvb, p*8, 12))
    end
end

-- 代理变更确认位图版 (表88)
local function dissect_cproxy_bmp(tvb, tree, off)
    tree:add(f.cpb_result, tvb(off, 1))
    local bmp_size = tvb(off+2, 2):le_uint()
    tree:add(f.cpb_bitmap_size, tvb(off+2, 2), bmp_size)
    add_val(tree, f.cpb_sta_tei, tvb, off+4, 2, read_bits(tvb, (off+4)*8, 12))
    tree:add(f.cpb_link_type, tvb(off+5, 1))
    add_val(tree, f.cpb_proxy_tei, tvb, off+6, 2, read_bits(tvb, (off+6)*8, 12))
    add_le(tree, f.cpb_e2e_seq, tvb, off+8, 4)
    add_le(tree, f.cpb_path_seq, tvb, off+12, 4)
    dissect_tei_bitmap(tvb, tree, off+20, bmp_size, f.cpb_child_bmp, T("子站点位图", "Child STA Bitmap"), "Child STA Bitmap")
end

-- 离线指示 (表91)
local function dissect_leave(tvb, tree, off)
    add_le(tree, f.li_reason, tvb, off, 2)
    local sta_cnt = tvb(off+2, 2):le_uint()
    tree:add(f.li_sta_cnt, tvb(off+2, 2), sta_cnt)
    add_le(tree, f.li_delay, tvb, off+4, 2)
    for i = 0, sta_cnt - 1 do
        tree:add(f.li_sta_mac, tvb(off+16+i*6, 6))
    end
end

-- 心跳检测 (表94)
local function dissect_heartbeat(tvb, tree, off)
    add_val(tree, f.hb_ostei, tvb, off, 2, read_bits(tvb, off*8, 12))
    add_val(tree, f.hb_max_disc_tei, tvb, off+2, 2, read_bits(tvb, (off+2)*8, 12))
    add_le(tree, f.hb_max_disc_cnt, tvb, off+4, 2)
    local bmp_size = tvb(off+6, 2):le_uint()
    tree:add(f.hb_bitmap_size, tvb(off+6, 2), bmp_size)
    dissect_tei_bitmap(tvb, tree, off+8, bmp_size, f.hb_disc_bmp, T("发现站点位图", "Discovery STA Bitmap"), "Discovery STA Bitmap")
end

-- 发现列表 (表95)
local function dissect_disc_list(tvb, tree, off)
    local tei = read_bits(tvb, off*8, 12)
    add_val(tree, f.dl_tei, tvb, off, 2, tei)
    add_val(tree, f.dl_proxy_tei, tvb, off+1, 2, read_bits(tvb, (off+1)*8+4, 12))
    tree:add(f.dl_role, tvb(off+3, 1))
    tree:add(f.dl_level, tvb(off+3, 1))
    tree:add(f.dl_mac, tvb(off+4, 6))
    tree:add(f.dl_cco_mac, tvb(off+10, 6))
    -- 记录 TEI ↔ MAC 映射 (发送发现列表报文的站点)
    record_tei_mac(tei, mac_to_str(tvb, off+4))
    tree:add(f.dl_phase, tvb(off+16, 1))
    tree:add(f.dl_proxy_qual, tvb(off+17, 1))
    tree:add(f.dl_proxy_rate, tvb(off+18, 1))
    tree:add(f.dl_proxy_dl_rate, tvb(off+19, 1))
    add_le(tree, f.dl_sta_cnt, tvb, off+20, 2)
    tree:add(f.dl_send_cnt, tvb(off+22, 1))
    local up_route_cnt = tvb(off+23, 1):uint()
    tree:add(f.dl_up_route_cnt, tvb(off+23, 1))
    add_le(tree, f.dl_route_remain, tvb, off+24, 2)
    local bmp_size = tvb(off+26, 2):le_uint()
    tree:add(f.dl_bitmap_size, tvb(off+26, 2), bmp_size)
    tree:add(f.dl_min_rate, tvb(off+28, 1))
    -- 上行路由条目信息: 每条 2 字节
    local pos = off + 32
    for i = 0, up_route_cnt - 1 do
        add_val(tree, f.dl_next_hop_tei, tvb, pos, 2, read_bits(tvb, pos*8, 12))
        tree:add(f.dl_route_type, tvb(pos+1, 1))
        pos = pos + 2
    end
    dissect_tei_bitmap(tvb, tree, pos, bmp_size, f.dl_disc_bmp, T("发现站点列表位图", "Discovery STA List Bitmap"), "Discovery STA List Bitmap")

    -- 接收发现列表信息 (表99): 条目数 = 位图中置位 bit 总数, 每个 1 字节
    -- 依次对应位图从 0 字节起第 1/2/.../N 个有效 TEI 站点的接收报文数
    local rcv_pos = pos + bmp_size
    if rcv_pos <= tvb:len() then
        -- 收集位图中置位 bit 对应的 TEI (bit 全局索引 = TEI: 字节i的bit j → TEI=i*8+j)
        local set_teis = {}
        for bi = 0, bmp_size - 1 do
            local b = tvb(pos + bi, 1):uint()
            for j = 0, 7 do
                if band(b, 2 ^ j) ~= 0 then
                    set_teis[#set_teis + 1] = bi * 8 + j
                end
            end
        end
        for k = 1, #set_teis do
            local tei = set_teis[k]
            local v = tvb(rcv_pos + k - 1, 1):uint()
            tree:add(f.dl_rcv_cnt, tvb(rcv_pos + k - 1, 1), v)
            tree:add(f.dl_rcv_item, tvb(rcv_pos + k - 1, 1),
                string.format(T("接收发现列表数[%d] (TEI %d): %d", "Rcv Discovery List Count[%d] (TEI %d): %d"), k - 1, tei, v))
        end
    end
end

-- 通信成功率上报 (表100)
local function dissect_succ_rate(tvb, tree, off)
    add_val(tree, f.sr_tei, tvb, off, 2, read_bits(tvb, off*8, 12))
    local cnt = tvb(off+2, 2):le_uint()
    tree:add(f.sr_sta_cnt, tvb(off+2, 2), cnt)
    for i = 0, cnt - 1 do
        local p = off + 4 + i * 4
        add_val(tree, f.sr_sta_tei, tvb, p, 2, read_bits(tvb, p*8, 12))
        tree:add(f.sr_down_rate, tvb(p+2, 1))
        tree:add(f.sr_up_rate, tvb(p+3, 1))
    end
end

-- 网络冲突上报 (表102)
local function dissect_net_conflict(tvb, tree, off)
    tree:add(f.ncr_cco_mac, tvb(off, 6))
    local cnt = tvb(off+6, 1):uint()
    tree:add(f.ncr_nbr_cnt, tvb(off+6, 1))
    tree:add(f.ncr_nid_width, tvb(off+7, 1))
    for i = 0, cnt - 1 do
        tree:add(f.ncr_nbr_nid, tvb(off+8+i*3, 3), tvb(off+8+i*3, 3):le_uint())
    end
end

-- 过零NTB采集指示 (表104)
local function dissect_zc_collect(tvb, tree, off)
    add_val(tree, f.zc_tei, tvb, off, 2, read_bits(tvb, off*8, 12))
    tree:add(f.zc_collect_site, tvb(off+2, 1))
    tree:add(f.zc_collect_period, tvb(off+3, 1))
    tree:add(f.zc_collect_cnt, tvb(off+4, 1))
end

-- 过零NTB上报 (表107)
local function dissect_zc_report(tvb, tree, off)
    add_val(tree, f.zr_tei, tvb, off, 2, read_bits(tvb, off*8, 12))
    tree:add(f.zr_total_cnt, tvb(off+2, 1))
    local ph1 = tvb(off+3, 1):uint()
    local ph2 = tvb(off+4, 1):uint()
    local ph3 = tvb(off+5, 1):uint()
    tree:add(f.zr_ph1_cnt, tvb(off+3, 1))
    tree:add(f.zr_ph2_cnt, tvb(off+4, 1))
    tree:add(f.zr_ph3_cnt, tvb(off+5, 1))
    add_le(tree, f.zr_base_ntb, tvb, off+6, 4)
    -- 过零NTB差值: 12bit 字段, 每 1.5 字节
    local pos = off + 10
    local function add_diffs(field, cnt)
        for i = 0, cnt - 1 do
            add_val(tree, field, tvb, pos, 2, read_bits(tvb, pos*8 + (i % 2) * 4, 12))
            -- 每两个 12bit 占 3 字节
            if i % 2 == 1 then pos = pos + 3 end
        end
    end
    add_diffs(f.zr_ph1_diff, ph1)
    add_diffs(f.zr_ph2_diff, ph2)
    add_diffs(f.zr_ph3_diff, ph3)
end

-- 网络诊断 (表109)
local function dissect_diag(tvb, tree, off)
    add_le(tree, f.diag_vendor_id, tvb, off, 2)
    tree:add(f.diag_custom, tvb(off+2))
end

-- 路由请求 (表111)
local function dissect_rreq(tvb, tree, off)
    tree:add(f.rreq_version, tvb(off, 1))
    add_le(tree, f.rreq_seq, tvb, off+1, 4)
    tree:add(f.rreq_path_pref, tvb(off+5, 1))
    tree:add(f.rreq_payload_type, tvb(off+5, 1))
    tree:add(f.rreq_payload_len, tvb(off+6, 1))
end

-- 路由回复 (表114)
local function dissect_rrep(tvb, tree, off)
    tree:add(f.rrep_version, tvb(off, 1))
    add_le(tree, f.rrep_seq, tvb, off+1, 4)
    tree:add(f.rrep_payload_type, tvb(off+5, 1))
    tree:add(f.rrep_payload_len, tvb(off+6, 1))
end

-- 路由错误 (表117)
local function dissect_rerr(tvb, tree, off)
    tree:add(f.rerr_version, tvb(off, 1))
    add_le(tree, f.rerr_seq, tvb, off+1, 4)
    local cnt = tvb(off+6, 1):uint()
    tree:add(f.rerr_unreach_cnt, tvb(off+6, 1))
    for i = 0, cnt - 1 do
        add_le(tree, f.rerr_unreach_tei, tvb, off+7+i*2, 2)
    end
end

-- 路由应答 (表118)
local function dissect_rack(tvb, tree, off)
    tree:add(f.rack_version, tvb(off, 1))
    add_le(tree, f.rack_seq, tvb, off+4, 4)
end

-- 链路确认请求 (表119)
local function dissect_lcreq(tvb, tree, off)
    tree:add(f.lcreq_version, tvb(off, 1))
    add_le(tree, f.lcreq_seq, tvb, off+1, 4)
    local cnt = tvb(off+6, 1):uint()
    tree:add(f.lcreq_sta_cnt, tvb(off+6, 1))
    for i = 0, cnt - 1 do
        add_le(tree, f.lcreq_sta_tei, tvb, off+7+i*2, 2)
    end
end

-- 链路确认回应 (表120)
local function dissect_lcrsp(tvb, tree, off)
    tree:add(f.lcrsp_version, tvb(off, 1))
    tree:add(f.lcrsp_level, tvb(off+1, 1))
    tree:add(f.lcrsp_chan_qual, tvb(off+2, 1))
    tree:add(f.lcrsp_path_pref, tvb(off+3, 1))
    add_le(tree, f.lcrsp_seq, tvb, off+4, 4)
end

-- 无线信道冲突上报 (表121)
local function dissect_rfccr(tvb, tree, off)
    tree:add(f.rfccr_cco_mac, tvb(off, 6))
    local cnt = tvb(off+6, 1):uint()
    tree:add(f.rfccr_nbr_cnt, tvb(off+6, 1))
    for i = 0, cnt - 1 do
        tree:add(f.rfccr_nbr_ch, tvb(off+7+i, 1))
    end
end

-- 无线发现列表 (表123, 单跳帧消息类型0)
local function dissect_rfdl(tvb, tree, off)
    tree:add(f.rfdl_sta_mac, tvb(off, 6))
    tree:add(f.rfdl_seq, tvb(off+6, 1))
    local pos = off + 7
    while pos + 1 < tvb:len() do
        local ie_type = band(tvb(pos, 1):uint(), 0x7F)
        local ltype = band(tvb(pos, 1):uint(), 0x80)
        tree:add(f.rfdl_ie_type, tvb(pos, 1))
        tree:add(f.rfdl_ie_ltype, tvb(pos, 1))
        pos = pos + 1
        local len_bytes = (ltype ~= 0) and 2 or 1
        local elen = tvb(pos, len_bytes):uint()
        tree:add(f.rfdl_ie_len, tvb(pos, len_bytes))
        pos = pos + len_bytes
        tree:add(f.rfdl_ie_data, tvb(pos, elen))
        pos = pos + elen
    end
end

-- 管理消息体分发
local function dissect_mgmt_body(tvb, tree, off, mmtype)
    if mmtype == 0x0000 then dissect_assoc_req(tvb, tree, off)
    elseif mmtype == 0x0001 then dissect_assoc_cnf(tvb, tree, off)
    elseif mmtype == 0x0002 then dissect_assoc_gather(tvb, tree, off)
    elseif mmtype == 0x0003 then dissect_cproxy_req(tvb, tree, off)
    elseif mmtype == 0x0004 then dissect_cproxy_cnf(tvb, tree, off)
    elseif mmtype == 0x0005 then dissect_cproxy_bmp(tvb, tree, off)
    elseif mmtype == 0x0006 then dissect_leave(tvb, tree, off)
    elseif mmtype == 0x0007 then dissect_heartbeat(tvb, tree, off)
    elseif mmtype == 0x0008 then dissect_disc_list(tvb, tree, off)
    elseif mmtype == 0x0009 then dissect_succ_rate(tvb, tree, off)
    elseif mmtype == 0x000A then dissect_net_conflict(tvb, tree, off)
    elseif mmtype == 0x000B then dissect_zc_collect(tvb, tree, off)
    elseif mmtype == 0x000C then dissect_zc_report(tvb, tree, off)
    elseif mmtype == 0x004F then dissect_diag(tvb, tree, off)
    elseif mmtype == 0x0050 then dissect_rreq(tvb, tree, off)
    elseif mmtype == 0x0051 then dissect_rrep(tvb, tree, off)
    elseif mmtype == 0x0052 then dissect_rerr(tvb, tree, off)
    elseif mmtype == 0x0053 then dissect_rack(tvb, tree, off)
    elseif mmtype == 0x0054 then dissect_lcreq(tvb, tree, off)
    elseif mmtype == 0x0055 then dissect_lcrsp(tvb, tree, off)
    elseif mmtype == 0x0080 then dissect_rfccr(tvb, tree, off)
    end
end

-- 管理消息头 (表58), 返回 mmtype
local function dissect_mgmt_hdr(tvb, tree, off)
    add_le(tree, f.mgmt_mmtype, tvb, off, 2)
    add_le(tree, f.mgmt_resv, tvb, off+2, 2)
    return tvb(off, 2):le_uint()
end

-- =========================================================================
-- 媒介类型 field extractor (必须在 dissector 注册前定义)
-- =========================================================================
local encap_type_f = Field.new("frame.encap_type")

function hplc.dissector(tvb, pinfo, tree)
    -- 按捕获 DLT 区分媒介: USER0(encap45)=HPLC 载波, USER1(encap46)=RF 无线
    local ev = encap_type_f()
    if ev and tostring(ev) == "46" then
        pinfo.cols.protocol = "RF"
    else
        pinfo.cols.protocol = "HPLC"
    end

    if tvb:len() < 16 then
        tree:add_expert_info(PI_MALFORMED, PI_WARN, "帧长不足 16 字节(最小 MPDU 帧控制)")
        return 0
    end

    local dt = band(tvb(0, 1):uint(), 0x07)
    local nid = tvb(1, 3):le_uint()
    cur_nid = nid  -- 供 record_tei_mac 记录映射

    -- 源/目的地址列 (Source/Destination 列): 按帧类型提取 TEI
    local src_tei, dst_tei
    if dt == 0 then
        src_tei = read_bits(tvb, 8*8, 12)      -- 信标源TEI (FCH 字节8)
    elseif dt == 1 then
        src_tei = read_bits(tvb, 4*8, 12)      -- SOF 源TEI
        dst_tei = read_bits(tvb, 5*8+4, 12)    -- SOF 目的TEI
    elseif dt == 2 then
        src_tei = read_bits(tvb, 5*8, 12)      -- SACK 源TEI
        dst_tei = read_bits(tvb, 6*8+4, 12)    -- SACK 目的TEI
    elseif dt == 3 then
        src_tei = 1                            -- 网间协调帧必然由 CCO 发出 (TEI 1)
    end
    -- 附加 MAC 地址 (历史帧已学到的 TEI↔MAC 映射)
    if src_tei then
        local base
        if src_tei == 1 then
            base = T("CCO (TEI 1)", "CCO (TEI 1)")
        else
            base = string.format(T("STA (TEI %d)", "STA (TEI %d)"), src_tei)
        end
        local mac = lookup_tei_mac(nid, src_tei)
        if mac then base = base .. " [" .. mac .. "]" end
        pinfo.cols.src = base
    else
        pinfo.cols.src = ""
    end
    if dst_tei then
        local base
        if dst_tei == 0xFFF then
            base = T("Broadcast (TEI 4095)", "Broadcast (TEI 4095)")
        else
            base = string.format("TEI %d", dst_tei)
        end
        local mac = lookup_tei_mac(nid, dst_tei)
        if mac then base = base .. " [" .. mac .. "]" end
        pinfo.cols.dst = base
    else
        pinfo.cols.dst = ""
    end

    pinfo.cols.info = string.format("DT=%s NID=0x%06x", dt_vals[dt] or T("Reserved", "Reserved").."("..dt..")", nid)

    local root = tree:add(hplc, tvb())

    -- MPDU 帧控制 16B (5.1.2)
    local fc_tree = root:add(hplc, tvb(0, 16), T("MPDU 帧控制 (FCH)", "MPDU Frame Control (FCH)"))
    fc_tree:add(f.fc_dt, tvb(0, 1))
    fc_tree:add(f.fc_net_type, tvb(0, 1))
    fc_tree:add(f.fc_nid, tvb(1, 3), nid)

    local vf_tree = fc_tree:add(f.fc_vf, tvb(4, 9))
    if dt == 0 then
        dissect_beacon_vf(tvb, vf_tree)
    elseif dt == 1 then
        dissect_sof_vf(tvb, vf_tree)
    elseif dt == 2 then
        dissect_sack_vf(tvb, vf_tree)
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

    -- 信标帧载荷 (dt=0)
    if dt == 0 and tvb:len() > 16 then
        -- 信标物理块无物理块头, 载荷直接跟在 FCH 后
        -- 精简信标标志在载荷首字节 bit4
        local simple = band(tvb(16, 1):uint(), 0x10) ~= 0
        if simple then
            dissect_simple_beacon_payload(tvb, root:add(hplc, tvb(16), T("精简信标帧载荷", "Lite Beacon Payload")), 16)
        else
            dissect_beacon_payload(tvb, root:add(hplc, tvb(16), T("信标帧载荷", "Beacon Payload")), 16)
        end
    end

    -- SOF 帧载荷: 物理块 -> MAC 帧 (dt=1)
    if dt == 1 and tvb:len() > 16 then
        -- 分集拷贝模式 → PB 块大小 (5.2.5.3 分片规格)
        local tmi = read_bits(tvb, 11*8+4, 4)
        local tmi_ext = read_bits(tvb, 12*8, 4)
        local pb_count = read_bits(tvb, 9*8+4, 4)
        local pbsz = sof_pb_size(tmi, tmi_ext)

        -- 逐 PB 块解析: 头(1B) + 体(pbsz-4) + CRC24(3B)
        local mac_off = 17  -- 第一块体起始 = FCH16 + 头1
        if pbsz > 0 and pb_count >= 1 and pb_count <= 4 then
            for i = 0, pb_count - 1 do
                local bstart = 16 + i * pbsz
                if bstart + pbsz > tvb:len() then break end
                local blk_tree = root:add(hplc, tvb(bstart, pbsz),
                    string.format(T("物理块 %d", "PB %d"), i))
                blk_tree:add(f.pb_size, tvb(bstart, pbsz), pbsz)
                blk_tree:add(f.pb_seq, tvb(bstart, 1))
                blk_tree:add(f.pb_sof, tvb(bstart, 1))
                blk_tree:add(f.pb_eof, tvb(bstart, 1))
                -- 体: 头后 pbsz-4 字节 (先整体标记, padding 后面再切)
                blk_tree:add(f.pb_body, tvb(bstart + 1, pbsz - 4))
                -- CRC24: 块末 3 字节 (小端), 校验前 pbsz-3 字节
                local crc_rx = tvb(bstart + pbsz - 3, 3):le_uint()
                local crc_calc = crc24_lsb(tvb, bstart, pbsz)
                blk_tree:add(f.pb_pbcs, tvb(bstart + pbsz - 3, 3), crc_rx)
                blk_tree:add(f.pb_crc_calc, tvb(bstart + pbsz - 3, 3), crc_calc)
                blk_tree:add(f.pb_crc_ok, tvb(bstart + pbsz - 3, 1), crc_calc == crc_rx)
            end
        else
            -- 无法查表时, 退化为旧行为: 仅读首块头
            local pb_off = 16
            local pb_tree = root:add(hplc, tvb(pb_off, 1), T("物理块头", "PB Header"))
            pb_tree:add(f.pb_seq, tvb(pb_off, 1))
            pb_tree:add(f.pb_sof, tvb(pb_off, 1))
            pb_tree:add(f.pb_eof, tvb(pb_off, 1))
        end

        if mac_off < tvb:len() then
            local version = read_bits(tvb, mac_off*8, 4)
            local mac_tree = root:add(hplc, tvb(mac_off), T("MAC 帧", "MAC Frame"))
            local msdu_off
            if version == 0 then
                local hdr_len = dissect_std_mac_hdr(tvb, mac_tree, mac_off)
                msdu_off = mac_off + hdr_len
            elseif version == 1 then
                local hdr_len = dissect_sh_mac_hdr(tvb, mac_tree, mac_off)
                msdu_off = mac_off + hdr_len
                -- 单跳帧: 消息类型0 = 无线发现列表
                local msg_type = tvb(mac_off+1, 1):uint()
                if msg_type == 0 then
                    dissect_rfdl(tvb, mac_tree, msdu_off)
                end
                return tvb:len()
            else
                msdu_off = mac_off + 16
            end

            local msdu_type = tvb(mac_off+7, 1):uint()
            local msdu_len = read_bits(tvb, mac_off*8+8*8, 11)

            -- 方向判断: OSTEI=1(源CCO) 且 ODTEI!=0xFFF → 下行; 否则上行
            -- 当前发出者 src_tei != OSTEI → 中继 Relay
            if version == 0 and mac_off + 16 <= tvb:len() then
                local ostei = read_bits(tvb, mac_off*8+4, 12)
                local odtei = read_bits(tvb, mac_off*8+2*8, 12)
                -- 原始目标地址列: ODTEI 格式化 + MAC 映射
                local ods
                if odtei == 0xFFF then
                    ods = T("Broadcast (TEI 4095)", "Broadcast (TEI 4095)")
                elseif odtei == 1 then
                    ods = T("CCO (TEI 1)", "CCO (TEI 1)")
                else
                    ods = string.format(T("STA (TEI %d)", "STA (TEI %d)"), odtei)
                end
                local odmac = lookup_tei_mac(nid, odtei)
                if odmac then ods = ods .. " [" .. odmac .. "]" end
                tree:add(f.col_orig_dst, tvb(mac_off+2, 2), ods)
                local dir_mark = ""
                if ostei == 1 and odtei ~= 0xFFF then
                    dir_mark = T(" ↓下行", " ↓Downlink")
                elseif ostei ~= 1 then
                    dir_mark = T(" ↑上行", " ↑Uplink")
                end
                if src_tei and ostei and src_tei ~= ostei then
                    dir_mark = dir_mark .. " Relay"
                end
                if dir_mark ~= "" then
                    pinfo.cols.info = tostring(pinfo.cols.info) .. dir_mark
                end
            end

            -- 管理消息
            if msdu_type == 0 and msdu_off + 4 <= tvb:len() then
                local mgmt_tree = mac_tree:add(hplc, tvb(msdu_off), T("管理消息", "Management Message"))
                local mmtype = dissect_mgmt_hdr(tvb, mgmt_tree, msdu_off)
                -- info 列追加管理消息类型
                local mt_name = mgmt_type_vals[mmtype] or ""
                local cur_info = tostring(pinfo.cols.info)
                if mt_name ~= "" then
                    pinfo.cols.info = cur_info .. string.format(" %s", mt_name)
                else
                    pinfo.cols.info = cur_info .. string.format(" MMTYPE=0x%04x", mmtype)
                end
                -- 消息体 = MSDU 去掉 4 字节管理消息头
                local body_off = msdu_off + 4
                local body_len = msdu_len - 4
                if body_len > 0 then
                    local avail = tvb:len() - body_off
                    if avail > 0 then
                        -- 用子 tvb 截断到实际可用长度, off 从 0 起
                        local body_tvb = tvb(body_off, math.min(body_len, avail)):tvb()
                        local ok, err = pcall(dissect_mgmt_body, body_tvb, mgmt_tree, 0, mmtype)
                        if not ok then
                            mgmt_tree:add_expert_info(PI_MALFORMED, PI_WARN,
                                "管理消息体解析失败(可能截断): " .. tostring(err))
                        end
                    end
                end
            end

            -- 应用层报文 (msdu_type=48): APP_BASE = 端口号 + 报文ID + 控制字
            if msdu_type == 48 and msdu_off + 4 <= tvb:len() then
                local app_tree = mac_tree:add(hplc, tvb(msdu_off), T("应用层报文", "Application Layer Packet"))
                app_tree:add(f.app_port, tvb(msdu_off, 1))
                local packet_id = tvb(msdu_off + 1, 2):le_uint()
                app_tree:add(f.app_packet_id, tvb(msdu_off + 1, 2), packet_id)
                app_tree:add(f.app_ctrl_word, tvb(msdu_off + 3, 1))
                -- 应用层载荷 = MSDU 去掉 4 字节 APP_BASE
                local app_payload_len = msdu_len - 4
                if app_payload_len > 0 and msdu_off + 4 + app_payload_len <= tvb:len() then
                    app_tree:add(f.app_payload, tvb(msdu_off + 4, app_payload_len))
                end
                -- info 列追加报文 ID 说明
                local pid_name = app_packet_id_vals[packet_id] or ""
                local cur_info = tostring(pinfo.cols.info)
                if pid_name ~= "" then
                    pinfo.cols.info = cur_info .. string.format(" APP[%s]", pid_name)
                else
                    pinfo.cols.info = cur_info .. string.format(" APP[ID=0x%04x]", packet_id)
                end
            end

            -- ICV CRC32 在 MSDU 末尾: 校验 MSDU(msdu_off..icv_off, 不含 MAC 帧头)
            local icv_off = msdu_off + msdu_len
            if msdu_len > 0 and icv_off + 4 <= tvb:len() then
                local icv_rx = tvb(icv_off, 4):le_uint()
                local icv_calc = crc32_le(tvb, msdu_off, msdu_len + 4)
                tree:add(f.mac_icv, tvb(icv_off, 4), icv_rx)
                tree:add(f.mac_icv_calc, tvb(icv_off, 4), icv_calc)
                tree:add(f.mac_icv_ok, tvb(icv_off, 1), icv_calc == icv_rx)
            end

            -- padding: 最后一个 PB 块中, MAC 帧(头+MSDU+ICV)结束到块 CRC24 前的填充区
            if pbsz > 0 and pb_count >= 1 and pb_count <= 4 then
                local pad_start = msdu_off + msdu_len + 4  -- ICV 之后
                local pad_end = 16 + pb_count * pbsz - 3   -- 末块 CRC24 起始
                if pad_start < pad_end and pad_end <= tvb:len() then
                    root:add(f.pb_padding, tvb(pad_start, pad_end - pad_start))
                end
            end
        end
    end

    return tvb:len()
end

-- =========================================================================
-- 注册到 USER DLT (internal encap 45-60)
-- =========================================================================
local wtap_encap = DissectorTable.get("wtap_encap")
for i = 45, 60 do
    wtap_encap:add(i, hplc)
end
