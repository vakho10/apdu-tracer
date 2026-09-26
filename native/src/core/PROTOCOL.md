# Capture decoder contract and scope

`TraceDecoder` consumes arbitrary byte chunks synchronously. It supports classic
PCAP 2.4 in both byte orders, with microsecond or nanosecond timestamps, and
USBPcap link type 249. Timestamps are Unix microseconds (nanoseconds are rounded
down). The pure C++20 library does not need Qt, Wireshark or USBPcapCMD.

USBPcap headers are always little endian. Only bulk OUT submissions and successful
bulk IN completions provide CCID payloads. The other half of each IRP is ignored,
so its data is not counted twice. An OUT row means the host submitted a command;
it is not proof that the card accepted it. A failed USB completion clears the
device's pending transactions and fragment buffers to avoid crossing a data gap.

CCID messages are reassembled by bus, USB device and endpoint using the ten-byte
header's `dwLength`. Complete messages sharing one transfer are also supported.
This is USB-transfer fragmentation, not CCID APDU chaining. Each complete
`PC_to_RDR_XfrBlock` with `wLevelParameter == 0` and a well-formed short or extended
ISO 7816 command length produces a command row. `bBWI` is ignored. A successful,
unchained `RDR_to_PC_DataBlock` produces a response only if it matches the latest
supported command's bus, device, slot and sequence. Time-extension responses keep
that command pending. Failed commands and unsupported commands clear that slot's
pending state. Power-on DataBlocks produce `Direction::Atr`, never a response.

This first version intentionally has these limits:

- Bulk CCID is detected heuristically at transfer boundaries. USB descriptors are
  not required, since live capture often begins after device enumeration. A reader
  must use APDU-level CCID exchanges. The decoder cannot prove the reader's mode
  without its descriptors, and TPDU bytes can occasionally resemble APDUs.
- Character-level exchange, T=0/T=1 TPDU reconstruction, CCID command/response
  chaining, secure PIN commands, vendor escape commands and ICCD control transport
  are not decoded. Nonzero chain parameters are suppressed; no partial APDU is
  presented as a complete response.
- A response captured without its command is omitted, because it cannot safely be
  distinguished from a power-on ATR. Capturing after a command was sent may thus
  miss the first response. One outstanding command per slot is supported.
- Multiple independent CCID interfaces sharing the same device and slot numbers
  are not distinguished. Such composite readers are not supported yet; the UI
  filters by USB device address, not interface.
- At capture end, unfinished CCID messages and pending responses are omitted.
  Packet framing truncation is an error. The decoder does not scan byte by byte
  for apparent CCID headers or attempt to repair corrupt captures.
- PCAPNG, other USB link types, snapshots over 16 MiB, shortened PCAP records and
  malformed USBPcap lengths are rejected with `std::runtime_error`. Discard the
  decoder after an exception. `finish()` is idempotent; `feed()` after it throws.
  CCID payloads are capped at 65,544 bytes, with at most 256 partial endpoint
  messages and 4,096 pending slots.

Protocol references used for this independent implementation:

- [USBPcap capture format](https://desowin.org/usbpcap/captureformat.html)
- [USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf),
  sections 6.1.4, 6.2.1 and 6.2.6
- [Wireshark CCID dissector](https://github.com/wireshark/wireshark/blob/master/epan/dissectors/packet-usb-ccid.c)
- [IETF PCAP format description](https://www.ietf.org/archive/id/draft-ietf-opsawg-pcap-04.html)

The synthetic test fixture is deliberately labeled as such; it is not hardware
validation. Generate it with `decoder_tests --write-fixture example.pcap`.
