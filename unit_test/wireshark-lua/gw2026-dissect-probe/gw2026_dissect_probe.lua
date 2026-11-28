-- gw2026-dissect-probe: 无头 Lua 探针 (不依赖 tshark/Wireshark)
-- stub tvb/TreeItem/Proto/ProtoField/Field, 加载 packet-gw_2026.lua, 喂 V6 构造帧,
-- 记录 tree:add 的 (field_name, value, range_len) → 断言 A 批 A1-A6 字段值。
--
-- 运行: lua gw2026_dissect_probe.lua <plugin_dir>
-- 环境: lua5.4 (纯标准库)

local PLUGIN_DIR = arg[1] or "wireshark_support_plugins"
local PLUGIN = "packet-gw_2026.lua"

-- =========================================================================
-- 基础工具
-- =========================================================================
local function band(a, b)
    local r, x = 0, 1
    while a > 0 and b > 0 do
        if a % 2 == 1 and b % 2 == 1 then r = r + x end
        a = math.floor(a / 2); b = math.floor(b / 2); x = x * 2
    end
    return r
end

local function hex(s)
    local t = {}
    s = (s:gsub("%s+", ""))
    for i = 1, #s, 2 do t[#t + 1] = tonumber(s:sub(i, i + 1), 16) end
    return t
end

-- 字节数组 → hex (调试用)
local function bytes_hex(t)
    local out = {}
    for i = 1, #t do out[#out + 1] = string.format("%02x", t[i]) end
    return table.concat(out, " ")
end

