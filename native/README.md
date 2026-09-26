# APDU Tracer Native

An initial Windows desktop tracer built with **C++20, Qt 6 Widgets, and CMake**.
It reads the installed USBPcap driver directly and displays sent (`>>`) and
received (`<<`) APDUs as hexadecimal text. Electron, Node, Wireshark, and tshark
are not needed to build or run this application.

The existing Electron implementation remains at the repository root as a
behavioral reference. This first native version focuses on capture and text;
APDU interpretation, TLV decoding, themes, and installers can follow later.

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
cmake -S native -B native/build/windows -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.1/msvc2022_64"
cmake --build native/build/windows --config Release --parallel
$env:PATH = "C:\Qt\6.8.1\msvc2022_64\bin;" + $env:PATH
ctest --test-dir native/build/windows -C Release --output-on-failure
& .\native\build\windows\Release\APDUTracer.exe
```

Alternatively, open `native/CMakeLists.txt` in Qt Creator and select your MSVC
kit. A DLL/plugin error at startup usually means Qt's `bin` directory is missing
from `PATH`. For a self-contained local application folder:

```powershell
$nativeDist = Join-Path (Get-Location) 'native/dist'
cmake --install native/build/windows --config Release --prefix $nativeDist
& .\native\dist\bin\APDUTracer.exe
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
3. Click **Start capture**, then use your existing smart-card application.
4. Click **Stop** before **Save text**. Selected text can also be copied normally.

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

The display retains recent APDUs up to **20,000 rows / 16 MiB of text**, and text
export saves that displayed portion. The worker queue is bounded by both row
count and text size; if the display cannot keep up
with live APDU traffic, the status bar reports the number of dropped display
entries. USBPcap itself can lose packets when its driver buffer overflows;
this initial version cannot count driver-side losses.

## Initial scope and limitations

- APDU-level **USB CCID** readers using unchained `PC_to_RDR_XfrBlock` and
  `RDR_to_PC_DataBlock` exchanges. Character/TPDU-level T=0/T=1 reconstruction,
  CCID APDU chaining, non-CCID/proprietary readers, and contactless protocols
  outside CCID are not supported.
- CCID framing and APDU lengths are checked conservatively. Reader descriptors
  are not decoded, so the app cannot establish a reader's exchange level or
  distinguish every possible unrelated bulk payload with a CCID-like layout.
- Responses require a captured matching command on the same bus, device, slot,
  and sequence. Starting halfway through an exchange can omit its response.
- Composite devices with independent CCID interfaces reusing the same slot
  numbers are not supported yet; response pairing does not distinguish them.
- Power-on responses are tracked separately as ATRs. Failed CCID commands and
  time-extension notifications are not APDU responses.
- Partial USB transfers are reassembled; invalid lengths, truncated packets,
  and malformed input do not become displayed APDUs.
- **PCAPNG is not supported yet**; save/export captures in classic PCAP format.
  Only USBPcap link type 249 is supported, including when replaying elsewhere.
- Live capture is Windows-only. The decoder and offline Qt viewer are portable.

## Verification

`decoder_tests` constructs synthetic captures to exercise PCAP boundaries,
endianness, USB direction, CCID fragmentation, APDU validation, ATRs, multiple
slots/devices, malformed input, and truncation. It needs no Qt or driver:

```powershell
cmake -S native -B native/build/core -DAPDU_BUILD_GUI=OFF
cmake --build native/build/core --config Release
ctest --test-dir native/build/core -C Release --output-on-failure
```

With the GUI enabled, CTest also generates a synthetic `.pcap` and runs the
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
available. This hardware check has not yet been performed.

## Structure

```text
src/capture/   Direct Win32 USBPcap driver access and a portable stub
src/core/      Incremental PCAP, USBPcap, and CCID decoding (standard C++ only)
src/ui/        Qt worker, bounded text queue, and main window
tests/        Synthetic protocol regression tests and fixture generation
ui-tests/     Qt asynchronous replay smoke test
```

The driver wire format and IOCTLs were checked against the upstream
[USBPcap public header](https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/include/USBPcap.h)
and [USBPcapCMD capture code](https://github.com/desowin/usbpcap/blob/master/USBPcapCMD/thread.c).
CCID framing follows the
[USB-IF CCID specification](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf).
No driver or USBPcapCMD implementation is bundled. Application source uses the
repository's MIT license; Qt and the separately installed USBPcap retain their
own licenses.
