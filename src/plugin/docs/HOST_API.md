# Plugin Host-Control API Contract (v1)

This document is the single source of truth for how scripts control the
host (main window) and how the host notifies scripts about frame selection.
Any new `host.*` API must be agreed here before implementation.

## 1. Design conventions

- **Namespace**: script-to-host calls live on the `host` object
  (`host.jumpToFrame(42)`). Host-to-script callbacks are top-level
  functions the script may define (`on_frame_selected(...)`); if the
  script does not define one, the host silently ignores it.
- **Naming**: `host.*` methods are camelCase (`jumpToFrame`);
  `frame.*` fields are camelCase too. Callback names follow the
  `on_<event>` pattern.
- **Threading**: script code runs on a worker thread. All `host.*`
  calls are fire-and-forget: they are queued to the GUI thread, the
  script never blocks, and **no return value is delivered** back to the
  script. Any data the script needs from the host must come from the
  frame object or a host-to-script callback.
- **Errors**: invalid arguments are ignored silently (no exception in
  the script). Host logs the misuse; the script keeps running.
- **Versioning**: `plugin.json` carries `api_version`. Host-Control v1
  is `api_version: 1`. New `host.*` methods bump the contract version
  in this document; hosts advertise the contract version they implement.

## 2. Script → host (`host.*`)

### `host.jumpToFrame(frameIndex: number): void` — implemented

Request the main window to jump to a decoded frame.

- `frameIndex` is the 1-based decoded frame number (the same value as
  `frame.index` delivered to `parse(frame)`).
- Effect: the frame is selected in the packet list, the list is centered
  on it, and the detail pane shows it. If a display filter is active, it
  is cleared first so the target frame is reachable.
- Invalid index (`< 1`, or beyond the decoded frame count): silently
  ignored, no error is thrown to the script.
- Implemented in-process via
  `IPluginBackend::HostJumpCallback` → `PluginWorker` →
  `LocalPluginEngine` → `MainWindow::jump_packet_to_frame`.
  The standalone `bplc-plugin-host` process has no back-channel to the
  main window yet: in that mode the call is accepted and logged but does
  not move the UI (limitation, see §5).

```js
// topo.js: double-click a frame record in the bottom log
on_dblclick(recordFrame) { host.jumpToFrame(recordFrame); }
```

```lua
-- diag.lua: same contract, same name
host.jumpToFrame(42)
```

## 3. Host → script (`on_*`)

### `on_frame_selected(frameIndex: number, forceHistory: boolean): void` — implemented

Notified when the user selects a frame in the main window's packet list.

- `frameIndex`: 1-based decoded frame number that was selected.
- `forceHistory`:
  - `true` — the frame was double-clicked; the plugin should freeze on
    the historical snapshot up to that frame, even if it is the latest
    decoded frame.
  - `false` — single click; selecting the latest frame returns the
    plugin to live mode, selecting an older frame freezes history.
- The host only calls this when the selection resolves to a valid
  decoded frame.

## 4. Decoded frame object delivered to `parse(frame)` (v1 fields)

| field | type | meaning |
|---|---|---|
| `data` | bytes | unescaped payload |
| `rawWire` | bytes | original on-wire bytes |
| `arrivalUs` | number | capture timestamp (µs) |
| `index` | number | 1-based decoded frame number (0 = raw, undecodable) |
| `epochMs` | number | decoded frame time (epoch ms) |
| `topoEvent` | object\|null | topology event carried by this frame (null = none) |
| `accepted` | boolean | whether the host accepted the frame |
| `rejectReason` | string | drop reason (`accepted=false`), empty otherwise |
| `mpdu` | object | host-decoded MPDU scalars: `ok, frameType, srcTei, dstTei, netId, netType, fchCrcOk, pbCrcOk` |
| `msduPresent` | boolean | frame carries a complete (reassembled) MSDU |
| `msduSummary` | string | MSDU summary, e.g. `"MMeDiscoverNodeList"`, empty if none |

`mpdu` values are pass-through scalars from the host's own decoder:
plugins must not re-parse raw bytes for these fields. Full MSDU field
trees are intentionally *not* pushed per frame; on-demand detail fetch
is reserved (see §5).

## 5. Reserved (not yet implemented — do not ship against these)

- `host.getFrameDetail(frameIndex): void` → planned on-demand delivery
  of the full MSDU field tree for one frame (avoids pushing ~4.6 KB/frame
  unconditionally).
- `host.backToLive(): void` → planned; today TOPO's "Back to Live"
  button only resets plugin-side state.
- Out-of-process host control (`bplc-plugin-host` → main window) is
  pending; currently only the in-process `LocalPluginEngine` path moves
  the UI.

## 6. History

- v1 (2026-10-01): single decoded-frame entry, `index`/`epochMs`/
  `topoEvent`, `host.jumpToFrame`, `on_frame_selected`.
- v1 (2026-10-02): scalar pass-through added (`accepted`,
  `rejectReason`, host-decoded `mpdu`, `msduPresent`/`msduSummary`);
  JS side no longer re-parses MPDU bytes. Contract published in this file.
