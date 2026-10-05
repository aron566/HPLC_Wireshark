// js_topo/topo.js — Topology graphics plugin, aligned with the native TOPO page.
// Consumes parser-generated topo events via frame.topoEvent (single parse
// entry) — the same source the native TopoWindow uses — and mirrors its layout:
//   top bar : NID selector | CCO MAC | node count | Live
//   left    : hierarchical node graph (same icons as native via draw_icon)
//   right   : TEI-MAC mapping table (TEI/MAC/Status/Level/Neighbors/Proxy)
//   bottom  : frame records (Frame/Time/Type/Description)
// Hover a node: tooltip with MAC / access mode / parent / up-down success rates.
// Click a node: select it. Click the NID box: switch network.
// Double-click a frame record: jump the main window to that frame.
// All UI strings are English.

var EVT_PRESS = 0;
var EVT_RELEASE = 1;
var EVT_MOVE  = 2;
var EVT_WHEEL = 3;
var EVT_LEAVE = 5;
var EVT_DBLCLICK = 6;

// ---- theme palette (follows host BPLC_THEME: dark/light) ----
var g_theme = "dark";
try {
    if (typeof host !== "undefined" && host.getEnv) {
        var _t = host.getEnv("BPLC_THEME");
        if (_t === "light") g_theme = "light";
    }
} catch (e) {}
var PALETTE = {
    // 深色:对齐主界面 QDarkStyleSheet 色系(theme.cpp dark_palette)
    dark: {
        bg: "#19232D", top: "#19232D", table_bg: "#19232D",
        border: "#455364", border2: "#37414F", border3: "#788D9C",
        text: "#DFE1E2", text_dim: "#9DA9B5", text_dim2: "#A9B7C0",
        text_bright: "#E6E9EB", text_muted: "#788D9C", text_head: "#9DA9B5",
        sel: "#346792", hover: "#2C5C84", nid_bg: "#37414F",
        nid_bg_open: "#455364",
        accent: "#1A72BB", accent2: "#259AE9", green: "#3BA55D",
        warn: "#D1B36A", warn_bg: "#4A3B20", warn_border: "#8A7A4A",
        warn_text: "#E6C384",
        line: "#455364", misc: "#788D9C", misc2: "#2C3B4A"
    },
    // 浅色:对齐主界面 light_palette
    light: {
        bg: "#f0f0f0", top: "#e4e4e4", table_bg: "#ffffff",
        border: "#c0c0c0", border2: "#d8d8d8", border3: "#909090",
        text: "#202020", text_dim: "#606060", text_dim2: "#505050",
        text_bright: "#303030", text_muted: "#909090", text_head: "#505050",
        sel: "#3d6f9f", hover: "#5a8ab5", nid_bg: "#e0e0e0",
        nid_bg_open: "#d0d0d0",
        accent: "#3d6f9f", accent2: "#2f7fbf", green: "#3a9a4a",
        warn: "#a08020", warn_bg: "#f0e8c0", warn_border: "#c0a040",
        warn_text: "#806010",
        line: "#c0c0c0", misc: "#909090", misc2: "#dde7f0"
    }
};
var C = PALETTE[g_theme];

// ---- state ---------------------------------------------------------------
var g_nets = {};        // nid -> net
var g_nid_order = [];   // first-seen NID order
var g_cur_nid = -1;
var g_topo_active = false;  // true once any frame.topoEvent arrived
var g_nid_open = false;     // NID dropdown open
var g_hover = null;     // {tei, mac} | null
var g_selected = null;  // {tei, mac} | null
var g_layout = [];      // node boxes from last render (hit test)
var g_nid_box = null;   // NID selector rect
var g_nid_items = [];   // dropdown item rects
var g_record_rows = []; // frame record row rects from last render (dblclick)
var g_map_rows = [];    // map table "Neighbors" cell rects + tei (dblclick → showTable)

// ---- view transform (wheel zoom + drag pan, only for the node graph) ----
var g_zoom = 1.0;       // user zoom factor (wheel)
var g_pan_x = 0;        // graph pan offset X (drag)
var g_pan_y = 0;        // graph pan offset Y (drag)
var g_drag = null;      // active drag {sx, sy, px, py, moved} or null
var g_graph_rect = null; // node-graph viewport rect {x,y,w,h} (drag/zoom region)
var g_rec_rect = null;   // frame-record viewport rect {x,y,w,h} (wheel scroll region)
var g_rec_scroll = 0;    // frame-record scroll offset (0=latest, >0 scroll up to older)

// ---- history mode (mirrors native TopoWindow show_history/show_live) ----
var g_event_log = [];   // {frameIndex, epochMs, evt} full topo events, frame order
var g_max_frame = 0;    // max frame.index seen (live frontier)
var g_hist = null;      // null=live; {frame, ms, model} when frozen
var g_live_btn = null;  // "Back to Live" button rect (top bar, history mode)
var EVT_LOG_CAP = 5000; // history rebuild window (event count cap)

// The live model lives in g_nets/g_nid_order/g_cur_nid/g_topo_active.
// History mode rebuilds a frozen snapshot by replaying g_event_log up to the
// target frame into a fresh model, then swaps it in for graph/map/NID/top-bar
// drawing; the frame-record table always reads the live model (native parity).
function save_model() {
    return { nets: g_nets, nid_order: g_nid_order,
             cur_nid: g_cur_nid, topo_active: g_topo_active };
}
function load_model(m) {
    g_nets = m.nets; g_nid_order = m.nid_order;
    g_cur_nid = m.cur_nid; g_topo_active = m.topo_active;
}
function fresh_model() {
    return { nets: {}, nid_order: [], cur_nid: -1, topo_active: false };
}
function rebuild_history(target) {
    var live = save_model();
    load_model(fresh_model());
    for (var i = 0; i < g_event_log.length; i++) {
        var e = g_event_log[i];
        if (e.frameIndex > target) break;
        apply_topo_event(e.evt, true);  // skip_log: avoid duplicating the log
    }
    var hist = save_model();
    // keep viewing the same NID when it exists in the frozen snapshot
    if (hist.nets[live.cur_nid]) hist.cur_nid = live.cur_nid;
    load_model(live);
    return hist;
}

