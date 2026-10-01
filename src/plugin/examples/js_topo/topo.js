// js_topo/topo.js — Topology graphics plugin (aligned with real TOPO page)
// Layout: left=hierarchical topology, right=TEI-MAC mapping table,
//         bottom=frame records (route events).
// Parses 0x3C frames: extracts TEI from MPDU, tracks parent via discovery.

var g_nodes = {};       // tei -> {tei, mac, parent, level, count, last_frame}
var g_order = [];       // insertion order
var g_events = [];      // frame records: {idx, tei, mac, parent, desc}
var g_frame_idx = 0;
var g_selected = -1;
var MAX_EVENTS = 50;

var EVT_PRESS = 0;
var NODE_R = 16;

function get_info() {
    return {
        protocolId: "JSTOPO_2024",
        displayName: "JS Topology View",
        displayNameEn: "JS Topology View"
    };
}

function mac_str(mac_bytes) {
    var parts = [];
    for (var i = 0; i < mac_bytes.length; i++) {
        var h = mac_bytes[i].toString(16);
        if (h.length < 2) h = "0" + h;
        parts.push(h);
    }
    return parts.join(":");
}

function ensure_node(tei) {
    if (tei === 0) return null;
    if (!g_nodes[tei]) {
        g_nodes[tei] = { tei: tei, mac: "", parent: 0, level: -1,
                         count: 0, last_frame: 0 };
        g_order.push(tei);
    }
    return g_nodes[tei];
}

// Parse: 使用后端提供的 MPDU 解析结果 (frame.mpdu)
// 不再做字节启发式猜测
function parse(frame) {
    var bytes = frame.data || [];
    g_frame_idx++;

    var mpdu = frame.mpdu || {};
    if (!mpdu.ok) {
        return { accept: false, summary: "Topo: MPDU parse failed" };
    }

    var src_tei = mpdu.srcTei || 0;
    var dst_tei = mpdu.dstTei || 0;
    var frame_type = mpdu.frameType || 0;

    // Ensure CCO exists
    ensure_node(1);

    var node = ensure_node(src_tei);
    var desc = "";
    if (node) {
        node.count++;
        node.last_frame = g_frame_idx;
        node.frame_type = frame_type;
        // 新节点默认挂到 CCO 下,等待发现列表/关联确认更新真实父节点
        if (node.parent === 0 && src_tei !== 1) {
            node.parent = 1;
            desc = "New node: TEI " + src_tei + " joined (parent=CCO)";
        }
        if (!desc) {
            var ftn = frame_type === 0 ? "BEACON" : frame_type === 1 ? "SOF" : "T" + frame_type;
            desc = "Frame #" + g_frame_idx + ": " + ftn + " TEI " + src_tei +
                   (dst_tei ? " -> " + dst_tei : "");
        }
    } else {
        desc = "Frame #" + g_frame_idx + ": no TEI";
    }

    // 也记录 dst TEI
    if (dst_tei && dst_tei !== src_tei) ensure_node(dst_tei);

    // Record event
    g_events.push({ idx: g_frame_idx, tei: src_tei, dst: dst_tei,
                    ftype: frame_type,
                    parent: node ? node.parent : 0, desc: desc });
    if (g_events.length > MAX_EVENTS) g_events.shift();

    // Recompute levels
    compute_levels();

    return {
        accept: true,
        summary: "Topo: TEI " + src_tei + " (" + g_order.length + " nodes)",
        mpdu: { frameType: frame_type, srcTei: src_tei, dstTei: dst_tei },
        fields: [
            { name: "SrcTei", value: String(src_tei) },
            { name: "DstTei", value: String(dst_tei) },
            { name: "FrameType", value: String(frame_type) },
            { name: "KnownNodes", value: String(g_order.length) }
        ]
    };
}

