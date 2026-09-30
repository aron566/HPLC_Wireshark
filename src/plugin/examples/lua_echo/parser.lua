-- lua_echo/parser.lua — Example Lua plugin
-- Plugin API v1: get_info() + parse(frame)

function get_info()
    return {
        protocolId = "LUAECHO_2024",
        displayName = "Lua Echo Parser"
    }
end

local function to_hex(v, w)
    local s = string.format("%X", v)
    while #s < w do s = "0" .. s end
    return "0x" .. s
end

function parse(frame)
    local bytes = frame.data  -- 1-based array of numbers
    if bytes == nil or #bytes == 0 then
        return { accept = false, rejectReason = "empty frame" }
    end

    -- Build a simple field tree: expand the first few bytes
    local fields = {}
    local n = math.min(#bytes, 8)
    for i = 1, n do
        fields[i] = {
            name = "Byte[" .. (i - 1) .. "]",
            value = to_hex(bytes[i], 2),
            relStart = i - 1,
            relLen = 1
        }
    end
    if #bytes > n then
        fields[n + 1] = {
            name = "...",
            value = (#bytes - n) .. " more bytes"
        }
    end

    -- Simple check: only accept frames whose first byte is 0xAA
    -- (demonstrates the reject path)
    if bytes[1] ~= 0xAA then
        return { accept = false, rejectReason = "first byte != 0xAA" }
    end

    return {
        accept = true,
        summary = "LuaEcho: " .. #bytes .. " bytes",
        mpdu = {
            frameType = 1,
            srcTei = #bytes > 2 and bytes[3] or 0,
            dstTei = #bytes > 3 and bytes[4] or 0
        },
        fields = {
            {
                name = "EchoFrame",
                value = to_hex(bytes[1], 2),
                relStart = 0,
                relLen = #bytes,
                children = fields
            }
        }
    }
end
