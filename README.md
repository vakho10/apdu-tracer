# APDU Tracer

A Windows desktop tracer built with **C++20, Qt 6 Widgets, and CMake**.
It reads the installed USBPcap driver directly and lists sent (`>>`) and
received (`<<`) APDUs in a table, with ISO 7816-4 command names and status
word meanings.

TLV decoding, themes, and an installer can follow later.

## Download

Each [GitHub release](https://github.com/vakho10/apdu-tracer/releases) has a
portable `APDUTracer-<version>-win64.zip` with a `.sha256` checksum. Extract it
and run `bin\APDUTracer.exe`; the Qt runtime is included. Install the
[USBPcap driver](https://desowin.org/usbpcap/) separately for live capture.

To verify a download, compare the hash with the `.sha256` file. In PowerShell,
run `Get-FileHash APDUTracer-<version>-win64.zip`. On Linux, macOS or WSL, run
`sha256sum -c APDUTracer-<version>-win64.zip.sha256`.

## Build on Windows

Requirements:

- Windows 10/11 x64.
- Visual Studio 2022 with **Desktop development with C++** and a Windows SDK.
- CMake 3.21 or newer (also available in Visual Studio).
- Qt 6.5 or newer with **Widgets** and an MSVC x64 kit matching your compiler.
  The implementation is tested with Qt 6.8.1 and MSVC 2022.
- The [USBPcap driver](https://desowin.org/usbpcap/) for live capture. Install it
  separately and restart Windows if requested. Offline replay needs no driver.

From PowerShell at the repository root, adjust the Qt kit path:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.1/msvc2022_64"
cmake --build build/windows --config Release --parallel
$env:PATH = "C:\Qt\6.8.1\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build/windows -C Release --output-on-failure
& .\build\windows\Release\APDUTracer.exe
```

Alternatively, open `CMakeLists.txt` in Qt Creator and select your MSVC
kit. A DLL/plugin error at startup usually means Qt's `bin` directory is missing
from `PATH`. For a self-contained local application folder:

```powershell
$apduDist = Join-Path (Get-Location) 'dist'
cmake --install build/windows --config Release --prefix $apduDist
& .\dist\bin\APDUTracer.exe
```

On Windows the install step uses Qt's deployment support to collect runtime
DLLs and plugins. It does not install or bundle the USBPcap driver. Qt's
[Windows deployment guide](https://doc.qt.io/qt-6/windows-deployment.html)
describes runtime deployment. The install step also copies `LICENSE`,
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md) and the Qt license texts in
`licenses/`; keep them with any redistributed runtime. Releases are portable
zip files, not installers.

## Use

1. Select a **Capture device**. Each USBPcap root hub (`USBPcap1`, `USBPcap2`, …)
   is listed as *entire root hub*, followed by the devices currently attached to it,
   e.g. `USBPcap2 – Smart card reader (address 5)`. Devices behind external hubs
   are included. Composite devices are named after their functions. **Refresh**
   rescans after plugging a reader in; listing never starts a capture or blocks
   one already running in another program.
2. Selecting a device sets **USB address** to that device. During live capture the
   USBPcap driver then records only that address, so storage, webcam, or other
   traffic on the same hub never fills its buffer. **All** captures the whole
   hub, including devices attached after capture starts. A device gets a new
   address when it is reconnected, so refresh and reselect it. The address filter
   also applies to offline replay.
3. Leave **Reader transport** on **Auto**. Choose **T=1** for a TPDU reader when
   starting mid-session, or **APDU** to force ordinary APDU-level interpretation.
   This setting also applies to offline replay; Auto detects each card slot separately.
4. Optionally tick **Save raw capture…** and choose a `.pcap` file. The next live
   capture also writes the unmodified USBPcap stream there, so the session can be
   reopened later with **Open PCAP** in another transport mode or examined in
   Wireshark. Each chosen file is used for one capture, so a later capture never
   overwrites it; tick the box again to save the next one. On Stop, a USB record cut
   in half is trimmed so the file ends on a complete record. The raw file contains
   what the driver captured: only the selected USB address, or the whole hub for **All**.
5. Click **Start capture**, then use your existing smart-card application.
6. Each APDU is a table row: number, time, direction, bus, device, card slot,
   CCID sequence, length, **Info**, and the APDU bytes. **Info** names the command
   (SELECT, GET RESPONSE, VERIFY, and common GlobalPlatform and EMV commands) or
   explains the response status word, such as `6A82 — File or application not
   found`. Error statuses are red and warnings orange. Very long APDUs are
   shortened in the table; hover over the bytes to see more.
7. **Filter** shows only rows containing hex bytes (`90 00` or `9000`, matched on
   byte boundaries) or whose **Info** contains the text (`select`, `not found`).
   The status bar then shows how many rows match.
8. **Clear** empties the display and the APDU counter at any time. During a capture
   the capture keeps running and only APDUs after the click are shown.
9. Select rows and press **Ctrl+C**, or right-click, to copy them; the menu can
   also copy just the APDU bytes. Click **Stop** before **Save text**. Copy and
   Save use the rows currently shown, with all bytes, in this format:

```text
[12:34:56.001] bus 1 / device 5 / slot 0 / seq 7  >>  00 A4 04 00 00
[12:34:56.013] bus 1 / device 5 / slot 0 / seq 7  <<  90 00
```

If opening the capture device fails with access denied, run the app as
Administrator. Close any other capture using the same controller. The app
does not automatically elevate or install drivers.

**Open PCAP** reads a classic `.pcap` with USBPcap link type 249. You can also
pass a capture filename on the command line. Each start/open begins a new
displayed session. ATRs and reader-control traffic are intentionally omitted.
CCID fragments and T=1 transport blocks are joined into complete APDUs. A joined
entry keeps its first fragment's timestamp, and a response keeps the original
command's sequence number even when continuation transfers use different numbers.

The display retains recent APDUs up to **20,000 rows / 8 MiB of APDU bytes**, and
text export saves the rows shown. The worker queue is bounded by both row count
and APDU bytes; if the display cannot keep up
with live APDU traffic, the status bar reports the number of dropped display
entries. USBPcap itself silently drops packets when its kernel buffer
overflows, and this app cannot count those losses. **Driver buffer** (default
16 MiB, 4–128 MiB of non-paged memory) sets that buffer for live capture; raise
it for busy hubs, or capture only the reader's address.

## Initial scope and limitations

- APDU-level **USB CCID** exchanges support both command and response chaining,
  including the empty continuation requests used for long reads.
- **T=1 TPDU** exchanges support LRC/CRC checksums and chained command/response
  I-blocks. R-block acknowledgements, IFS negotiation, and WTX exchanges are
  omitted from the APDU display; identical I-block retransmissions are suppressed.
- Character-level exchanges, T=0 TPDU reconstruction, the APDU inside secure
  PIN commands, vendor escape payloads, non-CCID readers, and contactless
  protocols outside CCID are not decoded. PIN pad and escape commands do not
  disturb decoding of the surrounding APDUs.
- CCID framing and APDU lengths are checked conservatively. Reader descriptors
  are not decoded. Auto infers T=1 from an IFS handshake, an unambiguous complete
  command frame, or a chained frame and its acknowledgement. Some byte patterns
  are valid both as APDUs and T=1 blocks; use the explicit transport setting when
  needed, particularly **T=1** when capturing mid-session. An ambiguous IFS
  lookalike may wait for its response or capture end before appearing in Auto mode.
- Responses require a captured matching command on the same bus, device, slot,
  and transfer sequence. Starting halfway through an exchange can omit its response.
- Composite devices with independent CCID interfaces reusing the same slot
  numbers are not supported yet; response pairing does not distinguish them.
- Power-on responses are tracked separately as ATRs. Failed CCID commands and
  time-extension notifications are not APDU responses.
- Records truncated by the capture's snapshot length (often USB storage on the
  same root hub) are skipped; for the reader they only drop the affected exchange.
- Partial USB transfers are reassembled; invalid lengths, truncated packets,
  invalid T=1 checksums, and incomplete chains do not become displayed APDUs.
  After damaged T=1 traffic, recovery may omit an exchange until a safe boundary.
- **PCAPNG is not supported yet**; save/export captures in classic PCAP format.
  Only USBPcap link type 249 is supported, including when replaying elsewhere.
- Live capture is Windows-only. The decoder and offline Qt viewer are portable.

## Verification

`decoder_tests` constructs synthetic captures to exercise PCAP boundaries,
endianness, USB direction, CCID fragmentation, APDU validation, ATRs, multiple
slots/devices, malformed input, and truncation. It needs no Qt or driver:

```powershell
cmake -S . -B build/core -DAPDU_BUILD_GUI=OFF
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

The full Windows build registers seven CTest checks: `decoder_tests`, `t1_tests`,
`transport_tests`, `usbpcap_backend_tests`, `generate_fixture`, `qt_smoke_tests`,
and `worker_tests`. The transport tests synthesize long reads and T=1 exchanges
matching the reader patterns reported during hardware trials, with generated
payloads rather than personal card data.
With the GUI enabled, CTest generates a synthetic `.pcap` and runs the
Qt window offscreen, replaying it through the worker to verify both directions
reach the display. Synthetic tests do not establish hardware compatibility.
On Windows a backend harness substitutes the driver-facing Win32 calls to test
configuration, streaming, idle-read cancellation, and cleanup on errors. It
checks the exact 17-byte address filter sent for **All** and for specific
addresses, the configured driver buffer size, and the device-listing helpers.
Worker tests also exercise lossless replay of a record containing 12,000 commands,
cancellation under backpressure, and bounded display memory for long APDUs.
They also write a stream in irregular chunks through the raw capture writer,
cut its last record in half, and check that the saved file is trimmed and replays
to the same APDUs.

For a manual hardware check, select your reader from the device list (check
that its name and address match Device Manager or USBPcapCMD), start capture,
send a known SELECT using your card application, and confirm its command bytes
and response/status word. Stop while the reader is idle, then start again to
verify cancellation and handle cleanup. While capturing only the reader,
copy a file to a USB drive on the same hub and confirm the trace is unaffected.
Repeat with another slot/reader when available.
The per-device list and driver-side address filter have not yet been tried on
hardware. Earlier hardware trials confirmed capture on two readers and exposed
the transport gaps addressed here. The updated reassembly still needs a repeat
live check; automated tests use synthetic captures.

## Structure

```text
CMakeLists.txt  Application, libraries, tests, and runtime deployment
src/capture/   Direct Win32 USBPcap driver access and a portable stub
src/core/      Incremental PCAP, USBPcap, and CCID decoding (standard C++ only)
src/ui/        Qt worker, bounded text queue, raw .pcap writer, and main window
tests/         Synthetic protocol regression tests and fixture generation
ui-tests/      Qt asynchronous replay and worker tests
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and validation guidance.

The driver wire format and IOCTLs were checked against the upstream
[USBPcap public header](https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/include/USBPcap.h)
and USBPcapCMD's [capture](https://github.com/desowin/usbpcap/blob/master/USBPcapCMD/thread.c)
and [device enumeration](https://github.com/desowin/usbpcap/blob/master/USBPcapCMD/enum.c) code.
CCID framing follows the
[USB-IF CCID specification](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf).
No driver or USBPcapCMD implementation is bundled. Application source uses the
repository's [MIT license](LICENSE); Qt and the separately installed USBPcap retain their
own licenses. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the
components shipped in release packages.
