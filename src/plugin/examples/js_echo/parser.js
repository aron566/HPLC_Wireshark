// js_echo/parser.js — Example JS plugin
// Plugin API v1: get_info() + parse(frame)

function get_info() {
    return {
        protocolId: "JSECHO_2024",
        displayName: "JS Echo Parser"
    };
}

function toHex(v, w) {
    var s = v.toString(16).toUpperCase();
    while (s.length < w) s = "0" + s;
    return "0x" + s;
}

function parse(frame) {
    var bytes = frame.data;  // number[]
    if (!bytes || bytes.length === 0) {
        return { accept: false, rejectReason: "empty frame" };
    }

    // Build a simple field tree: expand the first few bytes
    var fields = [];
    var n = Math.min(bytes.length, 8);
    for (var i = 0; i < n; i++) {
        fields.push({
            name: "Byte[" + i + "]",
            value: toHex(bytes[i], 2),
            relStart: i,
            relLen: 1
        });
    }
    if (bytes.length > n) {
        fields.push({
            name: "...",
            value: (bytes.length - n) + " more bytes"
        });
    }

    // Simple check: only accept frames whose first byte is 0xAA
    // (demonstrates the reject path)
    if (bytes[0] !== 0xAA) {
        return { accept: false, rejectReason: "first byte != 0xAA" };
    }

    return {
        accept: true,
        summary: "JSEcho: " + bytes.length + " bytes",
        mpdu: {
            frameType: 1,
            srcTei: bytes.length > 2 ? bytes[2] : 0,
            dstTei: bytes.length > 3 ? bytes[3] : 0
        },
        fields: [
            {
                name: "EchoFrame",
                value: toHex(bytes[0], 2),
                relStart: 0,
                relLen: bytes.length,
                children: fields
            }
        ]
    };
}