// status: "online" | "joining" | "offline" | "unknown"
function new_net(nid) {
    return {
        nid: nid, cco_mac: "", cco_restart: -1,
        nodes: {}, order: [], pending: {},
        neighbors: {}, neighbor_counts: {}, events: [], sta_restart: {},
        first_ms: 0, first_frame: 0, frame_count: 0
    };
}

function ensure_net(nid) {
    nid = nid || 0;
    if (!g_nets[nid]) {
        g_nets[nid] = new_net(nid);
        g_nid_order.push(nid);
        if (g_cur_nid < 0) g_cur_nid = nid;
    }
    return g_nets[nid];
}

function cur_net() {
    if (g_cur_nid < 0) return null;
    return g_nets[g_cur_nid] || null;
}

function ensure_node(net, tei) {
    if (!net.nodes[tei]) {
        net.nodes[tei] = { tei: tei, mac: "", parent: 0xFFFF,
                           status: "unknown", level: -1, is_rf: false,
                           count: 0, last_frame: 0, down: -1, up: -1 };
        net.order.push(tei);
    }
    return net.nodes[tei];
}

function find_node_by_mac(net, mac) {
    if (!mac) return null;
    for (var i = 0; i < net.order.length; i++) {
        var nd = net.nodes[net.order[i]];
        if (nd.mac && nd.mac === mac) return nd;
    }
    return null;
}

function same_key(a, b) {
    if (!a || !b) return false;
    return a.tei === b.tei && a.mac === b.mac;
}

// ---- topo event handling (mirrors C++ TopoState::apply) -------------------
var KIND_LABEL = {
    discoverList: "Discover List", assocReq: "Assoc Req",
    assocCnf: "Assoc Cnf", assocGatherInd: "Assoc Gather Ind",
    assocInd: "Assoc Ind", changeProxyReq: "Proxy Change Req",
    changeProxyCnf: "Proxy Change Cnf", leaveInd: "Leave Ind",
    successRate: "Success Rate", ccoRestart: "CCO Restart",
    staRestart: "STA Restart", other: "Other"
};

function kind_label(k) { return KIND_LABEL[k] || k; }

function first_node_mac(evt) {
    if (evt.nodes && evt.nodes.length > 0) return evt.nodes[0].mac || "";
    return "";
}

function build_desc(evt) {
    var k = evt.kind;
    var i, n, r;
    if (k === "discoverList") {
        var nn = evt.nodes ? evt.nodes.length : 0;
        var nr = evt.routes ? evt.routes.length : 0;
        return "Discover list from STA-" + evt.discoverSrcTei +
               ": " + nn + " node(s), " + nr + " route(s)";
    }
    if (k === "assocReq")
        return "Assoc request from " + first_node_mac(evt) + " (joining)";
    if (k === "assocCnf" || k === "assocInd") {
        n = (evt.nodes && evt.nodes.length > 0) ? evt.nodes[0] : null;
        r = (evt.routes && evt.routes.length > 0) ? evt.routes[0].parent : -1;
        return (k === "assocCnf" ? "Assoc confirm: " : "Assoc indication: ") +
               "STA TEI=" + (n ? n.tei : "?") + " (" + (n ? n.mac : "?") +
               ") via proxy TEI=" + r;
    }
    if (k === "assocGatherInd")
        return "Assoc gather: " + (evt.nodes ? evt.nodes.length : 0) + " STA(s) online";
    if (k === "changeProxyReq")
        return "Proxy change request: STA TEI=" +
               ((evt.nodes && evt.nodes.length > 0) ? evt.nodes[0].tei : "?");
    if (k === "changeProxyCnf") {
        r = (evt.routes && evt.routes.length > 0) ? evt.routes[0] : null;
        return "Proxy change confirm: STA TEI=" + (r ? r.child : "?") +
               " -> parent TEI=" + (r ? r.parent : "?");
    }
    if (k === "leaveInd")
        return "Leave: " + (evt.leaves || []).join(", ") + " offline";
    if (k === "successRate") {
        var parts = [];
        for (i = 0; i < (evt.commRates || []).length; i++) {
            var c = evt.commRates[i];
            parts.push("TEI=" + c.tei + " down " + c.down + "%/up " + c.up + "%");
        }
        return "Success rate: " + parts.join("; ");
    }
    return evt.desc || k;
}

function push_event(net, kind, frameIndex, epochMs, desc, nid) {
    net.events.push({ frameIndex: frameIndex, epochMs: epochMs,
                      kind: kind, desc: desc, nid: nid });
    if (net.events.length > 500) net.events.shift();
    if (net.first_frame === 0 && frameIndex > 0) {
        net.first_frame = frameIndex;
        net.first_ms = epochMs;
    }
}

