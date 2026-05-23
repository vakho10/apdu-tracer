# APDU Tracer

A desktop app that traces **APDUs** (Application Protocol Data Units) sent to
and received from a smart card reader. It captures live USB traffic with
Wireshark's `tshark` over a **USBPcap** interface — or replays a saved
`.pcap` / `.pcapng` capture file — decodes the CCID frames, and shows each
APDU with full ISO 7816-4, EMV, PIV, OpenPGP and GlobalPlatform decoding.

Built with **Electron** and **TypeScript**.

## Features

### Capture

- **USBPcap** interface picker with one-click start / stop tracing.
- **Open a `.pcap` / `.pcapng`** file to analyse a trace offline (`tshark -r`).
- Pairs each command with its response **per USB device address**, so multiple
  readers on one host controller don't cross-contaminate latency or TLV gating.
- Captures and decodes the **ATR** from `IccPowerOn` — convention (direct /
  inverse), offered transmission protocols, and historical bytes (with the
  category indicator and COMPACT-TLV data objects named).
- Configurable `tshark.exe` path; a **Locate tshark…** button surfaces in the
  header when tshark can't be invoked.

### Views — tabbed and live

- **Simple** — one APDU per line, with the **time gap since the previous APDU**.
- **Detailed** — each APDU as a card:
  - Decoded instruction name (ISO 7816-4, with **GlobalPlatform** names under a
    proprietary `CLA`), `CLA`/`INS`/`P1`/`P2`/`Lc`/`Le` fields, ISO **case 1–4**,
    a **secure-messaging chip** when the CLA flags SM, and a `P1`/`P2` note
    (SELECT method, READ BINARY/RECORD offsets, INSTALL phase, GET STATUS
    subset, …).
  - Response status word with its meaning, command↔response **latency**, and a
    collapsible **BER-TLV tree** for well-formed data.
  - Per-card **Copy** button puts the raw hex on the clipboard.
- **Summary** — per-session totals, error rate, latency min/avg/max and the
  slowest exchange, plus a command-by-instruction breakdown.
- **Decode** — paste APDU hex and decode it without capturing, using the active
  TLV profile.

### TLV decoding

- Application-aware tag dictionaries — **EMV**, **PIV**, **OpenPGP** — layered
  over the generic ISO 7816-4 set, **auto-selected from the `SELECT` AID** or
  chosen manually from the profile dropdown.
- Primitive values rendered in human-readable form: ASCII text, life-cycle byte,
  BCD dates (`20YY-MM-DD`) and times (`HH:MM:SS`), numeric fields, and binary
  counters.
- Under the EMV profile, structured / bit-field tags are decoded too:
  - **AIP** (`82`), **TVR** (`95`), **AUC** (`9F07`) and **TSI** (`9B`) — listed
    as their set-flag meanings.
  - **AFL** (`94`) — record ranges per file (SFI, first–last, offline-auth count).
  - **CVM List** (`8E`) — verification rules (method + condition).

### Trace handling

- **Filter & search** by direction, **Issues only**, or hex / instruction /
  status text (`Ctrl + F`). `F3` jumps the Detailed view to the next error or
  warning, flashing the target card.
- **Copy** any APDU's hex; **Import** a saved JSON trace; **Export** to
  `.txt`, `.json` or `.csv` — honouring the active filter.
- **Light / dark / system** theme, **auto-scroll** toggle, configurable
  `tshark` path, and the last active view all **persist between sessions**.
