// js_replay/replay.js — Data replay recorder plugin
// Plugin API v1: get_info() + parse(frame)
// Records every accepted frame; get_replay_data() returns cached summaries.

var g_seq = 0;
var g_cache = [];  // [{seq, len, arrivalUs, summary}]

function get_info() {
    return {
        protocolId: "JSREPLAY_2024",
        displayName: "JS Replay Recorder"
    };
}

function parse(frame) {
    var bytes = frame.data || [];
    g_seq += 1;
    var entry = {
        seq: g_seq,
        len: bytes.length,
        arrivalUs: frame.arrivalUs || 0,
        summary: "Frame #" + g_seq + ": " + bytes.length + " bytes"
    };
    g_cache.push(entry);
    if (g_cache.length > 10000) g_cache.shift();  // bound the cache

    return {
        accept: true,
        summary: entry.summary,
        fields: [
            { name: "Seq", value: String(g_seq) },
            {
                name: "Length",
                value: bytes.length + " bytes",
                relStart: 0,
                relLen: bytes.length
            },
            { name: "ArrivalUs", value: String(entry.arrivalUs) }
        ]
    };
}

// Returns summaries of all cached frames (for replay/playback tooling).
function get_replay_data() {
    return g_cache.map(function (e) { return e.summary; });
}