function apply_topo_event(evt, skip_log) {
    g_topo_active = true;
    if (!skip_log) {
        g_event_log.push({ frameIndex: evt.frameIndex,
                           epochMs: evt.epochMs, evt: evt });
        if (g_event_log.length > EVT_LOG_CAP) g_event_log.shift();
    }
    var net = ensure_net(evt.nid);
    var k = evt.kind;
    var i, n, r, nd;
    var up_route_changed = false, up_route_desc = "";

    // CCO node (TEI=1): root, always online
    if (evt.ccoMac) {
        net.cco_mac = evt.ccoMac;
        var cco = ensure_node(net, 1);
        cco.mac = evt.ccoMac;
        cco.parent = 0;
        cco.status = "online";
    }

    if (k === "discoverList") {
        for (i = 0; i < (evt.nodes || []).length; i++) {
            n = evt.nodes[i];
            nd = ensure_node(net, n.tei);
            if (n.mac) nd.mac = n.mac;
            nd.status = "online";
            if (evt.isRf) nd.is_rf = true;
            delete net.pending[n.mac];
        }
        for (i = 0; i < (evt.routes || []).length; i++) {
            r = evt.routes[i];
            nd = ensure_node(net, r.child);
            nd.parent = r.parent;
            if (nd.status === "unknown") nd.status = "online";
        }
        // upRoutes: parser already kept only RouteType=3 (proxy main path)
        // 检测下一跳变化(与主界面 up_route_changed 一致):变化才入路由变更表
        var up_changes = [];
        for (i = 0; i < (evt.upRoutes || []).length; i++) {
            r = evt.upRoutes[i];
            nd = ensure_node(net, r.sta);
            if (nd.parent !== r.nextHop) {
                up_route_changed = true;
                up_changes.push("TEI=" + r.sta + " 下一跳" +
                    (nd.parent === 0xFFFF ? "-" : String(nd.parent)) +
                    "->" + r.nextHop);
            }
            nd.parent = r.nextHop;
            if (nd.status === "unknown") nd.status = "online";
        }
        if (up_route_changed)
            up_route_desc = "发现列表上行路由变化: " + up_changes.join("; ");
        if (evt.discoverSrcTei && evt.neighborTeis) {
            net.neighbors[evt.discoverSrcTei] = evt.neighborTeis.slice();
            // 累计各邻居出现次数(与主界面 neighbor_counts 一致,供排序/信号强度)
            var cnts = net.neighbor_counts[evt.discoverSrcTei];
            if (!cnts) cnts = net.neighbor_counts[evt.discoverSrcTei] = {};
            for (var di = 0; di < evt.neighborTeis.length; di++) {
                var nb = evt.neighborTeis[di];
                cnts[nb] = (cnts[nb] || 0) + 1;
            }
        }
        // CCO restart detection (mirrors TopoState)
        if (evt.restartCount >= 0) {
            if (net.cco_restart >= 0 && net.cco_restart !== evt.restartCount) {
                push_event(net, "ccoRestart", evt.frameIndex, evt.epochMs,
                    "WARNING: CCO rebooted! restart count " +
                    net.cco_restart + " -> " + evt.restartCount, evt.nid);
            }
            net.cco_restart = evt.restartCount;
        }
    } else if (k === "assocReq") {
        var mac = first_node_mac(evt);
        if (mac) {
            net.pending[mac] = { mac: mac, count: (net.pending[mac] ?
                net.pending[mac].count + 1 : 1) };
            // STA restart detection (mirrors TopoState)
            if (evt.restartCount >= 0) {
                var prev = net.sta_restart[mac];
                if (prev !== undefined && prev !== evt.restartCount) {
                    push_event(net, "staRestart", evt.frameIndex, evt.epochMs,
                        "WARNING: STA rebooted! STA " + mac + " restart count " +
                        prev + " -> " + evt.restartCount, evt.nid);
                }
                net.sta_restart[mac] = evt.restartCount;
            }
        }
    } else if (k === "assocCnf" || k === "assocInd" || k === "assocGatherInd") {
        for (i = 0; i < (evt.nodes || []).length; i++) {
            n = evt.nodes[i];
            nd = ensure_node(net, n.tei);
            if (n.mac) nd.mac = n.mac;
            nd.status = "online";
            if (evt.isRf) nd.is_rf = true;
            delete net.pending[n.mac];
        }
        for (i = 0; i < (evt.routes || []).length; i++) {
            r = evt.routes[i];
            nd = ensure_node(net, r.child);
            nd.parent = r.parent;
        }
    } else if (k === "changeProxyCnf") {
        for (i = 0; i < (evt.routes || []).length; i++) {
            r = evt.routes[i];
            nd = ensure_node(net, r.child);
            nd.parent = r.parent;
            if (nd.status === "unknown") nd.status = "online";
        }
    } else if (k === "leaveInd") {
        for (i = 0; i < (evt.leaves || []).length; i++) {
            nd = find_node_by_mac(net, evt.leaves[i]);
            if (nd) nd.status = "offline";
        }
    } else if (k === "successRate") {
        for (i = 0; i < (evt.commRates || []).length; i++) {
            var c = evt.commRates[i];
            // 无条件覆盖(与主界面 comm_rates[tei]=cr 一致):节点不存在也先建立
            nd = net.nodes[c.tei] || ensure_node(net, c.tei);
            nd.down = c.down; nd.up = c.up;
        }
    }
    // 对齐主界面:仅真正"变更"入路由变更表
    // (assoc/changeProxy/leave 直接入;发现列表仅上行路由变化;成功率/重启不入)
    var RECORD_KINDS = { assocReq:1, assocCnf:1, assocGatherInd:1, assocInd:1,
                         changeProxyReq:1, changeProxyCnf:1, leaveInd:1 };
    if (RECORD_KINDS[k]) {
        push_event(net, k, evt.frameIndex, evt.epochMs, build_desc(evt), evt.nid);
    } else if (k === "discoverList" && up_route_changed) {
        push_event(net, k, evt.frameIndex, evt.epochMs,
                   up_route_desc || build_desc(evt), evt.nid);
    }
    return true;
}

// ---- frame entry (single entry): decoded frame from the main parser,
// topo events arrive as frame.topoEvent (null when the frame carries none)
function parse(frame) {
    if (frame.index > g_max_frame) g_max_frame = frame.index;
    var evt = frame.topoEvent;
    if (evt) apply_topo_event(evt, false);
    var mpdu = frame.mpdu || {};
    var src = mpdu.srcTei || 0;
    if (src <= 0) return { accept: true, summary: "Topo" };
    if (!g_topo_active) {
        var net = ensure_net(0);
        var nd = ensure_node(net, src);
        if (nd.status === "unknown") nd.status = "online";
        nd.count++;
        if (src !== 1 && nd.parent === 0xFFFF) nd.parent = 1;
        ensure_node(net, 1).status = "online";
        return { accept: true, summary: "Topo: TEI " + src };
    }
    return { accept: true, summary: "Topo" };
}

