# BPLC STA Monitor User Manual

Applies to: `v1.0.15`  
Platform: Windows

## 1. Overview

BPLC STA Monitor is a Windows desktop application for monitoring BPLC/HRF (HPLC) STA frames. It can:

- Capture `0x3C ... 0x3E` frames from a live debug serial port.
- Replay raw `.bin` files or raw hex text files.
- List BEACON, SOF, ACK, COORD, SEARCH, and SWITCH frames.
- Decode FCH, MPDU, PB, MSDU, MMe, and APP fields.
- Link decoded protocol fields to highlighted bytes in the hexadecimal view.
- Export captured data to replayable `.bin` files or raw hex text.
- Count frame types, dropped frames, and reassembled MSDUs.

## 2. Installation and Startup

### 2.1 Installer

The installer is under `dist/` and has a name similar to:

```text
BPLC_STA_Monitor_Setup_v1.0.15.exe
```

Installer behavior:

- Per-user installation; administrator rights are not required.
- Default installation directory:

```text
%LOCALAPPDATA%\Programs\BPLC_STA_Monitor
```

- Desktop and Start Menu shortcuts are created.
- An upgrade closes the running application and replaces the old program files.
- An existing `config.ini` is preserved during an upgrade. An uninstall removes the installation directory, so back up configuration files and captured data first.

### 2.2 Portable Run

If the `release/` or installation directory contains the required Qt runtime DLLs, run:

```text
BPLC_STA_Monitor.exe
```

## 3. Quick Start

### 3.1 Live Serial Capture

1. Connect the STA debug serial port.
2. Click `Start` or press `Ctrl+E`.
3. Select `Live Serial Port`.
4. Select the COM port and baud rate. The current capture path operates as 8 data bits, 1 stop bit, no parity.
5. Click `Start Capture`.
6. New frames are appended to the frame list in arrival order.
7. Click `Stop` to end capture.

### 3.2 Replay a `.bin` File

1. Click `Start`.
2. Select `File Replay (.bin)`.
3. Select the `.bin` file.
4. Click `Start Capture`.
5. The application automatically detects 8-byte BCD time tags at the file beginning or at segment boundaries and restores the capture timeline where possible.

### 3.3 Replay Raw Hex Text

1. Click `Start`.
2. Select `Raw Hex Text`.
3. Select a `.txt` or `.hex` file.
4. Click `Start Capture`.
5. A `TIME: yyyy-MM-dd HH:mm:ss.zzz` line establishes the time base for that segment.

### 3.4 Inspect a Frame

1. Select a row in the frame list.
2. The protocol field tree appears at the lower left.
3. Click a field such as `Net ID`, `PB CRC24`, or `MSDU CRC32`.
4. The hexadecimal view at the lower right highlights the bytes covered by that field.
5. The `RAW DATA` pane shows the complete original `0x3C ... 0x3E` frame.

## 4. Main Window

### 4.1 Toolbar

| Button | Function |
|--------|----------|
| Start | Opens the data-source dialog and starts serial capture, file replay, or raw hex import |
| Stop | Stops the current capture |
| Pause | Stops adding new parsed frames to the list; click again to resume |
| Clear | Clears the frame list, statistics, protocol tree, and byte view |
| Export | Exports all frames as `.bin` or raw hex text |
| Settings | Opens the communication, language, and theme settings dialog |
| Display filter | Applies a filter expression to the frame list |

Note: in the current `v1.0.15` source state, the toolbar `Settings` button is not connected to the settings dialog. Language, theme, and serial parameters can still be changed in the `Start` dialog or by editing `config.ini`.

### 4.2 Frame List

