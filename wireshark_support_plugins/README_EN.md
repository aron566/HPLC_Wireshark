# Wireshark Dissector Plugin

> This directory is maintained alongside the **BPLC_STA_QtMonitor** project. It contains the
> Wireshark dissectors and companion capture-conversion tools for both the State Grid (SGCC)
> and China Southern Power Grid (CSG) dual-mode protocols (the SGCC one was copied from
> `monitor/HPLC_HRF_Wireshark`).

Two independent dissectors, can be loaded at the same time without conflict:

- **packet-gw_2022.lua** — State Grid *Dual-Mode Communication Interoperability Technical
  Specification Part 4-2: Data Link Layer Communication Protocol*
- **packet-nw_2021.lua** — CSG dual-mode 2021 draft (link layer differs substantially from
  SGCC: 4-bit SNID instead of 24-bit NID, 4-byte PB header in SOF blocks, 32B/12B MAC
  header + MSDU header with MAC48b+VLAN, 6-byte MMe management message header)

Filter field names carry a `gw_2022.` / `nw_2021.` prefix respectively, so display/filter/
coloring never clash; captures are told apart by pcap linktype (see
[Capture file format requirements](#capture-file-format-requirements)).

## Files

| File | Description |
|---|---|
| `packet-gw_2022.lua` | **SGCC GW_2022 Lua dissector (drop-in)** |
| `packet-nw_2021.lua` | **CSG NW_2021 Lua dissector** |
| `packet-gw_2022.c` | SGCC C plugin source (outdated, do not use) |
| `bin2pcap.py` | BIN replay file → pcap (auto protocol detect, `--gw`/`--nw` to force) |
| `serial2pcap.py` | **Live serial capture → pcap** (`--gw`/`--nw` to force protocol) |
| `BPLC_Serial_Capture.bat` + `bplc_serial_extcap.py` | **Native serial capture inside Wireshark (extcap)** |
| `gen_test_pcap.py` / `gen_assoc_req_test.py` | SGCC test frame generators |
| `gen_nw_test_pcap.py` | CSG test frame generator (full FCCS/PB CRC24/BPCS/ICV values) |
| `DESIGN.md` | Implementation notes |

## Quick start

### Offline replay (bin file)

```
python bin2pcap.py BPLC_xxx.bin        # auto protocol detection (structure vote, ties fall back to SGCC)
python bin2pcap.py --nw BPLC_xxx.bin   # force CSG NW_2021
python bin2pcap.py --gw BPLC_xxx.bin   # force SGCC GW_2022
```

Generates a `.pcap` with the same name; open it in Wireshark.

### Live serial capture

```
python serial2pcap.py COM8 460800 capture.pcap    # auto protocol detection (--gw/--nw to force)
```

Writes pcap while capturing; can be opened live in Wireshark.

### Serial capture directly in Wireshark (extcap, recommended)

Copy `BPLC_Serial_Capture.bat` and `bplc_serial_extcap.py` to `%APPDATA%\Wireshark\extcap\`
(requires python + pyserial on PATH), restart Wireshark — two interfaces appear in the
capture list:

- **NW_2021_Capture** → dissected by `packet-nw_2021.lua` (USER5)
- **GW_2022_Capture** → dissected by `packet-gw_2022.lua` (USER4)

Configure **COM port** (auto-enumerated, incl. com0com virtual pairs) and **baud rate**
(default 460800), then Start. Each record carries a 4-byte media header
`[phr_mcs][option][channel][isRF]`, so the dissector picks PLC or RF **per frame** —
mixed-media captures are fully supported. Timestamps match serial2pcap (first frame =
local time, then NTB-delta × 40 ns).

### Multi-block frames and byte highlighting

When the MSDU spans several physical blocks (block-tail CRC24 + next PB header sit inside
the MAC frame):

- The hex pane **always shows the complete original frame** (single tab, PB headers and
  block-tail PBCS never removed)
- Clicking "PB N" highlights the whole block (header + CRC included)
- MAC-frame / APP-payload fields highlight their data bytes; **fields crossing a block
  boundary are split into one tree item per block**, each highlighting only that block's
  data segment (inter-block PB header/CRC excluded)
- Field values and ICV/PB CRC checks are computed byte-exactly over the reassembled
  block-body stream

### Dissection

Wireshark GUI: copy `packet-gw_2022.lua` and/or `packet-nw_2021.lua` to
`%APPDATA%\Wireshark\plugins\`, then restart to auto-load.
Command line: `tshark -r capture.pcap -X lua_script:packet-gw_2022.lua -V`
(for CSG captures use `-X lua_script:packet-nw_2021.lua`)

## English display

Field language **automatically follows the Wireshark UI language**
(Edit → Preferences → Appearance → Language), no extra setup:

- UI set to English → field names / value_string / tree nodes all English
- UI set to Chinese (or follows system Chinese) → all Chinese

How it works: Wireshark 4.x stores the UI language in `%APPDATA%\Wireshark\language`
(content `language: en`); the dissector reads it at load time to choose field names.

> Env vars override (take priority over the UI language): SGCC `HPLC_RF_LANG=en` / `=zh`,
> CSG `NW_2021_LANG=en` / `=zh`.

## Capture file format requirements

- Encapsulation = **USER DLT**; frame content = raw_wire byte stream as-is
  (0x3C frame stream, without the BIN replay file header)
- The two protocols use distinct USER channels (chosen automatically by
  `bin2pcap.py` / `serial2pcap.py`):

| Protocol | PLC (carrier) | RF (wireless) | Dissector |
|---|---|---|---|
| SGCC GW_2022 | USER0 = linktype 147 / encap 45 | USER1 = linktype 148 / encap 46 | `packet-gw_2022.lua` |
| CSG NW_2021 | USER2 = linktype 149 / encap 47 | USER3 = linktype 150 / encap 48 | `packet-nw_2021.lua` |
| SGCC GW_2022 **mixed** (serial capture) | USER4 = linktype 151 / encap 49, record = 4B media header `[phr_mcs][option][channel][isRF]` + MPDU | same | `packet-gw_2022.lua` |
| CSG NW_2021 **mixed** (serial capture) | USER5 = linktype 152 / encap 50, same layout | same | `packet-nw_2021.lua` |

With the mixed format the dissector **reads isRF from the media header per frame** and picks
the PLC or RF branch (protocol column shows HPLC/RF per frame), so PLC and RF frames can be
freely interleaved in one capture; the extcap serial capture uses this format. Legacy
pure-MPDU files (linktype 147-150) keep working unchanged.

## Coverage

### SGCC GW_2022 (packet-gw_2022.lua)

| Layer | Status |
|---|---|
| MPDU frame control 16B (Table 13) | ✅ complete |
| DT demux (beacon/SOF/SACK/coordination) | ✅ variable region complete |
| Beacon payload + beacon mgmt info entries (Table 38/44-57) | ✅ complete |
| Physical block header (Table 37) | ✅ complete |
| Standard/single-hop MAC frame header (Table 4/11) | ✅ complete |
| Management message body (21 MMTYPE, Table 60-122) | ✅ complete |
| Wireless discovery list (Table 123-137, TLV) | ✅ complete |
| Source/Destination columns | ✅ complete |
| Bilingual (Chinese/English) display | ✅ complete |
| ICV / FCCS / BPCS CRC check | ✅ complete (raw value + calculated + pass flag) |

### CSG NW_2021 (packet-nw_2021.lua)

| Layer | Status |
|---|---|
| MPDU frame control 16B (frame type / ConInd / SNID + per-frame variable region) | ✅ complete |
| ACK extended frame types (normal/search/sync/channel-switch) | ✅ complete (10-12 CSG extensions deferred, same as Qt monitor) |
| Beacon payload (6B fixed head + 6 mgmt item types + BPCS + reserved byte) | ✅ complete |
| SOF physical blocks (4B PB header + body + reserved + CRC24, multi-block MAC reassembly) | ✅ complete |
| MAC header (long 32B / short 12B / single-hop 4B) + MSDU header (MAC48b + VLAN + type) | ✅ complete |
| MMe management message body (17 MMType) | ✅ complete (0x0083/0x0084/0x00A0 deferred, same as Qt monitor) |
| APP application packet (channel control info + business header + BID description) | ✅ complete |
| Source/Destination columns (with TEI↔MAC learning) | ✅ complete |
| Bilingual (Chinese/English) display | ✅ complete |
| FCCS / PB CRC24 / BPCS / MSDU ICV check | ✅ complete (raw value + calculated + pass flag) |

## Key implementation notes (byte order)

**All multi-byte fields in this protocol are little-endian.** Based on the authoritative
BPLC monitor code: `BitDefine.getdata` → `byte_data << (8*(byte_num-start_byte))` and
`main.py` → `b_nid = data[2] | data[3]<<8 | data[4]<<16`.

- **Bit order**: within a byte bit0 = LSB, continuous across bytes. `read_bits(tvb, start_bit, nbits)` reads by absolute bit offset.
- **12-bit fields** (TEI etc.) cross byte boundaries; use `read_bits` + explicit value add.
- **Whole-byte multi-byte fields** (NID/BTS/MSDU sequence number) little-endian, `tvb(off,len):le_uint()`.
- **Timestamp (BTS/NTB)**: NTB (25 MHz, 40 ns/tick, ~171.8 s wrap). Seconds = NTB/25,000,000.
- **Beacon physical block size**: looked up from FCH byte 9 high 4 bits (TMI) (0/1→520, 2-6→136, 7-10→520, 11/12→264, 13/14→72).
- **Beacon entry length**: the length field includes the header + the length field itself; content length = len_raw-2 (normal) / len_raw-3 (0xC0).

## FAQ

**1. Startup reports `"hplc_rf.xxx" is not a valid protocol field`** —
`%APPDATA%\Wireshark\colorfilters` still uses the old field names; replace `hplc_rf.*`
with `gw_2022.*` (and add `nw_2021.*` rules as needed).

**2. Old plugin steals the DLT / Lua out-of-bounds error on beacons** — remove/rename
`%APPDATA%\Wireshark\plugins\packet-hplc_rf.lua` (superseded by `packet-gw_2022.lua`).

**3. extcap "cannot open serial port" (PermissionError)** — the port is held by another
program, or you picked the wrong end of a com0com virtual pair (e.g. COM90/COM91):
capture from the **other** end of the pair.

**4. COM90/COM91 missing from the dropdown** — pyserial cannot enumerate com0com ports;
the script also reads the SERIALCOMM registry, ports appear once the pair exists.

**5. "Error from extcap pipe" popup on stop** — old builds wrote info lines to stderr
(Wireshark treats any extcap stderr as an error popup); now logged to
`%APPDATA%\Wireshark\extcap\bplc_extcap_debug.log`.

**6. Port cannot be reopened after stop** — old builds left an orphaned python child
holding the port; a parent-process watchdog now releases it within 0.5 s.

**7. Multi-block frames: ICV failed / stray bytes inside the APP payload** — fixed by the
block-body logical view (see Multi-block frames above).

## Compatibility

The script does not depend on the bit/bit32 library and does not use the native `&` operator
(pure arithmetic `band`), compatible with standard Wireshark and custom builds
(WiresharkRenesas etc.).

## Next steps (optional)

(None — ICV/FCCS/BPCS CRC check is implemented.)