// ---- host notification: main packet-list selection changed ----
// Called by the host as on_frame_selected(frameIndex, forceHistory).
// force=true (frame double-clicked): freeze at idx even if it is the newest
// frame, mirroring native enter_topo_history; otherwise clicking the newest
// frame returns to live (native update_topo_history: latest frame = live).
function on_frame_selected(idx, force) {
    if (!force && idx >= g_max_frame) {
        if (g_hist) { g_hist = null; return true; }
        return false;
    }
    var ms = 0;
    for (var i = g_event_log.length - 1; i >= 0; i--) {
        if (g_event_log[i].frameIndex <= idx) {
            ms = g_event_log[i].epochMs;
            break;
        }
    }
    g_hist = { frame: idx, ms: ms, model: rebuild_history(idx) };
    g_nid_open = false; g_selected = null; g_hover = null;
    return true;
}

function get_info() {
    return {
        protocolId: "JSTOPO_2024",
        displayName: "JS Topology View",
        displayNameEn: "JS Topology View"
    };
}

// ---- levels (BFS from CCO, mirrors TopoState::compute_levels) -------------
function compute_levels(net) {
    var levels = { 1: 0 };
    var queue = [1];
    while (queue.length > 0) {
        var cur = queue.shift();
        for (var i = 0; i < net.order.length; i++) {
            var tei = net.order[i];
            if (levels[tei] !== undefined) continue;
            var nd = net.nodes[tei];
            if (nd.parent === cur) { levels[tei] = levels[cur] + 1; queue.push(tei); }
        }
    }
    return levels;
}

// ---- formatting ------------------------------------------------------------
function fmt_nid(nid) { return "0x" + (nid >>> 0).toString(16).toUpperCase(); }

function fmt_time(ms) {
    if (!ms) return "--";
    var d = new Date(ms);
    function p2(v) { return (v < 10 ? "0" : "") + v; }
    function p3(v) { return (v < 10 ? "00" : (v < 100 ? "0" : "")) + v; }
    // 对齐主界面 format_time: yyyy-MM-dd HH:mm:ss.zzz
    return d.getFullYear() + "-" + p2(d.getMonth() + 1) + "-" +
           p2(d.getDate()) + " " + p2(d.getHours()) + ":" +
           p2(d.getMinutes()) + ":" + p2(d.getSeconds()) + "." +
           p3(d.getMilliseconds());
}

function status_label(s) {
    return s === "online" ? "Online" : s === "joining" ? "Joining" :
           s === "offline" ? "Offline" : "-";
}

function node_icon_for(nd, is_pending) {
    if (is_pending) return "meter_joining";
    if (nd.status === "offline") return "meter_offline";
    return "meter_online";
}

function node_title(key) {
    if (key.tei === 1) return "CCO";
    if (key.tei === 0) return "Joining";
    return "STA-" + key.tei;
}

// ---- render ----------------------------------------------------------------
var TOP_H = 30;         // top bar height
var NODE_W = 140, ICON_H = 64, LABEL_H = 54;
var HGAP = 28, VGAP = 140, MARGIN = 40;

function render(p, w, h) {
    // 动态跟随主界面主题:切换后下一次渲染即生效(周期重绘约 800ms 内)
    var t = "dark";
    try {
        if (typeof host !== "undefined" && host.getEnv) {
            var _tv = host.getEnv("BPLC_THEME");
            if (_tv === "light") t = "light";
        }
    } catch (e) {}
    if (t !== g_theme) { g_theme = t; C = PALETTE[g_theme]; }
    p.clear(C.bg);
    // History mode: graph / map table / NID selector / top bar read the frozen
    // snapshot; the frame-record table below always reads the live model and
    // highlights the frozen row (native parity).
    var live = save_model();
    if (g_hist) load_model(g_hist.model);
    var net = cur_net();
    g_record_rows = [];  // rebuilt by draw_records each frame
    g_map_rows = [];     // rebuilt by draw_map_table each frame
    g_live_btn = null;   // rebuilt by draw_top_bar each frame
    draw_top_bar(p, w, net);
    var bot_h = Math.max(96, Math.floor(h * 0.26));
    if (!net || (net.order.length === 0 &&
                 Object.keys(net.pending).length === 0)) {
        p.set_font("", 10, false);
        p.set_pen(C.text_dim, 1);
        p.draw_text(20, TOP_H + 40, "No topology data yet");
    } else {
        var rw = Math.min(360, Math.floor(w * 0.38));
        var gx = 0, gy = TOP_H, gw = w - rw, gh = h - TOP_H - bot_h;
        g_graph_rect = { x: gx, y: gy, w: gw, h: gh };
        draw_graph(p, net, gx, gy, gw, gh);
        draw_map_table(p, net, w - rw, TOP_H, rw, h - TOP_H - bot_h);
    }
    if (g_nid_open) draw_nid_dropdown(p);
    draw_tooltip(p, net, w, h);
    load_model(live);
    g_rec_rect = { x: 0, y: h - bot_h, w: w, h: bot_h };
    draw_records(p, cur_net(), 0, h - bot_h, w, bot_h,
                 g_hist ? g_hist.frame : -1);
}

