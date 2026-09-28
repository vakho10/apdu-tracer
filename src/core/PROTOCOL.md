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
`CcidChannel` then reconstructs logical exchanges per bus, device and card slot.
Only complete, well-formed short or extended ISO 7816 command APDUs and responses
with at least two status bytes are emitted. `bBWI` is ignored. Power-on DataBlocks
produce `Direction::Atr`, never an APDU response; the UI hides ATR entries.

## CCID chaining

Command fragments use `wLevelParameter` 1 (first), 3 (middle), and 2 (last), with
empty `bChainParameter == 0x10` acknowledgements between fragments. Response
fragments use `bChainParameter` 1 (first), 3 (middle), and 2 (last), with empty
`wLevelParameter == 0x10` fetches between fragments. Zero denotes a complete
unchained APDU. Each CCID request/reply leg must match `bSeq`; successive legs
may change or reuse it. An emitted response retains the first command's sequence,
and each emitted entry retains its first data fragment's timestamp.

Time-extension responses preserve pending state. Failed CCID commands, USB
failures and state-changing reader operations discard incomplete exchanges.
Read-only GetSlotStatus and GetParameters polls have separately tracked replies
and preserve transport state, unless the reply reports an inactive/removed card.

## T=1 reconstruction and selection

`T1Decoder` accepts complete T=1 blocks carried in unchained CCID XfrBlock/DataBlock
messages. It checks NAD, PCB, LEN and LRC/CRC; joins chained I-blocks; and validates
the reconstructed command's APDU length. Sequence changes and R acknowledgements
are checked. Identical retries do not append or emit data twice. A retransmission
request requires an identical observed retry before data can advance. S-blocks
(IFS, WTX, ABORT and RESYNCH) and R-blocks do not produce APDU entries.

An invalid checksum, inconsistent sequence, missing acknowledgement or other data
gap discards the affected exchange. Decoding resumes after a valid final incoming
I-block or a completed RESYNCH/ABORT boundary. If that boundary is also missing,
an additional exchange may be omitted. Responses require a captured complete
command. A successful RESYNCH resets sequence history; ABORT abandons the exchange
while retaining sequence history. APDU sequence IDs and timestamps follow the
same first-fragment convention as CCID chaining.

`TransportMode` applies to live capture and replay:

- `Apdu` interprets CCID payloads as APDUs and supports CCID chaining.
- `T1` interprets payloads as T=1 blocks, including captures starting mid-session.
- `Auto` begins undecided for each slot. A matched IFS request/reply establishes
  T=1, as does a complete I-block containing a valid command whose whole frame
  cannot be an APDU. An invalid-as-APDU chained I-block followed by its matching
  R acknowledgement also establishes T=1. Raw-valid I-block lookalikes remain
  ordinary APDUs while undecided. An ambiguous IFS request is buffered; if its
  probe fails, is replaced, or reaches capture end, its raw bytes are emitted
  when they form a valid APDU. This can delay that command's display. A lone
  R-block never selects T=1. Power or parameter changes reset inferred mode.

Auto cannot remove every ambiguity without reader descriptors. A valid APDU pair
can coincide with an IFS handshake, while a mid-session T=1 block can coincide
with a valid APDU. The explicit selector resolves these cases.

## Remaining limits

- Bulk CCID is detected heuristically at transfer boundaries. USB descriptors are
  not required, since live capture often begins after device enumeration. A reader
  must use APDU-level or T=1 TPDU-level CCID exchanges. Descriptor parsing and
  character-level exchange are not implemented.
- T=0 TPDU reconstruction, secure PIN commands, vendor escape commands and ICCD
  control transport are not decoded. T=1 frames split across separate CCID
  messages are unsupported; splitting a CCID message across USB transfers works.
- A response captured without its command is omitted, because it cannot safely be
  distinguished from a power-on ATR. Capturing after a command was sent may thus
  miss the first response. One outstanding command per slot is supported.
- Multiple independent CCID interfaces sharing the same device and slot numbers
  are not distinguished. Such composite readers are not supported yet; the UI
  filters by USB device address, not interface.
- At capture end, unfinished CCID messages and pending responses are omitted.
  Packet framing truncation is an error. The decoder does not scan byte by byte
  for apparent CCID headers or attempt to repair corrupt captures.
  Intentional Stop uses `cancel()` to discard a partial last PCAP record and flush
  any complete ambiguous commands without treating cancellation as corruption.
  Final display-queue writes remain bounded and contribute to the dropped-entry
  counter if the queue is full.
- PCAPNG, other USB link types, snapshots over 16 MiB, shortened PCAP records and
  malformed USBPcap lengths are rejected with `std::runtime_error`. Discard the
  decoder after an exception. `finish()` is idempotent; `feed()` after it throws.
  CCID payloads are capped at 65,544 bytes, with at most 256 partial endpoint
  messages and 256 card slots. Joined commands are capped at 65,544 bytes and
  responses at 65,538 bytes. Poll tracking is bounded to the 256 CCID sequences
  per slot. T=1 INF fields are limited to 254 bytes per block.

Protocol references used for this independent implementation:

- [USBPcap capture format](https://desowin.org/usbpcap/captureformat.html)
- [USB-IF CCID 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf),
  sections 3.1.3, 6.1.4, 6.2.1 and 6.2.6
- [CCID driver's T=1 protocol implementation](https://github.com/LudovicRousseau/CCID/blob/master/src/openct/proto-t1.c)
- [CCID driver's checksum wire parameters](https://github.com/LudovicRousseau/CCID/blob/master/src/openct/checksum.c)
- [Wireshark CCID dissector](https://github.com/wireshark/wireshark/blob/master/epan/dissectors/packet-usb-ccid.c)
- [IETF PCAP format description](https://www.ietf.org/archive/id/draft-ietf-opsawg-pcap-04.html)

The synthetic test fixture is deliberately labeled as such; it is not hardware
validation. Generate it with `decoder_tests --write-fixture example.pcap`.
