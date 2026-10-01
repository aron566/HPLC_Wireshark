-- lua_diag/diag.lua — Diagnostic plugin
-- Plugin API v1: get_info() + parse(frame)
-- Checks frames for: abnormal length (<10 or >1500), zero TEI,
-- all-zero payload. Always accepts; problems are reported in fields.

function get_info()
    return {
        protocolId = "LUADIAG_2024",
        displayName = "Lua Frame Diagnostics"
    }
end

function parse(frame)
    local bytes = frame.data or {}
    local n = #bytes
    local problems = {}

    -- 1. length check
    if n < 10 then
        problems[#problems + 1] = {
            name = "Length",
            value = "too short: " .. n .. " bytes (< 10)"
        }
    elseif n > 1500 then
        problems[#problems + 1] = {
            name = "Length",
            value = "too long: " .. n .. " bytes (> 1500)"
        }
    end

    -- 2. TEI check (same byte convention as the echo plugins)
    local srcTei = n > 2 and bytes[3] or 0
    local dstTei = n > 3 and bytes[4] or 0
    if srcTei == 0 then
        problems[#problems + 1] = {
            name = "SrcTei",
            value = "TEI is 0 (unassigned)"
        }
    end
    if dstTei == 0 then
        problems[#problems + 1] = {
            name = "DstTei",
            value = "TEI is 0 (unassigned)"
        }
    end

    -- 3. all-zero payload check
    if n > 0 then
        local allZero = true
        for i = 1, n do
            if bytes[i] ~= 0 then allZero = false; break end
        end
        if allZero then
            problems[#problems + 1] = {
                name = "Payload",
                value = "all-zero payload"
            }
        end
    end

    local diagNode = {
        name = "Diagnosis",
        value = #problems == 0 and "OK" or (#problems .. " problem(s)")
    }
    if #problems > 0 then
        diagNode.children = problems
    end

    return {
        accept = true,
        summary = "Diag: " .. n .. " bytes, " ..
            (#problems == 0 and "OK" or (#problems .. " problem(s)")),
        mpdu = {
            frameType = 1,
            srcTei = srcTei,
            dstTei = dstTei
        },
        fields = {
            {
                name = "FrameLen",
                value = n .. " bytes",
                relStart = 0,
                relLen = n
            },
            { name = "SrcTei", value = tostring(srcTei) },
            { name = "DstTei", value = tostring(dstTei) },
            diagNode
        }
    }
end
