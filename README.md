# APDU Tracer

A Windows desktop tracer built with **C++20, Qt 6 Widgets, and CMake**.
It reads the installed USBPcap driver directly and displays sent (`>>`) and
received (`<<`) APDUs as hexadecimal text.

This initial version focuses on capture and text. APDU interpretation, TLV
decoding, themes, and installers can follow later.

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
describes runtime deployment. Keep Qt and third-party license notices with any
redistributed runtime; this prototype does not produce a release installer.

## Use

1. Select a **USB controller** (`USBPcap1`, `USBPcap2`, …). USBPcap captures a root
   hub, not an individual reader. **Refresh** rescans the available controllers.
2. Leave **USB address** at **All**, or choose the known USB address of your
   reader on that controller. The filter also applies to offline replay.
3. Leave **Reader transport** on **Auto**. Choose **T=1** for a TPDU reader when
   starting mid-session, or **APDU** to force ordinary APDU-level interpretation.
   This setting also applies to offline replay; Auto detects each card slot separately.
4. Click **Start capture**, then use your existing smart-card application.
5. Click **Stop** before **Save text**. Selected text can also be copied normally.

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

The display retains recent APDUs up to **20,000 rows / 16 MiB of text**, and text
export saves that displayed portion. The worker queue is bounded by both row
count and text size; if the display cannot keep up
with live APDU traffic, the status bar reports the number of dropped display
entries. USBPcap itself can lose packets when its driver buffer overflows;
this initial version cannot count driver-side losses.

## Initial scope and limitations

- APDU-level **USB CCID** exchanges support both command and response chaining,
  including the empty continuation requests used for long reads.
- **T=1 TPDU** exchanges support LRC/CRC checksums and chained command/response
  I-blocks. R-block acknowledgements, IFS negotiation, and WTX exchanges are
  omitted from the APDU display; identical I-block retransmissions are suppressed.
- Character-level exchanges, T=0 TPDU reconstruction, secure PIN commands,
  vendor escape commands, non-CCID readers, and contactless protocols outside
  CCID are not supported.
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
configuration, streaming, idle-read cancellation, and cleanup on errors.
Worker tests also exercise lossless replay of a record containing 12,000 commands,
cancellation under backpressure, and bounded display memory for long APDUs.

For a manual hardware check, select your reader's controller, start capture,
send a known SELECT using your card application, and confirm its command bytes
and response/status word. Stop while the reader is idle, then start again to
verify cancellation and handle cleanup. Repeat with another slot/reader when
available. Earlier hardware trials confirmed capture on two readers and exposed
the transport gaps addressed here. The updated reassembly still needs a repeat
live check; automated tests use synthetic captures.

## Structure

```text
CMakeLists.txt  Application, libraries, tests, and runtime deployment
src/capture/   Direct Win32 USBPcap driver access and a portable stub
src/core/      Incremental PCAP, USBPcap, and CCID decoding (standard C++ only)
src/ui/        Qt worker, bounded text queue, and main window
tests/         Synthetic protocol regression tests and fixture generation
ui-tests/      Qt asynchronous replay and worker tests
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and validation guidance.

The driver wire format and IOCTLs were checked against the upstream
[USBPcap public header](https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/include/USBPcap.h)
and [USBPcapCMD capture code](https://github.com/desowin/usbpcap/blob/master/USBPcapCMD/thread.c).
CCID framing follows the
[USB-IF CCID specification](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf).
No driver or USBPcapCMD implementation is bundled. Application source uses the
repository's [MIT license](LICENSE); Qt and the separately installed USBPcap retain their
own licenses.