-- =========================================================================
-- TvbRange / Tvb stub (记录型, 字节精确)
--   tvb(off, len)          → range (off 0-based, len 字节数)
--   tvb:len() / tvb.len()  → 帧长
--   range:uint()           → 单字节 (off)
--   range:le_uint()        → 小端读 len 字节
-- =========================================================================
local function make_range(bytes, off, len)
    local r = {}
    setmetatable(r, {
        __index = function(_, k)
            if k == "uint" then
                return function() return bytes[off + 1] or 0 end
            elseif k == "le_uint" then
                return function()
                    local v = 0
                    for i = 0, len - 1 do v = v + (bytes[off + 1 + i] or 0) * 2 ^ (8 * i) end
                    return v
                end
            elseif k == "len" then
                return function() return len end
            elseif k == "tvb" then
                return function()
                    local sub = {}
                    for i = off + 1, #bytes do sub[#sub + 1] = bytes[i] end
                    return make_tvb(sub)
                end
            elseif k == "bytes_str" then
                return function()
                    local s = {}
                    for i = 0, len - 1 do
                        local c = bytes[off + 1 + i] or 0
                        s[#s + 1] = string.char(c >= 32 and c <= 126 and c or 63)
                    end
                    return table.concat(s)
                end
            end
        end,
    })
    r.__range = true
    r.__off = off
    r.__len = len
    return r
end

local function make_tvb(bytes)
    local t = setmetatable({}, {
        __call = function(_, off, len)
            off = off or 0
            if len == nil then len = #bytes - off end
            if off < 0 then off = 0 end
            if len < 0 then len = 0 end
            return make_range(bytes, off, len)
        end,
    })
    t.len = function() return #bytes end
    return t
end

-- =========================================================================
-- Tree stub (记录型): tree:add 记录 (field_name, value, range_len)
-- =========================================================================
local function make_tree(records)
    local t = {}
    setmetatable(t, {
        __index = function(_, k)
            if k == "add" then
                return function(_, a, b, c)
                    if type(a) == "table" and a.__proto then
                        return make_tree(records)   -- 分组: 返回子 tree (共享 records)
                    elseif type(a) == "table" and a.__fname then
                        local v = c
                        local rlen = nil
                        if type(b) == "table" and b.__range then
                            rlen = b.__len
                            if v == nil then
                                if a.__ftype == "string" then
                                    v = b:bytes_str()   -- string 字段: 读 ASCII
                                else
                                    v = b:le_uint()
                                    if a.__mask then v = band(v, a.__mask) end
                                end
                            end
                        else
                            v = (v == nil) and b or v   -- add(field, value) 简化形式
                        end
                        records[#records + 1] = { name = a.__fname, value = v, rlen = rlen,
                            text = (a.__vals and a.__vals[v] ~= nil) and a.__vals[v] or nil }
                        return make_tree(records)       -- 字段 add 返回值可能被 append_text
                    end
                    return make_tree(records)
                end
            elseif k == "add_expert_info" or k == "append_text" or k == "set_text"
                or k == "add_proto_expert_info" then
                return function() end
            end
        end,
    })
    return t
end

-- =========================================================================
-- ProtoField / Proto / Field stub (记录型)
-- =========================================================================
local function field_maker(ftype)
    return function(name, label, base, vals, mask)
        return { __fname = name, __ftype = ftype, __mask = mask, __vals = vals }
    end
end
local ProtoField = {
    uint8 = field_maker("uint8"), uint16 = field_maker("uint16"),
    uint24 = field_maker("uint24"), uint32 = field_maker("uint32"),
    uint64 = field_maker("uint64"), ether = field_maker("ether"),
    string = field_maker("string"), bytes = field_maker("bytes"),
    bool = field_maker("bool"), double = field_maker("double"),
}

-- =========================================================================
-- 加载插件, 捕获 gw.dissector
-- =========================================================================
local function load_plugin()
    local dissector_fn = nil
    local G = {}
    for k, v in pairs(_G) do G[k] = v end
    G._G = G
    G.Proto = function(name, desc)
        local p = { __proto = true, name = name, desc = desc }
        return setmetatable(p, {
            __newindex = function(_, k, v)
                if k == "dissector" then dissector_fn = v end
                rawset(p, k, v)
            end,
        })
    end
    G.ProtoField = ProtoField
    G.Field = { new = function() return function() return 45 end end }  -- 载波 (USER0)
    G.base = { DEC = 10, HEX = 16, OCT = 8, NONE = 0, ASCII = 1, RANGE = 2 }
    G.wtap = {}
    -- 万能 stub 兜底 (Dissector/DissectorTable/ENC_* 等)
    local function new_stub()
        return setmetatable({}, {
            __index = function(_, k)
                if k == "PI_PROTOCOL" or k == "PI_WARN" or k == "PI_MALFORMED"
                    or k == "PI_ERROR" or k == "PI_NOTE" then return 0 end
                return new_stub()
            end,
            __call = function() return new_stub() end,
        })
    end
    setmetatable(G, { __index = function(_, k) return new_stub() end })

    local chunk, err = loadfile(PLUGIN_DIR .. "/" .. PLUGIN, "t", G)
    if not chunk then return nil, "loadfile: " .. tostring(err) end
    local ok, perr = pcall(chunk)
    if not ok then return nil, "加载报错: " .. tostring(perr) end
    if not dissector_fn then return nil, "未捕获 gw.dissector" end
    return dissector_fn, nil
end

-- =========================================================================
-- SOF 帧构造: 给定 MMe body (不含 MMe 头 4B), 返回完整 MPDU bytes (1-based)
-- FC(16B) + PB头(1B) + MSDU头(16B) + MMe头(4B) + MMe body + MSDU CRC32(4B)
-- =========================================================================
local function build_sof(mmtype, mme_body)
    local msdu_len = 4 + #mme_body   -- MMe 头 4B + body (11bit 字段)
    local f = {}
    -- FC 16B
    f[1] = 0x01            -- byte0: dt=1 (SOF 数据帧)
    f[2] = 0x34; f[3] = 0x12  -- byte1-2: NID (LE)
    f[4] = 0x00            -- byte3: 保留
    f[5] = 0x01            -- byte4: 源 TEI 低8 (TEI=1 CCO)
    f[6] = 0x00            -- byte5: 源TEI高4(bit0-3)=0 | 目的TEI低4(bit4-7)=0
    f[7] = 0x01            -- byte6: 目的 TEI 高8
    f[8] = 0x00; f[9] = 0x00   -- byte7-8
    f[10] = 0x00           -- byte9
    f[11] = 0x00           -- byte10: mod_mode=0(bit1) mod_param=0(bit2-6→tmi=0→pbsz=520) pb_num=0(bit7)
    f[12] = 0x00           -- byte11: pb_num bit0 = 0 (1 块)
    f[13] = 0x00; f[14] = 0x00; f[15] = 0x00; f[16] = 0x00  -- byte12-15
    -- PB 头 1B (byte16)
    f[17] = 0x00
    -- MSDU 头 16B (byte17-32, mac_off=17)
    local m = 17            -- MSDU 头起始 offset (0-based)
    f[m+1] = 0x00           -- byte17: version=0 (bit0-3)
    f[m+2] = 0x00           -- byte18: OSTEI 低8
    f[m+3] = 0x00           -- byte19: ODTEI 低8
    f[m+4] = 0x00           -- byte20: send_type
    f[m+5] = 0x00           -- byte21: retry/aggregate/rsv0
    f[m+6] = 0x00           -- byte22: msdu_seq 低
    f[m+7] = 0x00           -- byte23: msdu_seq 高
    f[m+8] = 0x00           -- byte24: msdu_type = 0 (管理消息)
    f[m+9] = band(msdu_len, 0xFF)        -- byte25: msdu_len 低8
    f[m+10] = band(math.floor(msdu_len / 256), 0x07)  -- byte26: msdu_len 高3
    f[m+11] = 0x00          -- byte27: restart/proxy
    f[m+12] = 0x00          -- byte28: route_total/left
    f[m+13] = 0x00          -- byte29: bcast/path/addr_flag=0/send_seq高4
    f[m+14] = 0x00          -- byte30: send_seq 低8
    f[m+15] = 0x00          -- offset31: net_seq (byte14)
    f[m+16] = 0x00          -- offset32: link_id (byte15)
    -- MSDU 头 16B = offset 17-32
    -- MMe 头 4B (offset 33-36, msdu_off=33)
    local mm = 33           -- MMe 头起始 offset
    f[mm+1] = band(mmtype, 0xFF)         -- offset33: mmtype 低8
    f[mm+2] = band(math.floor(mmtype / 256), 0xFF)  -- offset34: mmtype 高8
    f[mm+3] = 0x00          -- offset35: 保留
    f[mm+4] = 0x00          -- offset36: 保留
    -- MMe body (byte37 起)
    for i = 1, #mme_body do
        f[mm+4+i] = mme_body[i]
    end
    -- MSDU CRC32 4B (body 之后, 不参与 body_tvb)
    local crc_off = mm + 4 + #mme_body
    f[crc_off+1] = 0x00; f[crc_off+2] = 0x00; f[crc_off+3] = 0x00; f[crc_off+4] = 0x00
    return f
end

-- =========================================================================
-- 信标帧构造: 给定条目列表 {{etype=.., data={..}}, ...}, 返回完整 MPDU bytes
-- FC(16B, dt=0) + PB头(1B) + 固定头20B + 管理区(条目) + padding + CRC32(4B) + PBCRC24(3B)
-- tmi=0 → pbsize=520 (beacon_pb_size)
-- =========================================================================
local function build_beacon(entries)
    local pbsize = 520
    local total = 16 + pbsize
    local f = {}
    for i = 1, total do f[i] = 0x00 end
    -- FC 16B
    f[1] = 0x00            -- byte0: dt=0 (信标)
    f[2] = 0x34; f[3] = 0x12  -- byte1-2: NID
    f[5] = 0x01            -- byte4: 源 TEI 低8 (TEI=1)
    -- byte9 bit4-7 = tmi = 0 (已是 0) → pbsize=520
    -- PB 头 byte16 (f[17]) = 0
    -- 固定头 20B (byte16-35, off=16): 全 0
    -- 管理区 byte36 起
    local mgmt_off = 36    -- 0-based offset = byte36
    f[mgmt_off + 1] = #entries -- beacon_entry_cnt
    local e_off = mgmt_off + 1 -- byte37: 条目起始
    for n, ent in ipairs(entries) do
        local etype = ent.etype
        local data = ent.data
        local len_bytes = (etype >= 0xC0) and 2 or 1
        f[e_off + 1] = etype
        local len_raw = 1 + len_bytes + #data  -- 头 + len + data
        if len_bytes == 2 then
            f[e_off + 2] = band(len_raw, 0xFF)
            f[e_off + 3] = band(math.floor(len_raw / 256), 0xFF)
        else
            f[e_off + 2] = band(len_raw, 0xFF)
        end
        for i = 1, #data do
            f[e_off + 1 + len_bytes + i] = data[i]
        end
        e_off = e_off + 1 + len_bytes + #data
    end
    -- 其余字节为 padding/CRC32/PBCRC24 (全 0, 不影响断言)
    return f
end

-- =========================================================================
-- 断言框架
-- =========================================================================
local fails, total = 0, 0
local function check(name, got, expect)
    total = total + 1
    local ok = (got == expect)
    if not ok then fails = fails + 1 end
    print(string.format("  %s %-55s got=%s expect=%s", ok and "PASS" or "FAIL",
        name, tostring(got), tostring(expect)))
end

-- 文案断言 (显示实际文案, 便于对照; expect 可为 table → 集合成员匹配, 兼容 zh/en locale)
local function check_txt(name, got, expect)
    total = total + 1
    local ok
    if type(expect) == "table" then
        ok = false
        for _, e in ipairs(expect) do if got == e then ok = true break end end
    else
        ok = (got == expect)
    end
    if not ok then fails = fails + 1 end
    local exp_str = type(expect) == "table" and ("[" .. table.concat(expect, "|") .. "]") or expect
    print(string.format("  %s %-55s got='%s' expect='%s'", ok and "PASS" or "FAIL",
        name, tostring(got), exp_str))
end

-- 从 records 提取某字段的 value 序列 (按出现顺序)
local function field_vals(records, name)
    local out = {}
    for _, r in ipairs(records) do
        if r.name == name then out[#out + 1] = r.value end
    end
    return out
end

local function field_rlen(records, name, idx)
    local n = 0
    for _, r in ipairs(records) do
        if r.name == name then
            n = n + 1
            if n == idx then return r.rlen end
        end
    end
    return nil
end

-- 字段值 → 字典文案 (field_vals 同序, 取第 idx 条的 values[value])
local function field_texts(records, name, idx)
    local n = 0
    for _, r in ipairs(records) do
        if r.name == name then
            n = n + 1
            if n == (idx or 1) then return r.text end
        end
    end
    return nil
end

-- 跑一次 dissector, 返回 records
local function run_dissector(dissector_fn, frame_bytes)
    local records = {}
    local tvb = make_tvb(frame_bytes)
    local pinfo = { cols = { info = "" } }
    local tree = make_tree(records)
    local ok, perr = pcall(dissector_fn, tvb, pinfo, tree)
    if not ok then
        print("  [DEBUG] dissector 异常: " .. tostring(perr))
        return records
    end
    return records
end

-- =========================================================================
-- 主流程
-- =========================================================================
local dissector_fn, err = load_plugin()
if not dissector_fn then
    print("FATAL: " .. tostring(err))
    os.exit(1)
end
print("插件加载成功, gw.dissector 已捕获")

-- ---- A1: 时隙分配 (表27/28/29/30, 信标条目 0xC0) ----
print("\n== A1 表27 时隙分配: 17B 头 + CSMA/绑定CSMA 3B 条目 ==")
do
    -- 17B 头: nc=2, byte1=0x61(中央信标1+CSMA相线2+绑定CSMA相线1), NTB=0x12345678,
    -- 周期=0x0000C350, RF信标时隙长度 10bit=682(0x2AA)
    -- 非中央信标 2条(2B/条) + CSMA 2条(3B/条) + 绑定CSMA 1条(3B)
    local slot = hex("02 61 00 00 50 30 07  78 56 34 12  50 C3 00 00  AA 02"
        .. "  23 01 56 14  64 00 01 C8 00 02  2C 01 03")
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0xC0, data = slot}}))
    local ntb = field_vals(rec, "gw_2026.beacon.slot.bp_start_ntb")
    local bplen = field_vals(rec, "gw_2026.beacon.slot.bp_len")
    local rflen = field_vals(rec, "gw_2026.beacon.slot.rf_beacon_len")
    local csma_len = field_vals(rec, "gw_2026.beacon.csma.len")
    local csma_phase = field_vals(rec, "gw_2026.beacon.csma.phase")
    local bcsma_len = field_vals(rec, "gw_2026.beacon.bcsma.len")
    local bcsma_phase = field_vals(rec, "gw_2026.beacon.bcsma.phase")
    check("A1 NTB=0x12345678 (off+7)", ntb[1], 0x12345678)
    check("A1 周期=0x0000C350 (off+11)", bplen[1], 0x0000C350)
    check("A1 RF信标时隙长度=682 (10bit@off+15)", rflen[1], 682)
    check("A1 CSMA 2 条", #csma_len, 2)
    check("A1 CSMA[0] 长度=100", csma_len[1], 100)
    check("A1 CSMA[1] 长度=200", csma_len[2], 200)
    check("A1 CSMA[0] 相线=1", csma_phase[1], 1)
    check("A1 CSMA[1] 相线=2", csma_phase[2], 2)
    check("A1 绑定CSMA 1 条", #bcsma_len, 1)
    check("A1 绑定CSMA 长度=300", bcsma_len[1], 300)
    check("A1 绑定CSMA 相线=3", bcsma_phase[1], 3)
    -- B-0: 位域字段显示值 == 右移后的位域值(非 band(byte,mask) 未右移的整字节)
    local csma_pc = field_vals(rec, "gw_2026.beacon.slot.csma_phase_cnt")
    local bcsma_pc = field_vals(rec, "gw_2026.beacon.slot.bcsma_phase_cnt")
    local ncb_type = field_vals(rec, "gw_2026.beacon.ncb.type")
    local ncb_rf_flag = field_vals(rec, "gw_2026.beacon.ncb.rf_flag")
    check("B-0 CSMA相线数 显示值=2 (右移后, 非 0x20)", csma_pc[1], 2)
    check("B-0 绑定CSMA相线数 显示值=1 (右移后, 非 0x40)", bcsma_pc[1], 1)
    check("B-0 非中央信标[0] type=0 (右移后)", ncb_type[1], 0)
    check("B-0 非中央信标[1] type=1 (右移后, 非 0x10)", ncb_type[2], 1)
    check("B-0 非中央信标[0] rf_flag=0 (右移后)", ncb_rf_flag[1], 0)
end

-- ---- A2: 表83 邻居 Option (mmtype 0x0080) ----
print("\n== A2 表83 无线信道冲突上报: 每邻居 2B (ch + Option) ==")
do
    -- MMe body: CCO MAC(6) + 邻居数(1)=2 + 邻居0(ch=5,opt=1) + 邻居1(ch=9,opt=2)
    local body = hex("00 11 22 33 44 55  02  05 01  09 02")
    local rec = run_dissector(dissector_fn, build_sof(0x0080, body))
    local ch = field_vals(rec, "gw_2026.rfccr.nbr_ch")
    local opt = field_vals(rec, "gw_2026.rfccr.nbr_opt")
    check("A2 邻居数==2", #ch, 2)
    check("A2 邻居0 ch=5", ch[1], 5)
    check("A2 邻居1 ch=9 (非 Option 顶替)", ch[2], 9)
    check("A2 邻居0 opt=1", opt[1], 1)
    check("A2 邻居1 opt=2", opt[2], 2)
end

-- ---- A3: 表73/74 网络号宽度可变 (mmtype 0x000A) ----
print("\n== A3 表73/74 网络冲突上报: NID 宽度按 byte7 ==")
do
    -- CCO MAC(6) + 邻居数(1)=2 + 宽度(1)=2 + NID0(2B LE) + NID1(2B LE)
    local body = hex("AA BB CC DD EE FF  02  02  78 56  21 43")
    local rec = run_dissector(dissector_fn, build_sof(0x000A, body))
    local nid = field_vals(rec, "gw_2026.ncr.nbr_nid")
    check("A3 邻居数==2", #nid, 2)
    check("A3 NID0=0x5678", nid[1], 0x5678)
    check("A3 NID1=0x4321", nid[2], 0x4321)
end
-- 宽度=3 的边界
do
    local body = hex("AA BB CC DD EE FF  01  03  78 56 34")
    local rec = run_dissector(dissector_fn, build_sof(0x000A, body))
    local nid = field_vals(rec, "gw_2026.ncr.nbr_nid")
    check("A3 宽度=3 NID0=0x345678", nid[1], 0x345678)
end
-- 宽度=1 的边界
do
    local body = hex("AA BB CC DD EE FF  01  01  7F")
    local rec = run_dissector(dissector_fn, build_sof(0x000A, body))
    local nid = field_vals(rec, "gw_2026.ncr.nbr_nid")
    check("A3 宽度=1 NID0=0x7F", nid[1], 0x7F)
end

-- ---- A4: 表79 奇序号取位 (mmtype 0x000C) ----
print("\n== A4 表79 过零NTB差分: 奇序号取高位 ==")
do
    -- MMe body: TEI(2) + total(1)=2 + ph1(1)=2 + ph2(1)=0 + ph3(1)=0 + base_ntb(4) + 差分(3)=23 61 45
    local body = hex("01 00  02  02 00 00  00 00 00 00  23 61 45")
    local rec = run_dissector(dissector_fn, build_sof(0x000C, body))
    local d = field_vals(rec, "gw_2026.zr.ph1_diff")
    check("A4 ph1 有 2 条差分", #d, 2)
    check("A4 diff[0]=0x123", d[1], 0x123)
    check("A4 diff[1]=0x456 (奇序号取高位)", d[2], 0x456)
end

-- ---- A5: 表78 相位游标隔离 (mmtype 0x000C) ----
print("\n== A5 表78 相位游标隔离: 奇数条不跨相重叠 ==")
do
    -- ph1=1(奇) + ph2=1 → ph2 首条应从 byte1 高4 起(不共用 byte0)
    -- 差分 3B: 11 22 33 → ph1 diff=0x211, ph2 diff=0x332
    local body = hex("01 00  02  01 01 00  00 00 00 00  11 22 33")
    local rec = run_dissector(dissector_fn, build_sof(0x000C, body))
    local d1 = field_vals(rec, "gw_2026.zr.ph1_diff")
    local d2 = field_vals(rec, "gw_2026.zr.ph2_diff")
    check("A5 ph1 diff=0x211", d1[1], 0x211)
    check("A5 ph2 diff=0x332 (不跨相重叠)", d2[1], 0x332)
end

-- ---- A6: body 按 body_len 截断 (mmtype 0x004F 诊断) ----
print("\n== A6 管理消息体按 body_len 截断 (表80 诊断) ==")
do
    -- MMe body: vendor_id(2B LE)=0x1234 + custom(3B)=AA BB CC
    local body = hex("34 12  AA BB CC")
    local rec = run_dissector(dissector_fn, build_sof(0x004F, body))
    local vid = field_vals(rec, "gw_2026.diag.vendor_id")
    check("A6 vendor_id=0x1234", vid[1], 0x1234)
    -- custom 区长度 = body_len - 2 = 3 (不含 MSDU 尾 CRC32 4B)
    check("A6 custom 区长度=3 (截断, 不含 CRC32)", field_rlen(rec, "gw_2026.diag.custom", 1), 3)
end

-- ---- L-2: 表40 关联请求 byte16 备选相位 (mmtype 0x0000) ----
print("\n== L-2 表40 关联请求 byte16: 主相线 + 两个备选相位 ==")
do
    local body = {}
    for i = 1, 88 do body[i] = 0x00 end
    body[17] = 0x39  -- byte16: bit0-1 主=1(A) bit2-3 备选1=2(B) bit4-5 备选2=3(C)
    local rec = run_dissector(dissector_fn, build_sof(0x0000, body))
    local ph = field_vals(rec, "gw_2026.assoc_req.phase")
    local alt1 = field_vals(rec, "gw_2026.assoc_req.phase_alt1")
    local alt2 = field_vals(rec, "gw_2026.assoc_req.phase_alt2")
    check("L-2 主相线=1 (bit0-1)", ph[1], 1)
    check("L-2 备选相位1=2 (bit2-3)", alt1[1], 2)
    check("L-2 备选相位2=3 (bit4-5)", alt2[1], 3)
end

-- ---- L-3: 表49 代理变更请求 byte20 备选相位 (mmtype 0x0003) ----
print("\n== L-3 表49 代理变更请求 byte20: 主相线 + 两个备选相位 ==")
do
    local body = {}
    for i = 1, 24 do body[i] = 0x00 end
    body[21] = 0x39  -- byte20: bit0-1 主=1 bit2-3 备选1=2 bit4-5 备选2=3
    local rec = run_dissector(dissector_fn, build_sof(0x0003, body))
    local ph = field_vals(rec, "gw_2026.cproxy_req.phase")
    local alt1 = field_vals(rec, "gw_2026.cproxy_req.phase_alt1")
    local alt2 = field_vals(rec, "gw_2026.cproxy_req.phase_alt2")
    check("L-3 主相线=1 (bit0-1)", ph[1], 1)
    check("L-3 备选相位1=2 (bit2-3)", alt1[1], 2)
    check("L-3 备选相位2=3 (bit4-5)", alt2[1], 3)
end

-- ---- L-4: 表42 版本时间拆 年7/月4/日5 (mmtype 0x0000) ----
print("\n== L-4 表42 版本时间: 拆 年7+月4+日5 ==")
do
    local body = {}
    for i = 1, 88 do body[i] = 0x00 end
    body[47] = 0x9A  -- byte46: 年26(bit0-6) + 月 bit0(bit7)=1
    body[48] = 0xDD  -- byte47: 月 bit1-3=101 + 日27(bit3-7)
    local rec = run_dissector(dissector_fn, build_sof(0x0000, body))
    local yr = field_vals(rec, "gw_2026.assoc_req.ver_year")
    local mo = field_vals(rec, "gw_2026.assoc_req.ver_month")
    local dy = field_vals(rec, "gw_2026.assoc_req.ver_day")
    check("L-4 年=26 (7bit)", yr[1], 26)
    check("L-4 月=11 (4bit)", mo[1], 11)
    check("L-4 日=27 (5bit)", dy[1], 27)
end

-- ---- L-5: 表46 路由子站点表 (mmtype 0x0001 关联确认) ----
print("\n== L-5 表45/46 关联确认路由表: 直连站点 + 代理 + 子站点 ==")
do
    -- 固定头 40B(全0) + 表45 头 8B + 表46: 2 直连(2B/条) + 1 代理(4B+2 子站点)
    local body = {}
    for i = 1, 60 do body[i] = 0x00 end
    body[41] = 0x02; body[42] = 0x00  -- byte40-41: N=2 直连发现站点数
    body[43] = 0x01; body[44] = 0x00  -- byte42-43: M=1 直连代理数
    body[45] = 0x10; body[46] = 0x00  -- byte44-45: 路由表大小
    -- 表46 (byte48 起)
    body[49] = 0x01; body[50] = 0x01  -- 直连[0]: TEI=0x101, link=0
    body[51] = 0x02; body[52] = 0x11  -- 直连[1]: TEI=0x102, link=1(bit4)
    body[53] = 0x01; body[54] = 0x02  -- 代理[0]: TEI=0x201, link=0
    body[55] = 0x02; body[56] = 0x00  -- 代理[0] 子站点数=2
    body[57] = 0x01; body[58] = 0x03  -- 子[0]: TEI=0x301
    body[59] = 0x02; body[60] = 0x03  -- 子[1]: TEI=0x302
    local rec = run_dissector(dissector_fn, build_sof(0x0001, body))
    local sta = field_vals(rec, "gw_2026.assoc_cnf.route.sta_tei")
    local sta_link = field_vals(rec, "gw_2026.assoc_cnf.route.sta_link")
    local pco = field_vals(rec, "gw_2026.assoc_cnf.route.pco_tei")
    local pco_link = field_vals(rec, "gw_2026.assoc_cnf.route.pco_link")
    local ccnt = field_vals(rec, "gw_2026.assoc_cnf.route.pco_child_cnt")
    local child = field_vals(rec, "gw_2026.assoc_cnf.route.pco_child_tei")
    check("L-5 直连站点 2 条", #sta, 2)
    check("L-5 直连[0] TEI=0x101", sta[1], 0x101)
    check("L-5 直连[1] TEI=0x102", sta[2], 0x102)
    check("L-5 直连[0] link=0", sta_link[1], 0)
    check("L-5 直连[1] link=1", sta_link[2], 1)
    check("L-5 代理[0] TEI=0x201", pco[1], 0x201)
    check("L-5 代理[0] link=0", pco_link[1], 0)
    check("L-5 代理[0] 子站点数=2", ccnt[1], 2)
    check("L-5 子[0] TEI=0x301", child[1], 0x301)
    check("L-5 子[1] TEI=0x302", child[2], 0x302)
end

-- ---- P3: 表25 路由参数条目 6 字段 (信标条目 0x01) ----
print("\n== P3 表25 路由参数条目: 6 字段 16+16+8+8+8+8 ==")
do
    -- 路由周期=80(2B) + 剩余=60(2B) + 发现列表周期=30(1B) + 老化个数=5(1B) + 无线周期=20(1B) + 无线老化=3(1B)
    local data = hex("50 00 3C 00 1E 05 14 03")
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0x01, data = data}}))
    local rp = field_vals(rec, "gw_2026.beacon.rp.route_period")
    local rr = field_vals(rec, "gw_2026.beacon.rp.route_remain")
    local dp = field_vals(rec, "gw_2026.beacon.rp.disc_list_period")
    local ac = field_vals(rec, "gw_2026.beacon.rp.rate_age_cnt")
    local rdp = field_vals(rec, "gw_2026.beacon.rfp.dl_period")
    local rac = field_vals(rec, "gw_2026.beacon.rfp.age_cnt")
    check("P3 路由周期=80 (16bit)", rp[1], 80)
    check("P3 路由评估剩余=60 (16bit)", rr[1], 60)
    check("P3 发现列表周期=30 (8bit)", dp[1], 30)
    check("P3 接收率老化周期个数=5 (8bit)", ac[1], 5)
    check("P3 无线发现列表周期=20 (8bit)", rdp[1], 20)
    check("P3 无线接收率老化周期个数=3 (8bit)", rac[1], 3)
end

-- ---- P2: 表23 条目类型: 0x07 信标机制切换, 0x03/0x08 保留 ----
print("\n== P2 表23 条目类型: 0x07 信标机制切换 / 0x03、0x08 保留 ==")
do
    -- 0x07 信标机制切换(表33): 目标机制 ID=2 + 切换 NTB=0x12345678
    local data = hex("02 78 56 34 12")
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0x07, data = data}}))
    local tgt = field_vals(rec, "gw_2026.beacon.sw_target")
    local ntb = field_vals(rec, "gw_2026.beacon.sw_ntb")
    check("P2 0x07 切换目标机制ID=2", tgt[1], 2)
    check("P2 0x07 切换NTB=0x12345678", ntb[1], 0x12345678)
    local ca = field_vals(rec, "gw_2026.beacon.ca_period")
    check("P2 0x07 不再按信道评估参数解析", #ca, 0)
end
do
    -- 0x03 保留: 不深解析(无路由参数字段产出)
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0x03, data = hex("11 22")}}))
    local rp = field_vals(rec, "gw_2026.beacon.rp.route_period")
    check("P2 0x03 保留不深解析", #rp, 0)
end
do
    -- 0x08 保留: 不深解析(无切换字段产出)
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0x08, data = hex("02 78 56 34 12")}}))
    local tgt = field_vals(rec, "gw_2026.beacon.sw_target")
    check("P2 0x08 保留不深解析", #tgt, 0)
end

-- ---- L-9/L-10: 设备类型枚举 + 厂商代码 ASCII (mmtype 0x0000 关联请求) ----
print("\n== L-9/L-10 表40/41: 设备类型 + 厂商代码 ASCII ==")
do
    local body = {}
    for i = 1, 88 do body[i] = 0x00 end
    body[18] = 0x02   -- byte17: 设备类型=2 (融合终端本地通信单元)
    body[49] = 0x41   -- byte48: 厂商代码 ASCII 'A'
    body[50] = 0x42   -- byte49: 厂商代码 ASCII 'B'
    local rec = run_dissector(dissector_fn, build_sof(0x0000, body))
    local dt = field_vals(rec, "gw_2026.assoc_req.dev_type")
    local vc = field_vals(rec, "gw_2026.assoc_req.vendor_code")
    check("L-9 设备类型=2 (枚举=融合终端本地通信单元)", dt[1], 2)
    check("L-10 厂商代码 ASCII 显示 = AB", vc[1], "AB")
end

-- ---- L-6: 表49 代理变更原因枚举 (mmtype 0x0003) ----
print("\n== L-6 表49 代理变更原因/代理类型枚举 ==")
do
    local body = {}
    for i = 1, 24 do body[i] = 0x00 end
    body[15] = 0x00  -- byte14: 代理类型=0 (站点动态选择的代理)
    body[16] = 0x01  -- byte15: 原因=1 (周期代理变更)
    local rec = run_dissector(dissector_fn, build_sof(0x0003, body))
    local pt = field_vals(rec, "gw_2026.cproxy_req.proxy_type")
    local rs = field_vals(rec, "gw_2026.cproxy_req.reason")
    check("L-6 代理类型=0 (站点动态选择的代理)", pt[1], 0)
    check("L-6 原因=1 (周期代理变更)", rs[1], 1)
end

-- ---- L-8: 表47/50 结果枚举 ----
print("\n== L-8 表47/50 结果枚举 ==")
do
    -- 表50 代理变更确认: byte0 结果=0 (变更成功)
    local body = {}
    for i = 1, 20 do body[i] = 0x00 end
    local rec = run_dissector(dissector_fn, build_sof(0x0004, body))
    local r1 = field_vals(rec, "gw_2026.cproxy_cnf.result")
    check("L-8 表50 代理变更结果=0 (变更成功)", r1[1], 0)
    -- 表47 关联汇总指示: byte0 结果=0 (允许入网)
    local rec2 = run_dissector(dissector_fn, build_sof(0x0002, {0x00}))
    local r2 = field_vals(rec2, "gw_2026.assoc_gather.result")
    check("L-8 表47 汇总结果=0 (允许入网)", r2[1], 0)
end

-- ---- P8: 表28 无线信标标志 0-6 释义 (信标时隙分配条目) ----
print("\n== P8 表28 无线信标标志 bit5-7 ==")
do
    -- 非中央信标 byte1 = TEI高4(bit0-3) + type(bit4) + rf_flag(bit5-7)=5
    local slot = hex("01 60 00 00 00 00 00  00 00 00 00  00 00 00 00  00 00"
        .. "  00 A1")  -- 非中央信标[0]: TEI=0x100, type=0, rf_flag=5(bit5-7)
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0xC0, data = slot}}))
    local rf = field_vals(rec, "gw_2026.beacon.ncb.rf_flag")
    check("P8 无线信标标志=5 (同隙载波+无线标准)", rf[1], 5)