function draw_top_bar(p, w, net) {
    p.fill_rect(0, 0, w, TOP_H, C.top);
    p.set_pen(C.border, 1);
    p.draw_line(0, TOP_H - 1, w, TOP_H - 1);
    p.set_font("", 10, false);
    var x = 10;
    p.set_pen(C.text, 1);
    p.draw_text(x, 20, "NID:");
    x += 36;
    var bw = 110;
    g_nid_box = { x: x, y: 5, w: bw, h: TOP_H - 10 };
    p.fill_rect(x, 5, bw, TOP_H - 10, g_nid_open ? C.nid_bg_open : C.nid_bg);
    p.set_pen(C.border3, 1);
    p.draw_rect(x, 5, bw, TOP_H - 10);
    p.set_pen(C.text, 1);
    p.draw_text(x + 8, 20, (net ? fmt_nid(net.nid) : "--") + "  v");
    x += bw + 16;
    if (net && net.cco_mac) {
        p.set_pen(C.text_dim, 1);
        p.draw_text(x, 20, "CCO " + net.cco_mac);
        x += 170;
    }
    var nn = net ? net.order.length : 0;
    p.set_pen(C.text_dim, 1);
    p.draw_text(x, 20, nn + " node(s)");
    if (g_hist) {
        // History mode: frozen position label + Back to Live button
        var label = "History @ #" + g_hist.frame;
        if (g_hist.ms > 0) label += " " + fmt_time(g_hist.ms);
        p.set_pen(C.warn, 1);
        p.draw_text(w - 300, 20, label);
        var bw2 = 110;
        g_live_btn = { x: w - bw2 - 10, y: 5, w: bw2, h: TOP_H - 10 };
        p.fill_rect(g_live_btn.x, g_live_btn.y, g_live_btn.w, g_live_btn.h,
                    C.warn_bg);
        p.set_pen(C.warn_border, 1);
        p.draw_rect(g_live_btn.x, g_live_btn.y, g_live_btn.w, g_live_btn.h);
        p.set_pen(C.warn_text, 1);
        p.draw_text(g_live_btn.x + 12, 20, "Back to Live");
    } else {
        p.set_pen(C.accent2, 1);
        p.draw_text(w - 50, 20, "Live");
    }
}

function draw_nid_dropdown(p) {
    if (!g_nid_box) return;
    g_nid_items = [];
    p.set_font("", 10, false);
    for (var i = 0; i < g_nid_order.length; i++) {
        var nid = g_nid_order[i];
        var r = { x: g_nid_box.x, y: g_nid_box.y + g_nid_box.h + i * 24,
                  w: g_nid_box.w, h: 24, nid: nid };
        g_nid_items.push(r);
        p.fill_rect(r.x, r.y, r.w, r.h,
                    nid === g_cur_nid ? C.sel : C.nid_bg);
        p.set_pen(C.border3, 1);
        p.draw_rect(r.x, r.y, r.w, r.h);
        p.set_pen(C.text, 1);
        p.draw_text(r.x + 8, r.y + 16, fmt_nid(nid));
    }
}

// layout nodes per level (mirrors native TopoGraphWidget::relayout), fit area
function layout_nodes(net, gx, gy, gw, gh) {
    var levels = compute_levels(net);
    var by_level = {};
    var max_level = 0, i, tei, k;
    for (i = 0; i < net.order.length; i++) {
        tei = net.order[i];
        if (levels[tei] === undefined) continue;
        var lv = levels[tei];
        if (!by_level[lv]) by_level[lv] = [];
        by_level[lv].push(tei);
        if (lv > max_level) max_level = lv;
    }
    var pend_macs = Object.keys(net.pending);
    if (pend_macs.length > 0 && max_level < 1) max_level = 1;
    var max_tier = 1;
    for (k in by_level)
        if (by_level[k].length > max_tier) max_tier = by_level[k].length;
    if (max_level >= 1) max_tier += pend_macs.length;

    var node_h = ICON_H + LABEL_H;
    var canvas_w = Math.max(400, max_tier * (NODE_W + HGAP) + 2 * MARGIN);
    var canvas_h = MARGIN * 2 + (max_level + 1) * VGAP;
    var scale = Math.min(gw / canvas_w, gh / canvas_h, 1.5) * g_zoom;
    if (!(scale > 0)) scale = 1;
    var ox = gx + (gw - canvas_w * scale) / 2 + g_pan_x;
    var oy = gy + (gh - canvas_h * scale) / 2 + g_pan_y;

    var items = [];
    var centers = {};  // tei -> item (for edges)
    for (var lv2 = 0; lv2 <= max_level; lv2++) {
        var tier = by_level[lv2] || [];
        var extra = (lv2 === 1) ? pend_macs.length : 0;
        var total = tier.length + extra;
        if (total === 0) continue;
        var total_w = total * (NODE_W + HGAP) - HGAP;
        var x0 = (canvas_w - total_w) / 2;
        var idx = 0;
        for (i = 0; i < tier.length; i++) {
            tei = tier[i];
            var x = ox + (x0 + idx * (NODE_W + HGAP)) * scale;
            var y = oy + (MARGIN + lv2 * VGAP) * scale;
            var it = { tei: tei, mac: net.nodes[tei].mac,
                       x: x, y: y, w: NODE_W * scale, h: node_h * scale,
                       icon: ICON_H * scale, cx: x + NODE_W * scale / 2,
                       cy: y + node_h * scale / 2 };
            items.push(it);
            centers[tei] = it;
            idx++;
        }
        for (i = 0; i < extra; i++) {
            var mac = pend_macs[i];
            var px = ox + (x0 + idx * (NODE_W + HGAP)) * scale;
            var py = oy + (MARGIN + lv2 * VGAP) * scale;
            items.push({ tei: 0, mac: mac, x: px, y: py,
                         w: NODE_W * scale, h: node_h * scale,
                         icon: ICON_H * scale,
                         cx: px + NODE_W * scale / 2,
                         cy: py + node_h * scale / 2 });
            idx++;
        }
    }
    // unreachable nodes: bottom row
    var unreached = [];
    for (i = 0; i < net.order.length; i++) {
        tei = net.order[i];
        if (levels[tei] === undefined) unreached.push(tei);
    }
    if (unreached.length > 0) {
        var uw = unreached.length * (NODE_W + HGAP) - HGAP;
        var ux0 = ox + (canvas_w - uw) * scale / 2;
        var uy = oy + (canvas_h - MARGIN - node_h) * scale;
        for (i = 0; i < unreached.length; i++) {
            tei = unreached[i];
            var ux = ux0 + i * (NODE_W + HGAP) * scale;
            var uitem = { tei: tei, mac: net.nodes[tei].mac,
                          x: ux, y: uy, w: NODE_W * scale, h: node_h * scale,
                          icon: ICON_H * scale,
                          cx: ux + NODE_W * scale / 2,
                          cy: uy + node_h * scale / 2 };
            items.push(uitem);
            centers[tei] = uitem;
        }
    }
    return { items: items, centers: centers, scale: scale };
}