| Column | Meaning |
|--------|---------|
| `#` | One-based frame index; resets after `Clear` |
| `Time` | Frame time. File replay prefers BCD/time-tag values; live capture uses local receive time |
| `Delta` | Time difference from the previous frame, in seconds |
| `Orig Src` | Original MSDU source TEI, displayed as `CCO` or `STA-N` |
| `Source` | MPDU source TEI; may show `PLC`, `HRF`, or `DROP` |
| `Destination` | MPDU destination TEI; broadcast is shown as `BROADCAST` |
| `Orig Dst` | Original MSDU destination TEI, displayed as `CCO`, `STA-N`, or `BCAST` |
| `Dir` | `UP`, `DOWN`, broadcast, or unknown direction |
| `Protocol` | `HPLC`, `HRF`, or `ERR` for rejected frames |
| `Frame Type` | `BEACON`, `SOF`, `ACK`, `COORD`, `SEARCH`, `SWITCH`, and so on |
| `MSDU Type` | Reassembled MSDU/MMe/APP summary |
| `MSDU Seq` | MSDU sequence number |
| `Length` | Stored raw byte length |
| `Info` | NetID, TEI, TMI, PBNum, MSDU size, or rejection reason |

Row colors:

- BEACON: blue
- SOF: green
- ACK: yellow
- COORD: red
- DROP/error: gray

The list follows the newest frame by default. Scrolling upward suspends automatic following; scrolling back to the bottom resumes it.

### 4.3 Protocol Tree

The protocol tree contains:

- Physical: media, NTB timestamp, channel/band, PHR MCS, option, and frame time.
- MPDU Base: frame type, network type, NetID, version, and FCH CRC24.
- BEACON: timestamp, source TEI, TMI, symbol count, line, beacon management items, and CRC.
- SOF: source/destination TEI, Link ID, frame length, PB count, flags, TMI, and TMI_EXT.
- SOF PB: per-block Header, Body, Padding, and CRC24.
- MSDU: reassembled common header, MMe/APP fields, and MSDU CRC32.
- ACK: regular ACK, Search, Sync, and channel-switch variants.
- COORD: duration, next timeslot shift, neighboring NetID, and RF channel.

`[Nb]` after a field name indicates a bit width in bits.

### 4.4 Hex and Raw Views

The hexadecimal view format is:

```text
offset  byte0 ... byte7  byte8 ... byte15  ASCII
```

- Group nodes that do not directly map to bytes do not produce a highlight.
- When one MSDU spans multiple PB blocks, only fields that map to a continuous byte range in the current frame can be highlighted.
- Right-click the hex view to copy the highlighted bytes as plain hex or `0x`-prefixed hex.
- Right-click the raw view to copy the complete frame as plain hex or `0x`-prefixed hex.

### 4.5 Status Bar

The left side shows current status, source, replay progress, or errors. The right side shows:

- `Total`
- `BEACON`
- `SOF`
- `ACK`
- `COORD`
- `Drop`
- `MSDU`

`MSDU` counts reassembled MSDUs. `Total` is the sum of statistics categories and is not necessarily equal to the number of rows in the frame list.

## 5. Frame Operations

### 5.1 Pause

Pause only stops adding new parsed frames to the list. The capture or replay source continues running, and frames arriving while paused are not stored for later. They will not appear after resuming.

### 5.2 Clear

Clear removes:

- Current frame list
- Pending frame queue
- Frame statistics
- Protocol tree
- Hex and raw views
- Frame counter

Clear does not stop serial capture.

### 5.3 Stop

Stop closes the serial port or ends the current source task. File replay completes as quickly as possible and then returns to idle.

## 6. Display Filter

### 6.1 Syntax

- `&` means AND.
- `|` means OR.
- `&` has higher precedence than `|`.
- Parentheses are not supported.

Example:

```text
a & b | c
```

is equivalent to:

```text
(a AND b) OR c
```

Click `Apply` or press Enter to apply the filter. The filter affects existing and newly arriving frames and is saved in `config.ini` for the next launch.

### 6.2 Matchable Values