function compute_levels() {
    // BFS from CCO (TEI 1) along parent links
    for (var i = 0; i < g_order.length; i++)
        g_nodes[g_order[i]].level = -1;
    if (!g_nodes[1]) return;
    g_nodes[1].level = 0;
    var changed = true, iter = 0;
    while (changed && iter < 20) {
        changed = false; iter++;
        for (var j = 0; j < g_order.length; j++) {
            var n = g_nodes[g_order[j]];
            if (n.level !== -1 || n.parent === 0) continue;
            var p = g_nodes[n.parent];
            if (p && p.level !== -1) {
                n.level = p.level + 1;
                changed = true;
            }
        }
    }
    // Unreachable nodes go to level 1 (direct under CCO visually)
    for (var k = 0; k < g_order.length; k++) {
        var m = g_nodes[g_order[k]];
        if (m.level === -1 && m.tei !== 1) m.level = 1;
    }
}

function children_of(tei) {
    var out = [];
    for (var i = 0; i < g_order.length; i++) {
        var n = g_nodes[g_order[i]];
        if (n.parent === tei && n.tei !== tei) out.push(n.tei);
    }
    return out.sort(function(a, b) { return a - b; });
}

function layout_tree(x0, y0, w, h) {
    // Hierarchical: CCO top center, children in grid below to avoid overlap
    var pos = {};
    var max_level = 0;
    for (var i = 0; i < g_order.length; i++)
        max_level = Math.max(max_level, g_nodes[g_order[i]].level);

    // Group by level
    var by_level = {};
    for (var j = 0; j < g_order.length; j++) {
        var n = g_nodes[g_order[j]];
        if (!by_level[n.level]) by_level[n.level] = [];
        by_level[n.level].push(n.tei);
    }

    var levels = Object.keys(by_level).sort(function(a,b){return a-b;});
    var level_h = h / (levels.length + 1);

    for (var li = 0; li < levels.length; li++) {
        var lv = levels[li];
        var arr = by_level[lv].sort(function(a, b) { return a - b; });
        var y = y0 + level_h * (li + 0.8);

        // Grid layout for many nodes: multiple rows per level
        // 限制总高度不超出 h
        var max_per_row = Math.max(1, Math.floor(w / 70));
        var rows = Math.ceil(arr.length / max_per_row);
        var row_h = 45;
        // 如果行数太多超出高度,压缩行距
        var avail_h = h - (y - y0);
        if (rows * row_h > avail_h && rows > 1) {
            row_h = Math.max(32, avail_h / rows);
        }
        for (var k = 0; k < arr.length; k++) {
            var row = Math.floor(k / max_per_row);
            var col = k % max_per_row;
            var in_row = Math.min(max_per_row, arr.length - row * max_per_row);
            var x = x0 + w * (col + 1) / (in_row + 1);
            var yy = y + row * row_h;
            // 裁剪超出高度的节点
            if (yy > y0 + h - 20) continue;
            pos[arr[k]] = { x: x, y: yy };
        }
    }
    return pos;
}