function draw_graph(p, net, gx, gy, gw, gh) {
    p.set_clip(gx, gy, gw, gh);  // 裁剪到图区,防止缩放/平移后画到映射表
    var lay = layout_nodes(net, gx, gy, gw, gh);
    g_layout = lay.items;
    var i, it;

    // edges: parent -> child (pending -> CCO)
    p.set_pen(C.line, 1);
    for (i = 0; i < lay.items.length; i++) {
        it = lay.items[i];
        var pc = null;
        if (it.tei === 0) {
            pc = lay.centers[1];
        } else {
            var nd0 = net.nodes[it.tei];
            var par = nd0 ? nd0.parent : 0xFFFF;
            if (par === 0xFFFF || par === it.tei) continue;
            pc = lay.centers[par];
        }
        if (!pc) continue;
        p.draw_line(pc.cx, pc.cy + it.h / 2, it.cx, it.y);
    }

    // nodes
    for (i = 0; i < lay.items.length; i++) {
        it = lay.items[i];
        var is_pending = (it.tei === 0);
        var nd = is_pending ? null : net.nodes[it.tei];
        var key = { tei: it.tei, mac: it.mac };
        var icon = (it.tei === 1) ? "cco" : node_icon_for(nd, is_pending);
        var ix = it.x + (it.w - it.icon) / 2;
        p.draw_icon(icon, ix, it.y, it.icon, it.icon);

        // selection / hover outline
        if (same_key(g_selected, key)) {
            p.set_pen(C.warn, 2);
            p.draw_rect(it.x - 3, it.y - 3, it.w + 6, it.h + 6);
        } else if (same_key(g_hover, key)) {
            p.set_pen(C.text_dim, 1);
            p.draw_rect(it.x - 2, it.y - 2, it.w + 4, it.h + 4);
        }

        // labels: title / MAC / access
        var fs = Math.max(7, Math.floor(10 * lay.scale));
        var row_h = (it.h - it.icon) / 3;
        var ly = it.y + it.icon;
        var title = node_title(key);
        var dim = (!is_pending && nd && nd.status === "offline");
        p.set_font("", fs, true);
        p.set_pen(it.tei === 1 ? C.accent : dim ? C.text_dim : C.text, 1);
        center_text(p, title, it.x, ly + row_h * 0.75, it.w, fs);
        p.set_font("", Math.max(6, fs - 1), false);
        p.set_pen(dim ? C.text_muted : C.text_dim2, 1);
        center_text(p, it.mac ? it.mac : "MAC ?", it.x, ly + row_h * 1.75,
                    it.w, fs - 1);
        var acc = (!is_pending && nd && nd.is_rf) ? "RF" : "PLC";
        center_text(p, acc, it.x, ly + row_h * 2.75, it.w, fs - 1);
    }
    p.reset_clip();
}

function center_text(p, s, x, y, w, fs) {
    var tw = s.length * fs * 0.58;
    p.draw_text(x + (w - tw) / 2, y, s);
}

function draw_map_table(p, net, x, y, w, h) {
    p.fill_rect(x, y, w, h, C.table_bg);
    p.set_pen(C.border, 1);
    p.draw_line(x, y, x, y + h);
    var cols = [
        { t: "TEI", w: 0.10 }, { t: "MAC", w: 0.28 },
        { t: "Status", w: 0.14 }, { t: "Level", w: 0.10 },
        { t: "Neighbors", w: 0.24 }, { t: "Proxy", w: 0.14 }
    ];
    var rh = 20;
    p.set_font("", 9, true);
    p.set_pen(C.text_head, 1);
    var cx = x;
    var i, j;
    for (i = 0; i < cols.length; i++) {
        p.draw_text(cx + 4, y + 15, cols[i].t);
        cx += w * cols[i].w;
    }
    p.draw_line(x, y + rh, x + w, y + rh);
    p.set_font("", 9, false);

    var teis = net.order.slice().sort(function(a, b) { return a - b; });
    var levels = compute_levels(net);
    var ry = y + rh;
    for (i = 0; i < teis.length; i++) {
        if (ry + rh > y + h) break;
        var tei = teis[i];
        var nd = net.nodes[tei];
        var key = { tei: tei, mac: nd.mac };
        if (same_key(g_hover, key) || same_key(g_selected, key))
            p.fill_rect(x + 1, ry + 1, w - 2, rh - 1, C.hover);
        p.set_pen(nd.status === "offline" ? C.text_muted : C.text, 1);
        cx = x;
        var nbs = net.neighbors[tei] || [];
        var vals = [
            tei === 1 ? "CCO(1)" : String(tei),
            nd.mac ? nd.mac : "-",
            status_label(nd.status),
            levels[tei] !== undefined ? String(levels[tei]) : "-",
            nbs.length > 0 ? nbs.join(",") : "-",
            (nd.parent === 0xFFFF || nd.parent === 0) ? "-" : String(nd.parent)
        ];
        for (j = 0; j < cols.length; j++) {
            // Neighbors 列用绿色(与原版邻居表配色呼应)
            if (j === 4 && nbs.length > 0)
                p.set_pen(C.green, 1);
            else
                p.set_pen(nd.status === "offline" ? C.text_muted : C.text, 1);
            p.draw_text(cx + 4, ry + 15, vals[j]);
            cx += w * cols[j].w;
        }
        p.set_pen(C.border2, 1);
        p.draw_line(x, ry + rh, x + w, ry + rh);
        // 记录邻居列单元格 rect(双击 → host.showTable 弹独立邻居表)
        if (nbs.length > 0)
            g_map_rows.push({ x: x + w * 0.62, y: ry, w: w * 0.24, h: rh,
                              tei: tei });
        ry += rh;
    }
    // joining (assocReq, TEI not yet assigned): list by MAC like the native table
    // (TEI "-", Status "Joining", Level 1, Proxy 1)
    var pend_macs = Object.keys(net.pending);
    for (i = 0; i < pend_macs.length; i++) {
        if (ry + rh > y + h) break;
        var pmac = pend_macs[i];
        var pvals = ["-", pmac ? pmac : "-", "Joining", "1", "-", "1"];
        cx = x;
        p.set_pen(C.text, 1);
        for (j = 0; j < cols.length; j++) {
            p.draw_text(cx + 4, ry + 15, pvals[j]);
            cx += w * cols[j].w;
        }
        p.set_pen(C.border2, 1);
        p.draw_line(x, ry + rh, x + w, ry + rh);
        ry += rh;
    }
}