end

-- ---- L-7: 保留位渲染 (表40 关联请求, 保留位非零验证) ----
print("\n== L-7 保留位渲染: 表40 相线/模块类型/byte57-59 ==")
do
    local body = {}
    for i = 1, 88 do body[i] = 0x00 end
    body[17] = 0xF9   -- byte16: 相线0x39(bit0-5) + 保留 bit6-7=3
    body[20] = 0xFF   -- byte19: 模块类型=3(bit0-1) + 保留 bit2-7=63
    body[58] = 0xAA; body[59] = 0xBB; body[60] = 0xCC  -- byte57-59 保留
    local rec = run_dissector(dissector_fn, build_sof(0x0000, body))
    local pr = field_vals(rec, "gw_2026.assoc_req.phase_rsv")
    local mr = field_vals(rec, "gw_2026.assoc_req.module_rsv")
    local rv = field_vals(rec, "gw_2026.assoc_req.rsv")
    check("L-7 相线保留位=3 (bit6-7)", pr[1], 3)
    check("L-7 模块类型保留位=63 (bit2-7)", mr[1], 63)
    check("L-7 byte57-59 保留区渲染 (len=3)", field_rlen(rec, "gw_2026.assoc_req.rsv", 1), 3)
end
do
    -- 表29/30 CSMA 时隙相线保留 bit2-7: 17B 头(CSMA相线数=2) + 2 条 CSMA(3B/条)
    -- CSMA[0]: 长度=64(40 00) + 相线字节 0x81(相线=1 + 保留=32)
    local slot2 = "00 60 00 00 00 00 00  00 00 00 00  00 00 00 00  00 00"
        .. "  40 00 81  64 00 02"
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0xC0, data = hex(slot2)}}))
    local cr = field_vals(rec, "gw_2026.beacon.csma.slot_rsv")
    check("L-7 CSMA[0] 相线保留位=32 (bit2-7)", cr[1], 32)
    check("L-7 CSMA[1] 相线保留位=0", cr[2], 0)