function render(p, w, h) {
    p.clear("#1e1e1e");

    // Three panels: topo left (60%), mapping right (40%), events bottom (25%)
    var topo_w = Math.floor(w * 0.60);
    var map_x = topo_w;
    var map_w = w - topo_w;
    var evt_h = Math.floor(h * 0.28);
    var topo_h = h - evt_h;

    // 拓扑布局严格限制在 topo_h 内
    var pos = layout_tree(10, 30, topo_w - 20, topo_h - 50);

    // --- Topology panel ---
    p.set_pen("#555555", 1);
    p.draw_line(topo_w, 0, topo_w, h);
    p.draw_line(0, topo_h, w, topo_h);

    if (g_order.length === 0) {
        p.set_pen("#888888", 1);
        p.set_font("sans", 12, false);
        p.draw_text(20, 30, "No nodes yet - waiting for frames...");
    } else {
        // Links parent -> child
        p.set_pen("#3a7bd5", 1.5);
        for (var i = 0; i < g_order.length; i++) {
            var n = g_nodes[g_order[i]];
            if (n.tei === 1 || !pos[n.tei] || !pos[n.parent]) continue;
            var a = pos[n.parent], b = pos[n.tei];
            p.draw_line(a.x, a.y, b.x, b.y);
        }
        // Nodes
        for (var j = 0; j < g_order.length; j++) {
            var nd = g_nodes[g_order[j]];
            if (!pos[nd.tei]) continue;
            var px = pos[nd.tei].x, py = pos[nd.tei].y;
            nd._x = px; nd._y = py;
            var sel = (nd.tei === g_selected);
            p.set_pen(sel ? "#ffd166" : "#ffffff", sel ? 3 : 2);
            p.set_brush(nd.tei === 1 ? "#06d6a0" : "#118ab2");
            p.draw_ellipse(px - NODE_R, py - NODE_R, NODE_R * 2, NODE_R * 2);
            p.set_pen("#ffffff", 1);
            p.set_font("sans", 9, false);
            var label = "TEI" + nd.tei;
            p.draw_text(px - 16, py + 4, label);
            // level badge
            p.set_font("sans", 8, false);
            p.set_pen("#aaaaaa", 1);
            p.draw_text(px - 8, py + NODE_R + 10, "L" + nd.level);
        }
    }
    p.set_pen("#ffffff", 1);
    p.set_font("sans", 11, true);
    p.draw_text(10, 20, "Topology (" + g_order.length + " nodes)");

    // --- TEI-MAC mapping table (right) ---
    p.set_pen("#ffffff", 1);
    p.set_font("sans", 11, true);
    p.draw_text(map_x + 10, 20, "TEI -> MAC Mapping");
    p.set_font("sans", 9, false);
    var my = 40;
    p.set_pen("#888888", 1);
    p.draw_text(map_x + 10, my, "TEI");
    p.draw_text(map_x + 70, my, "Parent");
    p.draw_text(map_x + 130, my, "Frames");
    my += 16;
    p.set_pen("#444444", 1);
    p.draw_line(map_x + 10, my - 4, w - 10, my - 4);
    var sorted = g_order.slice().sort(function(a, b) { return a - b; });
    var shown = 0;
    for (var k = 0; k < sorted.length && my < topo_h - 10; k++) {
        var mn = g_nodes[sorted[k]];
        var sel2 = (mn.tei === g_selected);
        p.set_pen(sel2 ? "#ffd166" : "#cccccc", 1);
        p.draw_text(map_x + 10, my, String(mn.tei) + (mn.tei === 1 ? " [CCO]" : ""));
        p.draw_text(map_x + 70, my, mn.parent ? String(mn.parent) : "-");
        p.draw_text(map_x + 130, my, String(mn.count));
        my += 15;
        shown++;
    }
    if (sorted.length > shown) {
        p.set_pen("#888888", 1);
        p.draw_text(map_x + 10, my, "... +" + (sorted.length - shown) + " more");
    }

    // --- Frame records (bottom) ---
    p.set_pen("#ffffff", 1);
    p.set_font("sans", 11, true);
    p.draw_text(10, topo_h + 20, "Frame Records (last " + g_events.length + ")");
    p.set_font("sans", 9, false);
    var ey = topo_h + 40;
    // Show last ~8 events that fit
    var start = Math.max(0, g_events.length - 8);
    for (var e = start; e < g_events.length && ey < h - 8; e++) {
        var ev = g_events[e];
        p.set_pen("#cccccc", 1);
        p.draw_text(10, ey, "#" + ev.idx);
        p.draw_text(70, ey, "TEI " + ev.tei);
        p.draw_text(140, ey, "parent " + (ev.parent || "-"));
        p.set_pen("#88ccff", 1);
        p.draw_text(230, ey, ev.desc.substring(0, 60));
        ey += 15;
    }

    // Selection info
    if (g_selected !== -1 && g_nodes[g_selected]) {
        var s = g_nodes[g_selected];
        p.set_pen("#ffd166", 1);
        p.set_font("sans", 10, true);
        p.draw_text(10, h - 10, "Selected TEI " + s.tei +
            " | parent " + (s.parent || "-") +
            " | level " + s.level +
            " | " + s.count + " frames" +
            (s.tei === 1 ? " [CCO]" : " [STA]"));
    }
}

function on_event(type, x, y, button, modifiers, delta) {
    if (type !== EVT_PRESS) return false;
    for (var i = 0; i < g_order.length; i++) {
        var node = g_nodes[g_order[i]];
        if (node._x === undefined) continue;
        var dx = x - node._x, dy = y - node._y;
        if (dx * dx + dy * dy <= NODE_R * NODE_R) {
            g_selected = (g_selected === node.tei) ? -1 : node.tei;
            return true;
        }
    }
    if (g_selected !== -1) { g_selected = -1; return true; }
    return false;
}