| Content | Syntax | Example |
|---------|--------|---------|
| Frame type, exact | `beacon`, `sof`, `ack`, `coord`, `search`, `switch` | `sof` |
| NetID | Hexadecimal, `0x` optional | `cda1d5`, `0xcda1d5` |
| Source/destination | `cco`, `sta-N`, `broadcast` | `sta-2` |
| Media | `hplc`, `hrf` | `hplc` |
| MSDU/MMe/APP summary | Summary text | `assoc`, `event` |
| Frame index | Decimal substring | `42` |
| Dropped frame | `drop`, `err`, or rejection reason | `drop` |

Examples:

```text
beacon & cda1d5
```

Shows BEACON frames for NetID `cda1d5`.

```text
sof & sta-2 | ack
```

Shows SOF frames whose source or destination contains STA-2, or all ACK frames.

```text
coord & 0xcda1d5
```

Shows COORD frames for the selected network.

```text
hplc & beacon
```

Shows HPLC BEACON frames only.

## 7. Export

Click `Export` and choose:

- Replay file `*.bin`
- Raw hex text `*.txt`

Export includes all frames in the current session, regardless of the active display filter. The default file name is:

```text
BPLC_yyyyMMdd_HHmmss.bin
```

### 7.1 Replay `.bin`

The file preserves each original `0x3C ... 0x3E` frame. An 8-byte BCD time tag is written before the first frame and before each new time segment so replay can reconstruct the timeline.

### 7.2 Raw Hex Text

Each line contains a complete original frame:

```text
0x3C 0x.. ... 0x3E
```

A time line may precede a segment:

```text
TIME: 2026-09-07 18:43:00.123
```

The resulting file can be imported again with the `Raw Hex Text` source.

## 8. Configuration File

Configuration path:

```text
<application directory>\config.ini
```

The file is created on first launch. Deleting it causes defaults to be regenerated on the next launch.

Main settings:

```ini
[general]
lang=auto
update_url=https://raw.githubusercontent.com/aron566/HPLC_Wireshark/main/update.json
filter=
theme=auto

[reader]
mode=0
com=COM3
baud=460800
file_path=
time_tag=false
```

| Setting | Values |
|---------|--------|
| `lang` | `auto`, `zh`, `en` |
| `theme` | `auto`, `dark`, `light` |
| `filter` | Frame-list display filter |
| `update_url` | `update.json` manifest URL |
| `mode` | `0` serial, `1` `.bin` replay, `2` raw hex |
| `com` | Serial port name |
| `baud` | Baud rate |
| `file_path` | Replay/import file path |
| `time_tag` | Legacy compatibility field; current formats auto-detect BCD time tags |

Language and theme changes made in the `Start` dialog are normally saved immediately. Some window-framework text is fully updated after restarting the application.

## 9. Checking for Updates

Use:

```text
Help -> Check for Updates
```

The application reads the manifest at `general/update_url`, compares versions, and prompts for download when a newer version is available.

Manifest format:

```json
{
  "updates": {
    "windows": {
      "latest-version": "1.0.15",
      "download-url": "https://example.com/BPLC_STA_Monitor_Setup_v1.0.15.exe",
      "changelog": "Release notes",
      "mandatory-update": false
    }
  }
}
```

The changelog string may contain `<br/>`.

## 10. Input File Formats

### 10.1 Frame Boundary

Serial and `.bin` input use:

```text
0x3C <data> 0x3E
```

The reserved bytes `0x3C`, `0x3E`, and `0x3D` inside data are escaped:

| Original | Escaped |
|----------|---------|
| `0x3C` | `0x3D 0xC3` |
| `0x3E` | `0x3D 0xC1` |
| `0x3D` | `0x3D 0xC2` |

Unescape with `original = 0xFF - escaped_byte` after `0x3D`.

### 10.2 Frame Data

After unescaping:

```text
[dlen 2B LE][ts 4B LE][phr_mcs 1B][option 1B][channel 1B][isRF 1B][MPDU...]
```

