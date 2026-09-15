# Wireshark Dissector Plugin

> This directory is maintained alongside the **BPLC_STA_QtMonitor** project. It contains the
> Wireshark dissector and companion capture-conversion tools for the State Grid
> *Dual-Mode Communication Interoperability Technical Specification Part 4-2: Data Link Layer
> Communication Protocol* (copied from `monitor/HPLC_HRF_Wireshark`).

Wireshark dissector for the State Grid dual-mode communication data-link-layer protocol.
The China Southern Power Grid (CSG) link layer is identical to the State Grid version
(differences are only at the physical layer), so this dissector works for both.

## Files

| File | Description |
|---|---|
| `packet-hplc_rf.lua` | **Lua dissector (recommended, drop-in)** |
| `packet-hplc_rf.c` | C plugin source (outdated, do not use) |
| `bin2pcap.py` | BIN replay file → pcap |
| `serial2pcap.py` | **Live serial capture → pcap** |
| `gen_test_pcap.py` / `gen_assoc_req_test.py` | Test frame generators |
| `DESIGN.md` | Implementation notes |

## Quick start

### Offline replay (bin file)

```
python bin2pcap.py BPLC_xxx.bin
```

Generates a `.pcap` with the same name; open it in Wireshark.

### Live serial capture

```
python serial2pcap.py COM8 460800 capture.pcap
```

Writes pcap while capturing; can be opened live in Wireshark.

### Dissection

Wireshark GUI: copy `packet-hplc_rf.lua` to `%APPDATA%\Wireshark\plugins\`, then restart to auto-load.
Command line: `tshark -r capture.pcap -X lua_script:packet-hplc_rf.lua -V`

## English display

Field language **automatically follows the Wireshark UI language**
(Edit → Preferences → Appearance → Language), no extra setup:

- UI set to English → field names / value_string / tree nodes all English
- UI set to Chinese (or follows system Chinese) → all Chinese

How it works: Wireshark 4.x stores the UI language in `%APPDATA%\Wireshark\language`
(content `language: en`); the dissector reads it at load time to choose field names.

> Env var `HPLC_RF_LANG=en` / `=zh` overrides (takes priority over the UI language).

## Capture file format requirements

- Encapsulation = **USER DLT** (`USER 0` ~ `USER 15`), internal encap = 45~60
- Frame content = raw_wire byte stream as-is (0x3C frame stream, without the BIN replay file header)

## Coverage

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

## Compatibility

The script does not depend on the bit/bit32 library and does not use the native `&` operator
(pure arithmetic `band`), compatible with standard Wireshark and custom builds
(WiresharkRenesas etc.).

## Next steps (optional)

(None — ICV/FCCS/BPCS CRC check is implemented.)