function draw_records(p, net, x, y, w, h, hist_frame) {
    p.fill_rect(x, y, w, h, C.bg);
    p.set_pen(C.border, 1);
    p.draw_line(x, y, x + w, y);
    var cols = [
        { t: "Seq", w: 50 }, { t: "Time", w: 160 },
        { t: "Type", w: 110 }, { t: "NID", w: 70 },
        { t: "Description", w: 0 }
    ];
    var rh = 20;
    p.set_font("", 9, true);
    p.set_pen(C.text_head, 1);
    var cx = x + 6;
    var i;
    for (i = 0; i < cols.length; i++) {
        p.draw_text(cx, y + 15, cols[i].t);
        cx += cols[i].w;
    }
    p.draw_line(x, y + rh, x + w, y + rh);
    if (!net) return;
    p.set_font("", 9, false);
    var ry = y + rh;
    var evs = net.events;
    var visible = Math.max(1, Math.floor((h - rh) / rh));
    var max_scroll = Math.max(0, evs.length - visible);
    if (g_rec_scroll > max_scroll) g_rec_scroll = max_scroll;
    var start = Math.max(0, evs.length - visible - g_rec_scroll);
    for (i = start; i < evs.length; i++) {
        if (ry + rh > y + h) break;
        var e = evs[i];
        var warn = (e.kind === "ccoRestart" || e.kind === "staRestart");
        var frozen = (hist_frame > 0 && e.frameIndex === hist_frame);
        if (frozen) p.fill_rect(x, ry, w, rh, C.misc2);  // frozen row highlight
        p.set_pen(warn ? C.warn : (frozen ? C.warn_text : C.text_bright), 1);
        p.draw_text(x + 6, ry + 15, e.frameIndex > 0 ? String(e.frameIndex) : "-");
        p.draw_text(x + 56, ry + 15, fmt_time(e.epochMs));
        p.draw_text(x + 216, ry + 15, kind_label(e.kind));
        p.set_pen(warn ? C.warn : C.misc, 1);
        p.draw_text(x + 326, ry + 15,
                    e.nid !== undefined ? fmt_nid(e.nid) : "-");
        p.set_pen(warn ? C.warn : C.text_dim, 1);
        p.draw_text(x + 396, ry + 15, e.desc);
        if (e.frameIndex > 0)
            g_record_rows.push({ x: x, y: ry, w: w, h: rh,
                                 frameIndex: e.frameIndex });
        ry += rh;
    }
    // 滚动条指示(有可滚动内容时):右侧竖条 + 滑块
    if (max_scroll > 0) {
        var tr_x = x + w - 7, tr_y = y + rh, tr_h = Math.max(8, h - rh);
        p.fill_rect(tr_x, tr_y, 5, tr_h, C.border2);
        var thumb_h = Math.max(16, tr_h * visible / evs.length);
        var thumb_y = tr_y + (tr_h - thumb_h) * g_rec_scroll / max_scroll;
        p.fill_rect(tr_x, thumb_y, 5, thumb_h, C.border3);
    }
}

function tooltip_lines(net, key) {
    var lines = [];
    var title, nd = null;
    if (key.tei === 0) {
        title = "Joining (assoc request)";
        lines.push(key.mac || "MAC ?");
    } else {
        nd = net.nodes[key.tei];
        if (!nd) return null;
        title = (key.tei === 1) ? "CCO" : "STA-" + key.tei;
        lines.push(nd.mac ? nd.mac : "MAC ?");
    }
    lines.push("Access: " + ((nd && nd.is_rf) ? "RF" : "PLC"));
    if (key.tei !== 1 && nd) {
        var par = nd.parent;
        var pname = (par === 0xFFFF || par === 0) ? "-" :
                    (par === 1 ? "CCO" : "STA-" + par);
        lines.push("Parent: " + pname);
        if (nd.down >= 0)
            lines.push("Down: " + nd.down + "%  Up: " + nd.up + "%");
        else
            lines.push("No rate report");
        var nbs = net.neighbors[key.tei];
        if (nbs && nbs.length > 0)
            lines.push("Neighbors: " + nbs.join(", "));
    }
    return { title: title, lines: lines };
}