- Detailed-view cards are **virtualised** (Chromium `content-visibility`), so
  traces with thousands of APDUs stay smooth.

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| **Node.js** | v18 or newer. |
| **Wireshark** | Provides `tshark`. The app looks in `C:\Program Files\Wireshark\` and the `(x86)` variant, then falls back to `PATH`. If it lives elsewhere, use **Locate tshark…** in the header. |
| **USBPcap** | USB capture driver. Install via the Wireshark installer ("USBPcap" component) or from [usbpcap.com](https://desowin.org/usbpcap/). A reboot is required after installing. |

## Installation

```bash
git clone https://github.com/Vakho10/apdu-tracer.git
cd apdu-tracer
npm install
```

> If the Electron binary download stalls, use a mirror:
> ```bash
> # Windows (PowerShell)
> $env:ELECTRON_MIRROR="https://npmmirror.com/mirrors/electron/"; npm install
> ```

## Usage

```bash
npm run dev
```

1. Pick a **USBPcap interface** (use **Refresh** to rescan). To analyse a
   saved capture instead, **Open file…** loads a `.pcap` / `.pcapng`.
2. Press **Start tracing**.
3. Interact with your smart card reader — captured APDUs appear live in the
   active tab.
4. Switch between **Simple**, **Detailed**, **Summary** and **Decode** tabs at
   any time; arrow keys cycle the tab list when focused.
5. Press **Stop tracing** when done.
6. **Save trace** writes to `.txt`, `.json` or `.csv` (the format follows the
   chosen extension); **Import** reloads a saved JSON trace.

### Keyboard shortcuts

| Key | Action |
|-----|--------|
| `Ctrl + F` | Focus the filter / search box. |
| `F3` | Jump the Detailed view to the next error or warning. |
| `← / →` | Cycle through the view tabs (with the tab list focused). |

### Simple view

Each line is formatted as:

```
[HH:MM:SS +120ms] >> 00 A4 04 00 07 A0 00 00 00 03 00 00
[HH:MM:SS +12ms]  << 6F 1A 84 ... 90 00
```

`>>` is a command sent to the card, `<<` is the response received from it.
The `+Nms` is the gap since the previous APDU (absent for the first APDU and
for imported traces).

### Detailed view

Each APDU is a card:

- **Command** — decoded instruction name (GlobalPlatform names take precedence
  under a proprietary CLA), `CLA`/`INS`/`P1`/`P2`/`Lc`/`Le`, ISO case 1–4,
  optional **SM** chip when secure messaging is in use, and a P1/P2 note.
- **Response** — data, status word and its meaning (e.g. `6A82` — "file or
  application not found"), round-trip latency since the matching command, and
  a collapsible BER-TLV tree where applicable. Each tag is named from the
  active TLV profile (EMV / PIV / OpenPGP, falling back to ISO 7816-4) and
  shown with its length, raw value, and a decoded interpretation when known.
- **ATR** — a card-power-on event, decoded into convention, protocols, and
  historical bytes (COMPACT-TLV data objects listed by name).

## How it works

The main process spawns:

```
tshark -i <interface> -l -n \
  -Y "usbccid.bMessageType == 0x6f || usbccid.bMessageType == 0x80 || usbccid.bMessageType == 0x62" \
  -T fields -e usbccid.bMessageType -e usb.endpoint_address.direction \
  -e data.data -e usb.capdata -e usb.device_address
```

- `0x6f` = `PC_to_RDR_XfrBlock` (command), `0x80` = `RDR_to_PC_DataBlock`
  (response), `0x62` = `PC_to_RDR_IccPowerOn` (the next DataBlock carries the
  ATR).
- The APDU payload is read from the CCID `abData` field, with a fallback that
  strips the 10-byte CCID header from raw `usb.capdata`.
- `usb.device_address` keys command/response pairing to each reader.
- A capture file is read the same way with `tshark -r <file>` instead of `-i`.

Captured records are streamed to the renderer over a context-isolated IPC
bridge, where ISO 7816-4 / EMV / PIV / OpenPGP / GlobalPlatform decoding happens.

## Scripts

| Command | Description |
|---------|-------------|
| `npm run dev` | Start the app with hot reload. |
| `npm run build` | Build the production bundle into `out/`. |
| `npm run build:win` | Build and package a Windows installer. |
| `npm run typecheck` | Run the TypeScript compiler with no emit. |

## Project structure

```
src/
  main/                 Electron main process — tshark capture, IPC, persisted settings
  preload/              Context-isolated bridge exposing window.api
  renderer/             UI — semantic HTML5, CSS3, TypeScript
    src/apdu.ts         APDU parsing — ISO + GP instruction names, status words,
                        secure-messaging indication, case 1-4 derivation
    src/tlv.ts          BER-TLV parser; ISO / EMV / PIV / OpenPGP tag dictionaries;
                        value decoding (text, dates, BCD, bit fields, AFL, CVM List)
    src/atr.ts          ATR parser; historical-byte COMPACT-TLV decoding
    src/main.ts         UI logic — views, filter, summary, decode, IPC handlers
```

## Troubleshooting

- **No USBPcap interfaces listed** — USBPcap is not installed, or the machine
  has not been rebooted since installing it.
- **`tshark` not found** — install Wireshark, or click **Locate tshark…** in
  the header to point the app at your `tshark.exe`.
- **Frames captured but APDUs are empty** — the CCID payload field can vary
  between Wireshark builds; adjust the field extraction in
  `src/main/index.ts` (`handleLine`).
- **Permission errors on capture** — USBPcap may require running the app as
  Administrator.

## License

MIT
