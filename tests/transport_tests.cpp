#include "core/TraceDecoder.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Data = std::vector<std::uint8_t>;
using apdu::Direction;
using apdu::TraceEntry;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void put(Data& data, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
        data.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

Data join(Data first, const Data& second) {
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

Data part(const Data& data, std::size_t begin, std::size_t end) {
    return Data(data.begin() + begin, data.begin() + end);
}

struct Location {
    std::uint8_t slot{0};
    std::uint16_t device{5}, bus{1};
};

// Synthetic USBPcap records only. CCID wire fields follow the USB-IF CCID 1.1
// specification, sections 6.1.4 and 6.2.1. No captured card data is included.
struct Capture {
    Data bytes;
    std::uint32_t tick{0};
    Capture() {
        put(bytes, 0xa1b2c3d4, 4);
        put(bytes, 2, 2); put(bytes, 4, 2);
        put(bytes, 0, 4); put(bytes, 0, 4);
        put(bytes, 1024 * 1024, 4); put(bytes, 249, 4);
    }

    std::uint64_t add(bool incoming, std::uint8_t type, const Data& body,
                      std::uint8_t seq, std::uint8_t first, std::uint8_t second,
                      std::uint8_t third, Location location = {}, std::uint32_t usbStatus = 0) {
        Data message{type};
        put(message, body.size(), 4);
        message.insert(message.end(), {location.slot, seq, first, second, third});
        message.insert(message.end(), body.begin(), body.end());
        Data packet;
        put(packet, 27, 2); put(packet, tick + 1, 8); put(packet, usbStatus, 4);
        put(packet, 9, 2); packet.push_back(incoming ? 1 : 0);
        put(packet, location.bus, 2); put(packet, location.device, 2);
        packet.push_back(incoming ? 0x81 : 1); packet.push_back(3);
        put(packet, message.size(), 4);
        packet.insert(packet.end(), message.begin(), message.end());
        put(bytes, 1700000000, 4); put(bytes, ++tick * 1000, 4);
        put(bytes, packet.size(), 4); put(bytes, packet.size(), 4);
        bytes.insert(bytes.end(), packet.begin(), packet.end());
        return 1700000000000000ULL + tick * 1000ULL;
    }

    std::uint64_t command(const Data& body, std::uint8_t seq, std::uint16_t level = 0,
                          Location location = {}) {
        return add(false, 0x6f, body, seq, 0, static_cast<std::uint8_t>(level),
                   static_cast<std::uint8_t>(level >> 8), location);
    }
    std::uint64_t response(const Data& body, std::uint8_t seq, std::uint8_t chain = 0,
                           Location location = {}, std::uint8_t status = 0,
                           std::uint8_t error = 0) {
        return add(true, 0x80, body, seq, status, error, chain, location);
    }
};

const Data command{0x00, 0xca, 0x00, 0x00, 0x08};
const Data success{0x90, 0x00};

std::vector<TraceEntry> decode(const Capture& capture,
                              apdu::TransportMode mode = apdu::TransportMode::Auto,
                              std::size_t chunk = 17) {
    std::vector<TraceEntry> result;
    apdu::TraceDecoder decoder([&](const TraceEntry& entry) { result.push_back(entry); }, mode);
    for (std::size_t i = 0; i < capture.bytes.size(); i += chunk)
        decoder.feed(std::span(capture.bytes).subspan(i, std::min(chunk, capture.bytes.size() - i)));
    decoder.finish();
    return result;
}

void entryIs(const TraceEntry& entry, Direction direction, const Data& data,
             std::uint8_t seq, Location location = {}) {
    require(entry.direction == direction && entry.data == data, "Incorrect reconstructed APDU or direction.");
    require(entry.sequence == seq, "Transport continuation changed the logical command sequence.");
    require(entry.bus == location.bus && entry.device == location.device && entry.slot == location.slot,
            "Transport reconstruction crossed a bus, device, or slot.");
}

void ccidResponseChain() {
    Capture capture;
    capture.command(command, 250);
    const auto responseTime = capture.response({0x11}, 250, 1);
    capture.command({}, 251, 0x10);
    capture.response({0x22, 0x33}, 251, 3);
    capture.command({}, 252, 0x10);
    capture.response({}, 252, 0, {}, 0x80, 2); // Time extension during final fetch.
    capture.response(success, 252, 2);
    const auto entries = decode(capture);
    require(entries.size() == 2, "CCID response chain emitted fragments or lost its response.");
    entryIs(entries[0], Direction::Command, command, 250);
    entryIs(entries[1], Direction::Response, {0x11, 0x22, 0x33, 0x90, 0}, 250);
    require(entries[1].timestampUs == responseTime, "Response did not retain its first fragment timestamp.");
}

void ccidCommandAndResponseChain() {
    Capture capture;
    const Data complete{0, 0xda, 0, 0, 6, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const auto commandTime = capture.command(part(complete, 0, 3), 20, 1);
    capture.response({}, 20, 0, {}, 0x80, 1);
    capture.response({}, 20, 0x10);
    capture.command(part(complete, 3, 7), 21, 3);
    capture.response({}, 21, 0x10);
    capture.command(part(complete, 7, complete.size()), 22, 2);
    const auto responseTime = capture.response({0x42}, 22, 1);
    capture.command({}, 23, 0x10);
    capture.response(success, 23, 2);
    const auto entries = decode(capture, apdu::TransportMode::Auto, 1);
    require(entries.size() == 2, "CCID command chain or its continuation ACK appeared as separate APDUs.");
    entryIs(entries[0], Direction::Command, complete, 20);
    entryIs(entries[1], Direction::Response, {0x42, 0x90, 0}, 20);
    require(entries[0].timestampUs == commandTime && entries[1].timestampUs == responseTime,
            "Chained APDU timestamps must identify the first fragment.");
}

void ccidIsolation() {
    Capture capture;
    const std::vector<Location> locations{{0, 5, 1}, {1, 5, 1}, {0, 6, 1}, {0, 5, 2}};
    for (std::size_t i = 0; i < locations.size(); ++i) {
        capture.command(command, 7, 0, locations[i]);
        capture.response({static_cast<std::uint8_t>(0xa0 + i)}, 7, 1, locations[i]);
    }
    for (const auto& location : locations) {
        capture.command({}, 8, 0x10, location);
        capture.response(success, 8, 2, location);
    }
    const auto entries = decode(capture);
    require(entries.size() == 8, "Interleaved CCID chains lost or duplicated APDUs.");
    for (std::size_t i = 0; i < locations.size(); ++i) {
        entryIs(entries[i], Direction::Command, command, 7, locations[i]);
        entryIs(entries[i + 4], Direction::Response,
                {static_cast<std::uint8_t>(0xa0 + i), 0x90, 0}, 7, locations[i]);
    }
}

void ccidDiscardAndRecovery() {
    Capture capture;
    capture.command({0, 0xda}, 1, 1);
    capture.response({}, 1, 0x10);
    capture.add(false, 0x72, {}, 1, 0, 0, 0); // PC_to_RDR_Abort
    capture.command({0, 0, 0}, 2, 2);
    capture.response(success, 2);
    require(decode(capture).empty(), "An aborted command chain became an APDU.");

    Capture failed;
    failed.command(command, 3);
    failed.response({0x11}, 3, 1);
    failed.command({}, 4, 0x10);
    failed.response({}, 4, 0, {}, 0x40, 0xfe);
    failed.command({}, 5, 0x10);
    failed.response(success, 5, 2);
    require(decode(failed).size() == 1, "Failed response chain survived a CCID error.");

    Capture replaced;
    replaced.command({0, 0xda}, 10, 1);
    replaced.response({}, 10, 0x10);
    replaced.command(command, 11);
    replaced.response(success, 11);
    replaced.command({0, 0, 0}, 12, 2);
    replaced.response(success, 12);
    auto entries = decode(replaced);
    require(entries.size() == 2, "A new command did not discard an unfinished command chain.");
    entryIs(entries[0], Direction::Command, command, 11);
    entryIs(entries[1], Direction::Response, success, 11);

    Capture newResponse;
    newResponse.command(command, 20);
    newResponse.response({0x77}, 20, 1);
    newResponse.command(command, 21);
    newResponse.response(success, 21);
    newResponse.command({}, 22, 0x10);
    newResponse.response(success, 22, 2);
    entries = decode(newResponse);
    require(entries.size() == 3, "A response chain leaked into a later command's response.");
    entryIs(entries.back(), Direction::Response, success, 21);
}

void ccidBoundsAndIncomplete() {
    Capture commandOverflow;
    auto first = Data(40000, 0x42);
    const Data extendedHeader{0, 0xda, 0, 0, 0, 0xff, 0xff};
    std::copy(extendedHeader.begin(), extendedHeader.end(), first.begin());
    // Only aggregate size matters here: neither fragment alone is a complete APDU.
    commandOverflow.command(first, 1, 1);
    commandOverflow.response({}, 1, 0x10);
    commandOverflow.command(Data(30000, 0x43), 2, 2);
    commandOverflow.response(success, 2);
    require(decode(commandOverflow).empty(), "Oversized chained command emitted an APDU.");

    Capture responseOverflow;
    responseOverflow.command(command, 3);
    responseOverflow.response(Data(40000, 0x44), 3, 1);
    responseOverflow.command({}, 4, 0x10);
    responseOverflow.response(Data(30000, 0x45), 4, 2);
    responseOverflow.command(command, 5);
    responseOverflow.response(success, 5);
    const auto entries = decode(responseOverflow);
    require(entries.size() == 3, "Oversized response emitted data or prevented recovery.");
    entryIs(entries.back(), Direction::Response, success, 5);

    Capture incomplete;
    incomplete.command({0, 0xda}, 6, 1);
    incomplete.response({}, 6, 0x10);
    require(decode(incomplete).empty(), "An unfinished command chain emitted a partial APDU.");
    Capture incompleteResponse;
    incompleteResponse.command(command, 7);
    incompleteResponse.response({0x42, 0x43}, 7, 1);
    require(decode(incompleteResponse).size() == 1, "An unfinished response chain emitted a partial APDU.");
}

Data t1(std::uint8_t pcb, const Data& information = {}, bool crc = false) {
    require(information.size() <= 254, "Fixture exceeds the T=1 information field limit.");
    Data frame{0, pcb, static_cast<std::uint8_t>(information.size())};
    frame.insert(frame.end(), information.begin(), information.end());
    if (crc) {
        // ISO 7816 T=1: reflected 0x1021, initial FFFF, no final XOR,
        // high byte first. This is deliberately independent of production code.
        std::uint16_t checksum = 0xffff;
        for (const auto byte : frame) {
            checksum ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                checksum = static_cast<std::uint16_t>((checksum >> 1) ^ ((checksum & 1) ? 0x8408 : 0));
        }
        frame.push_back(static_cast<std::uint8_t>(checksum >> 8));
        frame.push_back(static_cast<std::uint8_t>(checksum));
    } else {
        std::uint8_t checksum = 0;
        for (const auto byte : frame) checksum ^= byte;
        frame.push_back(checksum);
    }
    return frame;
}

void negotiate(Capture& capture, bool crc = false, Location location = {}) {
    capture.command(t1(0xc1, {0xfe}, crc), 1, 0, location);
    capture.response(t1(0xe1, {0xfe}, crc), 1, 0, location);
}

void t1SimpleAndCrc() {
    for (const bool crc : {false, true}) {
        Capture capture;
        negotiate(capture, crc);
        capture.command(t1(0, command, crc), 2);
        capture.response(t1(0, success, crc), 2);
        const auto entries = decode(capture);
        require(entries.size() == 2, "T=1 handshake was displayed or a complete APDU was lost.");
        entryIs(entries[0], Direction::Command, command, 2);
        entryIs(entries[1], Direction::Response, success, 2);
    }
}

void t1ChainedResponse() {
    Capture capture;
    negotiate(capture);
    capture.command(t1(0, command), 40);
    Data first(254);
    for (std::size_t i = 0; i < first.size(); ++i) first[i] = static_cast<std::uint8_t>(i ^ 0x5a);
    const auto responseTime = capture.response(t1(0x20, first), 40);
    capture.command(t1(0x90), 41); // R(1) acknowledges the first chained I(0).
    capture.response({}, 41, 0, {}, 0x80, 1);
    const Data last{0x71, 0x72, 0x73, 0x90, 0};
    capture.response(t1(0x40, last), 41);
    const auto entries = decode(capture);
    require(entries.size() == 2, "T=1 R-block or 254-byte response fragment appeared as an APDU.");
    entryIs(entries[0], Direction::Command, command, 40);
    entryIs(entries[1], Direction::Response, join(first, last), 40);
    require(entries[1].timestampUs == responseTime, "T=1 response lost its initial fragment timestamp.");
}

void t1ChainedCommandAndIsolation() {
    Capture capture;
    negotiate(capture);
    const Data complete{0, 0xda, 0, 0, 6, 1, 2, 3, 4, 5, 6};
    const auto commandTime = capture.command(t1(0x20, part(complete, 0, 3)), 10);
    capture.response(t1(0x90), 10);
    const Location plainSlot{1, 5, 1};
    capture.command(command, 10, 0, plainSlot);
    capture.response(success, 10, 0, plainSlot);
    capture.command(t1(0x40, part(complete, 3, complete.size())), 11);
    capture.response(t1(0, success), 11);
    const auto entries = decode(capture);
    require(entries.size() == 4, "T=1 command chaining leaked mode into another CCID slot.");
    entryIs(entries[0], Direction::Command, command, 10, plainSlot);
    entryIs(entries[1], Direction::Response, success, 10, plainSlot);
    entryIs(entries[2], Direction::Command, complete, 10);
    entryIs(entries[3], Direction::Response, success, 10);
    require(entries[2].timestampUs == commandTime, "T=1 command lost its initial fragment timestamp.");
}

void t1InvalidAndModeOverride() {
    Capture bad;
    negotiate(bad);
    bad.command(t1(0, command), 10);
    auto corrupt = t1(0x20, Data(254, 0x42));
    corrupt.back() ^= 1;
    bad.response(corrupt, 10);
    bad.command(t1(0x90), 11);
    bad.response(t1(0x40, success), 11);
    require(decode(bad).size() == 1, "Bad T=1 checksum became a response or retained a partial exchange.");

    Capture incomplete;
    negotiate(incomplete);
    incomplete.command(t1(0x20, {0, 0xda, 0}), 12);
    incomplete.response(t1(0x90), 12);
    require(decode(incomplete).empty(), "Unfinished T=1 command was emitted.");

    Capture ambiguous;
    const auto request = t1(0xc1, {0xfe}); // Also a syntactically valid five-byte C-APDU.
    const auto reply = t1(0xe1, {0xfe});
    ambiguous.command(request, 13);
    ambiguous.response(reply, 13);
    const auto plain = decode(ambiguous, apdu::TransportMode::Apdu);
    require(plain.size() == 2, "Forced APDU mode suppressed a transport-shaped APDU.");
    entryIs(plain[0], Direction::Command, request, 13);
    entryIs(plain[1], Direction::Response, reply, 13);

    Capture withoutHandshake;
    withoutHandshake.command(t1(0, command), 14);
    withoutHandshake.response(t1(0, success), 14);
    for (const auto mode : {apdu::TransportMode::Auto, apdu::TransportMode::T1}) {
        const auto entries = decode(withoutHandshake, mode);
        require(entries.size() == 2, "A complete unambiguous T=1 exchange required an IFS handshake.");
        entryIs(entries[0], Direction::Command, command, 14);
        entryIs(entries[1], Direction::Response, success, 14);
    }

    // Capture can start after both lanes have already toggled to N(S)=1.
    // Explicit T=1 must not require seeing sequence zero or negotiation first.
    Capture midcapture;
    midcapture.command(t1(0x40, command), 15);
    midcapture.response(t1(0x40, success), 15);
    const auto resumed = decode(midcapture, apdu::TransportMode::T1);
    require(resumed.size() == 2, "Explicit T=1 could not start mid-capture at N(S)=1.");
    entryIs(resumed[0], Direction::Command, command, 15);
    entryIs(resumed[1], Direction::Response, success, 15);
}

void ambiguousIBlocksRemainApdus() {
    // Both payloads happen to be complete T=1 I-blocks with valid LRCs, but
    // the outbound bytes are also a valid raw case-3 command. A matching
    // response shape alone must not silently strip application bytes.
    const Data rawCommand{0, 0, 5, 0, 4, 0, 0, 0, 1};
    const Data rawResponse{0, 0, 2, 0x90, 0, 0x92};
    Capture collision;
    collision.command(rawCommand, 31);
    collision.response(rawResponse, 31);
    const auto entries = decode(collision);
    require(entries.size() == 2, "Auto mode lost a valid APDU that also resembles a T=1 I-block.");
    entryIs(entries[0], Direction::Command, rawCommand, 31);
    entryIs(entries[1], Direction::Response, rawResponse, 31);
}

void readOnlyPolls(Capture& capture, std::uint8_t seq, Location location = {}) {
    capture.add(false, 0x65, {}, seq, 0, 0, 0, location); // GetSlotStatus
    capture.add(true, 0x81, {}, seq, 0, 0, 0, location);  // active card, clock running
    const auto parameterSeq = static_cast<std::uint8_t>(seq + 1);
    capture.add(false, 0x6c, {}, parameterSeq, 0, 0, 0, location); // GetParameters
    capture.add(true, 0x82, {0x11, 0x10, 0, 0x4d, 0, 0xfe, 0},
                parameterSeq, 0, 0, 1, location); // successful T=1 Parameters
}

void readOnlyPollsPreserveTransport() {
    Capture capture;
    negotiate(capture);
    readOnlyPolls(capture, 2);
    const Data complete{0, 0xda, 0, 0, 6, 1, 2, 3, 4, 5, 6};
    const auto commandTime = capture.command(t1(0x20, part(complete, 0, 3)), 4);
    capture.response(t1(0x90), 4);
    // Polls occur between complete CCID command/response pairs, while the
    // logical T=1 command remains unfinished across those pairs.
    readOnlyPolls(capture, 5);
    capture.command(t1(0x40, part(complete, 3, complete.size())), 7);
    const auto responseTime = capture.response(t1(0x20, {0x51, 0x52}), 7);
    readOnlyPolls(capture, 8);
    capture.command(t1(0x90), 10);
    capture.response(t1(0x40, success), 10);
    const auto entries = decode(capture);
    require(entries.size() == 2, "Read-only CCID polling reset inferred T=1 mode or an incomplete chain.");
    entryIs(entries[0], Direction::Command, complete, 4);
    entryIs(entries[1], Direction::Response, {0x51, 0x52, 0x90, 0}, 4);
    require(entries[0].timestampUs == commandTime && entries[1].timestampUs == responseTime,
            "Read-only polling changed logical APDU timestamps.");

    Capture ccid;
    ccid.command(command, 20);
    ccid.response({0x61, 0x62}, 20, 1);
    readOnlyPolls(ccid, 21);
    ccid.command({}, 23, 0x10);
    ccid.response(success, 23, 2);
    const auto ccidEntries = decode(ccid);
    require(ccidEntries.size() == 2, "Read-only polling discarded an incomplete CCID response chain.");
    entryIs(ccidEntries[0], Direction::Command, command, 20);
    entryIs(ccidEntries[1], Direction::Response, {0x61, 0x62, 0x90, 0}, 20);
}

void unresolvedProbePreservesCommand() {
    const auto candidate = t1(0xc1, {0xfe}); // Valid five-byte APDU until a matched IFS reply proves T=1.
    Capture failed;
    const auto failedTime = failed.command(candidate, 40);
    failed.response({}, 40, 0, {}, 0x40, 0xfe);
    auto entries = decode(failed);
    require(entries.size() == 1, "A failed IFS-shaped probe erased a valid raw command APDU.");
    entryIs(entries[0], Direction::Command, candidate, 40);
    require(entries[0].timestampUs == failedTime, "Failed probe fallback changed the command timestamp.");

    Capture replaced;
    replaced.command(candidate, 41);
    replaced.command(command, 42);
    replaced.response(success, 42);
    entries = decode(replaced);
    require(entries.size() == 3, "Replacing an unconfirmed IFS probe erased the previous raw command.");
    entryIs(entries[0], Direction::Command, candidate, 41);
    entryIs(entries[1], Direction::Command, command, 42);
    entryIs(entries[2], Direction::Response, success, 42);

    Capture eof;
    const auto eofTime = eof.command(candidate, 43);
    eof.response({}, 43, 0, {}, 0x80, 1);
    entries = decode(eof);
    require(entries.size() == 1, "End-of-capture erased an unconfirmed raw-valid IFS probe.");
    entryIs(entries[0], Direction::Command, candidate, 43);
    require(entries[0].timestampUs == eofTime, "End-of-capture fallback changed the command timestamp.");
}

void cancellationFlushesOnlyCompleteCommands() {
    const auto candidate = t1(0xc1, {0xfe});
    Capture interrupted;
    const auto commandTime = interrupted.command(candidate, 50);
    interrupted.command(command, 51);
    interrupted.bytes.pop_back(); // The last USB/PCAP record was interrupted by Stop.
    std::vector<TraceEntry> entries;
    apdu::TraceDecoder decoder([&](const TraceEntry& entry) { entries.push_back(entry); });
    decoder.feed(interrupted.bytes);
    require(entries.empty(), "An unconfirmed probe was emitted before cancellation.");
    decoder.cancel();
    require(entries.size() == 1, "Cancellation lost a complete probe or emitted the interrupted record.");
    entryIs(entries[0], Direction::Command, candidate, 50);
    require(entries[0].timestampUs == commandTime, "Cancellation changed the buffered command timestamp.");
    decoder.cancel();
    decoder.finish();
    require(entries.size() == 1, "Repeated cancel/finish duplicated a buffered command.");
    bool feedRejected = false;
    try { decoder.feed({}); }
    catch (const std::logic_error&) { feedRejected = true; }
    require(feedRejected, "feed() accepted more bytes after cancel().");

    // Natural file EOF remains strict even though intentional cancellation is
    // allowed to end anywhere inside a PCAP header or packet.
    apdu::TraceDecoder strict([](const TraceEntry&) {});
    strict.feed(interrupted.bytes);
    bool truncationRejected = false;
    try { strict.finish(); }
    catch (const std::runtime_error&) { truncationRejected = true; }
    require(truncationRejected, "Natural EOF silently accepted an incomplete PCAP record.");

    apdu::TraceDecoder partialHeader([&](const TraceEntry& entry) { entries.push_back(entry); });
    partialHeader.feed(std::span(interrupted.bytes).first(7));
    partialHeader.cancel();
    partialHeader.cancel();
    require(entries.size() == 1, "Cancellation emitted data from an incomplete global header.");

    Capture incompleteCcid;
    incompleteCcid.command({0, 0xda}, 52, 1);
    incompleteCcid.response({}, 52, 0x10);
    Capture incompleteT1;
    negotiate(incompleteT1);
    incompleteT1.command(t1(0x20, {0, 0xda, 0}), 53);
    incompleteT1.response(t1(0x90), 53);
    for (const auto* capture : {&incompleteCcid, &incompleteT1}) {
        std::vector<TraceEntry> partial;
        apdu::TraceDecoder chain([&](const TraceEntry& entry) { partial.push_back(entry); });
        chain.feed(capture->bytes);
        chain.cancel();
        require(partial.empty(), "Cancellation emitted an incomplete transport command chain.");
    }
}

void t1TransportGapsQuarantineCommandTails() {
    const std::vector<const char*> gaps{
        "failed CCID response", "unsupported CCID response chaining", "failed USB completion",
        "replacement before expected response", "mismatched CCID response sequence"
    };
    for (std::size_t gap = 0; gap < gaps.size(); ++gap) {
        try {
            Capture capture;
            negotiate(capture);
            capture.command(t1(0x20, {0, 0xda, 0}), 10);
            switch (gap) {
            case 0:
                capture.response({}, 10, 0, {}, 0x40, 0xfe);
                break;
            case 1:
                capture.response(t1(0x90), 10, 1);
                break;
            case 2:
                capture.add(true, 0x80, {}, 10, 0, 0, 0, {}, 0xc0000001);
                break;
            case 3:
                // The next outgoing block replaces an unanswered CCID leg.
                break;
            case 4:
                capture.response(t1(0x90), 99);
                break;
            }
            // These four continuation bytes also form a valid standalone
            // C-APDU. Resetting transport state at the gap would mislabel them
            // as a fresh command and wrongly pair the next 90 00 response.
            capture.command(t1(0x40, {0, 0x84, 0, 0}), 11);
            capture.response(t1(0, success), 11);
            require(decode(capture).empty(), "A damaged T=1 command tail became a false APDU exchange.");

            // A complete response establishes the end of the damaged exchange.
            // The next complete exchange must work without another IFS pair.
            capture.command(t1(0, command), 12);
            capture.response(t1(0, success), 12);
            const auto entries = decode(capture);
            require(entries.size() == 2, "T=1 did not recover after the damaged exchange boundary.");
            entryIs(entries[0], Direction::Command, command, 12);
            entryIs(entries[1], Direction::Response, success, 12);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(gaps[gap]) + ": " + error.what());
        }
    }
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"CCID response chaining and time extension", ccidResponseChain},
        {"CCID command and response chaining", ccidCommandAndResponseChain},
        {"CCID bus, device and slot isolation", ccidIsolation},
        {"CCID abort, error and replacement discard", ccidDiscardAndRecovery},
        {"CCID aggregate bounds and incomplete chains", ccidBoundsAndIncomplete},
        {"T=1 IFS detection, simple exchange and CRC", t1SimpleAndCrc},
        {"T=1 254-byte response chaining and R-blocks", t1ChainedResponse},
        {"T=1 command chaining and slot isolation", t1ChainedCommandAndIsolation},
        {"T=1 invalid frames, incomplete chains and mode overrides", t1InvalidAndModeOverride},
        {"Auto mode preserves raw APDU and T=1 I-block collisions", ambiguousIBlocksRemainApdus},
        {"Read-only CCID polls preserve transport and partial chains", readOnlyPollsPreserveTransport},
        {"Failed, replaced and unfinished probes preserve raw commands", unresolvedProbePreservesCommand},
        {"Cancellation flushes complete probes and discards partial transport data", cancellationFlushesOnlyCompleteCommands},
        {"Transport gaps quarantine T=1 command tails and recover at exchange boundaries", t1TransportGapsQuarantineCommandTails}
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