function draw_tooltip(p, net, w, h) {
    if (!g_hover || g_nid_open) return;
    var tip = tooltip_lines(net, g_hover);
    if (!tip) return;
    var ax = 0, ay = 0, found = false, i;
    for (i = 0; i < g_layout.length; i++) {
        var it = g_layout[i];
        if (it.tei === g_hover.tei && it.mac === g_hover.mac) {
            ax = it.x + it.w + 8; ay = it.y; found = true; break;
        }
    }
    if (!found) return;
    p.set_font("", 9, false);
    // 用真实文本宽度撑开提示框(字符数估算对比例字体不准,如 "Down: 98% Up: 95%"
    // 比同字符数的 MAC 行宽 14px)
    var maxw = p.text_width(tip.title);
    for (i = 0; i < tip.lines.length; i++) {
        var lw = p.text_width(tip.lines[i]);
        if (lw > maxw) maxw = lw;
    }
    var tw = maxw + 20, th = (tip.lines.length + 1) * 16 + 12;
    if (ax + tw > w) ax = w - tw - 4;
    if (ay + th > h) ay = h - th - 4;
    if (ax < 0) ax = 4;
    if (ay < 0) ay = 4;
    p.fill_rect(ax, ay, tw, th, C.top);
    p.set_pen(C.border3, 1);
    p.draw_rect(ax, ay, tw, th);
    p.set_font("", 9, true);
    p.set_pen(C.text, 1);
    p.draw_text(ax + 8, ay + 17, tip.title);
    p.set_font("", 9, false);
    p.set_pen(C.text_bright, 1);
    for (i = 0; i < tip.lines.length; i++)
        p.draw_text(ax + 8, ay + 17 + (i + 1) * 16, tip.lines[i]);
}

// ---- interaction -----------------------------------------------------------
function hit_node(x, y) {
    for (var i = g_layout.length - 1; i >= 0; i--) {
        var it = g_layout[i];
        if (x >= it.x && x <= it.x + it.w && y >= it.y && y <= it.y + it.h)
            return { tei: it.tei, mac: it.mac };
    }
    return null;
}

function in_rect(x, y, r) {
    return r && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// 双击邻居表单元格 → 弹出独立表格窗口(对齐原版 on_teimac_double_clicked)
function show_neighbor_table(tei) {
    var net = cur_net();
    if (!net) return;
    var nbs = (net.neighbors[tei] || []).slice();
    if (nbs.length === 0) return;
    var counts = net.neighbor_counts[tei] || {};
    // 按发现列表出现次数降序(与主表一致)
    nbs.sort(function(a, b) { return (counts[b] || 0) - (counts[a] || 0); });
    var levels = compute_levels(net);
    var cols = ["TEI", "MAC", "Level", "Discover count"];
    var rows = [];
    for (var i = 0; i < nbs.length; i++) {
        var nb = nbs[i];
        var node = net.nodes[nb] || {};
        rows.push([
            String(nb),
            node.mac ? node.mac : "-",
            levels[nb] !== undefined ? String(levels[nb]) : "-",
            String(counts[nb] || 0)
        ]);
    }
    if (typeof host !== "undefined" && host.showTable)
        host.showTable("Neighbors of STA TEI=" + tei, cols, rows);
}

function on_event(type, x, y, button, modifiers, delta_y) {
    var i, r;
    if (type === EVT_LEAVE) {
        if (g_hover) { g_hover = null; return true; }
        return false;
    }
    if (type === EVT_WHEEL) {
        // 帧记录区域:滚轮滚动(向上=看更早记录)
        if (g_rec_rect && in_rect(x, y, g_rec_rect)) {
            g_rec_scroll += (delta_y > 0) ? 2 : -2;
            if (g_rec_scroll < 0) g_rec_scroll = 0;
            return true;
        }
        // 图区域:滚轮缩放,以鼠标位置为中心
        var f = (delta_y > 0) ? 1.15 : (1 / 1.15);
        var nz = g_zoom * f;
        if (nz < 0.3) nz = 0.3;
        if (nz > 4.0) nz = 4.0;
        f = nz / g_zoom;
        g_pan_x = x - (x - g_pan_x) * f;
        g_pan_y = y - (y - g_pan_y) * f;
        g_zoom = nz;
        return true;
    }
    if (type === EVT_MOVE) {
        if (g_drag) {
            var dx = x - g_drag.sx, dy = y - g_drag.sy;
            if (!g_drag.moved && (dx * dx + dy * dy) > 9) g_drag.moved = true;
            g_pan_x = g_drag.px + dx;
            g_pan_y = g_drag.py + dy;
            return true;
        }
        var h = hit_node(x, y);
        if (!same_key(h, g_hover)) { g_hover = h; return true; }
        return false;
    }
    if (type === EVT_RELEASE) {
        if (g_drag) { g_drag = null; return true; }
        return false;
    }
    if (type === EVT_DBLCLICK) {
        // Frame record double-click: jump the main window to that frame
        for (i = 0; i < g_record_rows.length; i++) {
            r = g_record_rows[i];
            if (in_rect(x, y, r)) {
                if (typeof host !== "undefined" && host.jumpToFrame)
                    host.jumpToFrame(r.frameIndex);
                return false;  // no redraw needed
            }
        }
        // Neighbors cell double-click: pop up an independent table window
        for (i = 0; i < g_map_rows.length; i++) {
            r = g_map_rows[i];
            if (in_rect(x, y, r)) {
                show_neighbor_table(r.tei);
                return false;
            }
        }
        return false;
    }
    if (type === EVT_PRESS) {
        // Back to Live button (history mode)
        if (g_live_btn && in_rect(x, y, g_live_btn)) {
            g_hist = null;
            g_live_btn = null;
            return true;
        }
        // NID dropdown items first
        if (g_nid_open) {
            for (i = 0; i < g_nid_items.length; i++) {
                r = g_nid_items[i];
                if (in_rect(x, y, r)) {
                    g_cur_nid = r.nid;
                    g_nid_open = false;
                    g_selected = null;
                    g_hover = null;
                    return true;
                }
            }
        }
        if (in_rect(x, y, g_nid_box)) {
            g_nid_open = !g_nid_open;
            return true;
        }
        g_nid_open = false;
        var n = hit_node(x, y);
        if (n) {
            if (!same_key(n, g_selected)) { g_selected = n; return true; }
            return false;
        }
        // 图区域空白处按下:开始拖拽平移
        if (g_graph_rect && in_rect(x, y, g_graph_rect)) {
            g_drag = { sx: x, sy: y, px: g_pan_x, py: g_pan_y, moved: false };
        }
        return false;
    }
    return false;
}
