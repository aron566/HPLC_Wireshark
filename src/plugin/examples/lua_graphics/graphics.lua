--- Lua 图形插件示例:仪表盘,点击增加数值
--- Lua graphics demo: gauge, click to increment

local value = 42
local view_w, view_h = 400, 300

function get_info()
    return { protocolId = "LUAGRAPH_2024", displayName = "Lua Graphics Demo" }
end

function parse(frame)
    return { accept = false, rejectReason = "graphics only" }
end

function render(p, w, h)
    view_w, view_h = w, h
    p.clear("#ffffff")
    p.set_font("Sans", 16, true)
    p.set_pen("#000000", 1)
    p.draw_text(10, 30, "Lua Gauge Demo - click to +1")

    -- 表盘
    local cx, cy, r = w / 2, h / 2 + 20, math.min(w, h) / 2 - 40
    p.set_pen("#333333", 3)
    p.no_brush()
    p.draw_ellipse(cx - r, cy - r, r * 2, r * 2)

    -- 刻度
    p.set_pen("#888888", 1)
    for i = 0, 10 do
        local a = math.pi * (0.75 + i * 0.15)
        local x1 = cx + (r - 10) * math.cos(a)
        local y1 = cy + (r - 10) * math.sin(a)
        local x2 = cx + r * math.cos(a)
        local y2 = cy + r * math.sin(a)
        p.draw_line(x1, y1, x2, y2)
    end

    -- 指针
    local va = math.pi * (0.75 + (value / 100) * 1.5)
    p.set_pen("#ff0000", 3)
    p.draw_line(cx, cy, cx + (r - 20) * math.cos(va), cy + (r - 20) * math.sin(va))

    -- 数值
    p.set_pen("#000000", 1)
    p.set_font("Sans", 24, true)
    p.draw_text(cx - 30, cy + r + 30, tostring(value))
end

function on_event(type, x, y, button, modifiers, delta)
    -- type 0=press
    if type == 0 then
        value = (value + 1) % 101
        return true
    end
    return false
end
