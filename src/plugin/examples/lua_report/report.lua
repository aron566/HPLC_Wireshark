-- lua_report/report.lua — Statistics report plugin
-- Plugin API v1: get_info() + parse(frame)
-- Counts frame types and TEI distribution across all parsed frames.
-- get_report() returns the statistics as a JSON string.

local g_total = 0
local g_frame_types = {}  -- frameType -> count
local g_tei_count = {}     -- tei -> count

function get_info()
    return {
        protocolId = "LUAREPORT_2024",
        displayName = "Lua Statistics Report"
    }
end

function parse(frame)
    local bytes = frame.data or {}
    local n = #bytes
    -- frame type: low nibble of the first byte
    local frameType = n > 0 and (bytes[1] % 16) or 0
    local srcTei = n > 2 and bytes[3] or 0
    local dstTei = n > 3 and bytes[4] or 0

    g_total = g_total + 1
    g_frame_types[frameType] = (g_frame_types[frameType] or 0) + 1
    if srcTei ~= 0 then
        g_tei_count[srcTei] = (g_tei_count[srcTei] or 0) + 1
    end
    if dstTei ~= 0 and dstTei ~= srcTei then
        g_tei_count[dstTei] = (g_tei_count[dstTei] or 0) + 1
    end

    return {
        accept = true,
        summary = "Report: frame #" .. g_total .. " type=" .. frameType,
        mpdu = {
            frameType = frameType,
            srcTei = srcTei,
            dstTei = dstTei
        },
        fields = {
            { name = "FrameType", value = tostring(frameType) },
            { name = "TotalFrames", value = tostring(g_total) }
        }
    }
end

-- Minimal JSON encoder for the stats tables (keys sorted for determinism).
local function encode_stats()
    local parts = {}
    parts[#parts + 1] = '{"total":' .. g_total .. ',"frameTypes":{'

    local fkeys = {}
    for k in pairs(g_frame_types) do fkeys[#fkeys + 1] = k end
    table.sort(fkeys)
    for i, k in ipairs(fkeys) do
        if i > 1 then parts[#parts + 1] = ',' end
        parts[#parts + 1] = '"' .. k .. '":' .. g_frame_types[k]
    end

    parts[#parts + 1] = '},"teiDist":{'
    local tkeys = {}
    for k in pairs(g_tei_count) do tkeys[#tkeys + 1] = k end
    table.sort(tkeys)
    for i, k in ipairs(tkeys) do
        if i > 1 then parts[#parts + 1] = ',' end
        parts[#parts + 1] = '"' .. k .. '":' .. g_tei_count[k]
    end
    parts[#parts + 1] = '}}'
    return table.concat(parts)
end

function get_report()
    return encode_stats()
end