| Field | Meaning |
|-------|---------|
| `dlen` | `MPDU length + 4`; not validated by the reader |
| `ts` | NTB tick, little-endian 32-bit, 40 ns/tick, wraps in about 171.8 seconds |
| `phr_mcs` | HRF physical MCS/modulation information |
| `option` | Physical-layer option |
| `channel` | HRF channel or PLC band |
| `isRF` | `0` for PLC, non-zero for HRF |
| `MPDU` | FCH and subsequent PB/payload data |

### 10.3 `.bin` Time Tag

An independent 8-byte BCD time tag may appear at the file beginning or before a segment:

| Byte | Content |
|------|---------|
| 0 | Year minus 2000 |
| 1 | Month |
| 2 | Day |
| 3 | Hour |
| 4 | Minute |
| 5 | Second |
| 6 | Hundreds of milliseconds |
| 7 | Lower two digits of milliseconds |

### 10.4 Raw Hex Text

- A standalone time line must parse as `yyyy-MM-dd HH:mm:ss[.zzz]`; the `TIME:` prefix is recommended.
- A frame line must contain one complete raw frame with `0x3C` and `0x3E`.
- Both `0x01 0xd5` and `01 d5` byte styles are accepted.
- Legacy raw MPDU lines without `0x3C/0x3E` sentinels are no longer supported and are ignored.

## 11. Troubleshooting

### 11.1 Serial Port Cannot Be Opened

- Make sure another program is not using the COM port.
- Confirm the STA debug port is connected.
- The default baud rate is `460800`.
- The current serial implementation uses 8-N-1.

### 11.2 No Frames Appear During File Replay

- `.bin` input must use the `0x3C ... 0x3E` frame format.
- Every raw hex row must contain the complete sentinels.
- Confirm the file is not empty and has not been rewritten into an unsupported legacy format.

### 11.3 Frames Are Marked DROP

Common causes:

- FCH CRC24 failure
- Invalid PB configuration
- PB block exceeds frame length
- Frame too short
- Link, NetID, TEI, or frame-type rejection by a parser filter

Check the `Destination` or `Info` column for the rejection reason.

### 11.4 Delta Looks Wrong

- Live capture starts a new segment after a long silence or an NTB wrap around 171.8 seconds.
- File replay depends on BCD/TIME tags to restore absolute time.
- Older files without time tags fall back to local time.

### 11.5 A Protocol Field Cannot Be Highlighted

- The selected node is a grouping node and does not map directly to bytes.
- The MSDU spans multiple PB blocks and the field crosses a PB boundary.
- The frame was rejected or its payload is incomplete.

## 12. Keyboard Shortcuts

| Shortcut | Function |
|----------|----------|
| `Ctrl+E` | Start |
| `Ctrl+.` | Stop |
| `Ctrl+P` | Toggle the Pause menu action |
| `Ctrl+L` | Clear |
| `Ctrl+F` | Apply the display filter |
| `Enter` | Apply the filter from the filter input box |

The Pause menu item and toolbar button are not fully synchronized in the current version. Prefer the toolbar `Pause/Resume` button when reliable pausing is required.

## 13. Current Limitations

- Detailed protocol trees focus on BEACON, SOF, ACK, and COORD. SEARCH and SWITCH can appear in the list but do not have equally complete payload decoders.
- Filter expressions do not support parentheses.
- Export always writes all frames, not only the filtered view.
- New frames arriving while paused are not buffered.
- The toolbar `Settings` button is not connected in the current source version.
- Serial data-bit, stop-bit, and parity selections are not written into the effective capture configuration; the application operates as 8-N-1.

## 14. Build and Packaging

Environment:

| Component | Version |
|-----------|---------|
| Qt | 6.10.1 mingw_64 |
| MinGW | 13.1.0 |
| Build | qmake + mingw32-make |

Build:

```bash
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.10.1/mingw_64/bin:$PATH"
qmake BPLC_STA_Monitor.pro
mingw32-make -j4
```

Output:

```text
release/BPLC_STA_Monitor.exe
```

Package:

```bash
bash scripts/package.sh 1.0.15
```

Output:

```text
dist/BPLC_STA_Monitor_Setup_v1.0.15.exe
```