end

-- ---- P7 返工: 相线两套字典 + beacon_phase 右移 (=0 断言) ----
print("\n== P7R 相线字典拆分: 0 值语义文案级断言 (表5/40=未知, 表24=全相线) ==")
do
    -- ar_phase=0 → 文案「未知」 (表40, 挂 phase_vals_unknown)
    local body = {}
    for i = 1, 88 do body[i] = 0x00 end
    local rec = run_dissector(dissector_fn, build_sof(0x0000, body))
    local ph = field_vals(rec, "gw_2026.assoc_req.phase")
    check("P7R ar_phase=0 值仍为 0", ph[1], 0)
    check_txt("P7R ar_phase=0 文案=未知", field_texts(rec, "gw_2026.assoc_req.phase"), {"未知", "Unknown"})
end
do
    -- ent_phase=0 → 文案「全相线」 (表24 站点能力条目, 挂 phase_vals_all_lines)
    local cap = hex("01 00 00 00 64 AA BB CC DD EE FF 00 00 00")  -- 13B, byte12 相线=0
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0x00, data = cap}}))
    local ph = field_vals(rec, "gw_2026.beacon.cap.phase")
    check("P7R ent_phase=0 值仍为 0", ph[1], 0)
    check_txt("P7R ent_phase=0 文案=全相线", field_texts(rec, "gw_2026.beacon.cap.phase"), {"全相线", "All lines"})
end
do
    -- beacon_phase=0 → 文案「未知」 (表5 byte11 bit1-2, 右移后 0-3)
    local rec = run_dissector(dissector_fn, build_beacon({{etype = 0x01, data = hex("50 00 3C 00 1E 05 14 03")}}))
    local ph = field_vals(rec, "gw_2026.beacon.phase")
    check("P7R beacon_phase=0 值仍为 0", ph[1], 0)
    check_txt("P7R beacon_phase=0 文案=未知", field_texts(rec, "gw_2026.beacon.phase"), {"未知", "Unknown"})
end

print(string.format("\nRESULT: %s (%d/%d pass)", fails == 0 and "ALL PASS" or (fails .. " FAIL"), total - fails, total))
os.exit(fails == 0 and 0 or 1)
