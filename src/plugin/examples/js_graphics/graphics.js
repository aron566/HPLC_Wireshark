/// JS 图形插件示例:绘制 3x3 色块,点击切换颜色
/// JS graphics demo: 3x3 color grid, click to toggle

var colors = [
    ["#ff0000", "#00ff00", "#0000ff"],
    ["#ffff00", "#ff00ff", "#00ffff"],
    ["#ff8800", "#88ff00", "#0088ff"]
];
var selected = { row: -1, col: -1 };
var view_w = 400, view_h = 300;

function get_info() {
    return {
        protocolId: "JSGRAPH_2024",
        displayName: "JS Graphics Demo"
    };
}

function parse(frame) {
    return { accept: false, rejectReason: "graphics only" };
}

function render(p, w, h) {
    view_w = w; view_h = h;
    p.clear("#ffffff");
    p.set_font("Sans", 14, true);
    p.set_pen("#000000", 1);
    p.draw_text(10, 25, "JS Graphics Demo - click a block");

    var cols = 3, rows = 3;
    var margin = 10, top = 35;
    var cw = (w - margin * 2) / cols;
    var ch = (h - top - margin) / rows;

    for (var r = 0; r < rows; r++) {
        for (var c = 0; c < cols; c++) {
            var x = margin + c * cw;
            var y = top + r * ch;
            p.fill_rect(x + 2, y + 2, cw - 4, ch - 4, colors[r][c]);
            if (r === selected.row && c === selected.col) {
                p.set_pen("#000000", 3);
            } else {
                p.set_pen("#888888", 1);
            }
            p.draw_rect(x + 2, y + 2, cw - 4, ch - 4);
        }
    }
}

function on_event(type, x, y, button, modifiers, delta) {
    // type 0=press
    if (type === 0) {
        var margin = 10, top = 35;
        var cw = (view_w - margin * 2) / 3;
        var ch = (view_h - top - margin) / 3;
        var c = Math.floor((x - margin) / cw);
        var r = Math.floor((y - top) / ch);
        if (r >= 0 && r < 3 && c >= 0 && c < 3) {
            selected.row = r;
            selected.col = c;
            return true;
        }
    }
    return false;
}
