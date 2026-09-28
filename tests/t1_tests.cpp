#include "core/T1Decoder.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Data = std::vector<std::uint8_t>;
using apdu::T1Apdu;
using apdu::T1BlockType;
using apdu::T1Checksum;
using apdu::T1Decoder;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint16_t reverseBits(std::uint16_t value, unsigned width) {
    std::uint16_t result = 0;
    for (unsigned i = 0; i < width; ++i) {
        result = static_cast<std::uint16_t>((result << 1) | (value & 1));
        value >>= 1;
    }
    return result;
}

Data frame(std::uint8_t pcb, const Data& body = {}, std::uint8_t nad = 0,
           T1Checksum checksum = T1Checksum::Lrc) {
    require(body.size() <= 255, "Invalid test fixture body size.");
    Data result{nad, pcb, static_cast<std::uint8_t>(body.size())};
    result.insert(result.end(), body.begin(), body.end());
    if (checksum == T1Checksum::Crc) {
        // Independent MSB-first implementation with reflected input/output.
        // Production uses the reflected, LSB-first recurrence instead.
        std::uint16_t crc = 0xffff;
        for (const auto byte : result) {
            crc ^= static_cast<std::uint16_t>(reverseBits(byte, 8) << 8);
            for (unsigned bit = 0; bit < 8; ++bit) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
            }
        }
        crc = reverseBits(crc, 16);
        result.push_back(static_cast<std::uint8_t>(crc >> 8));
        result.push_back(static_cast<std::uint8_t>(crc));
    } else {
        std::uint8_t lrc = 0;
        for (const auto byte : result) lrc ^= byte;
        result.push_back(lrc);
    }
    return result;
}

const Data command{0, 0xa4, 4, 0, 2, 0x12, 0x34};
const Data success{0x90, 0};

struct Session {
    T1Decoder decoder;
    std::vector<T1Apdu> entries;
    std::uint64_t time{1000};
    std::uint8_t ccidSequence{17};
    T1Checksum checksum{T1Checksum::Lrc};
    std::uint8_t hostAddress{};

    explicit Session(T1Checksum type = T1Checksum::Lrc, std::uint8_t nad = 0)
        : checksum(type), hostAddress(nad) {}

    void raw(bool incoming, const Data& bytes) {
        auto result = decoder.feed(incoming, bytes, time++, ccidSequence++);
        entries.insert(entries.end(), result.begin(), result.end());
    }
    void send(bool incoming, std::uint8_t pcb, const Data& body = {}) {
        const auto nad = incoming
            ? static_cast<std::uint8_t>((hostAddress << 4) | (hostAddress >> 4))
            : hostAddress;
        raw(incoming, frame(pcb, body, nad, checksum));
    }
    void handshake() {
        send(false, 0xc1, {0xfe});
        send(true, 0xe1, {0xfe});
    }
    void chain(bool incoming, const Data& data, std::uint8_t firstSequence = 0,
               bool retransmit = false) {
        std::size_t offset = 0;
        auto sequence = firstSequence;
        while (offset < data.size()) {
            const auto length = std::min<std::size_t>(254, data.size() - offset);
            const bool more = offset + length < data.size();
            const auto pcb = static_cast<std::uint8_t>((sequence << 6) | (more ? 0x20 : 0));
            const Data block(data.begin() + offset, data.begin() + offset + length);
            send(incoming, pcb, block);
            if (retransmit) send(incoming, pcb, block);
            if (more) {
                send(!incoming, static_cast<std::uint8_t>(0x80 | ((sequence ^ 1) << 4)));
            }
            offset += length;
            sequence ^= 1;
        }
    }
};

Data extendedCommand(std::size_t payloadLength) {
    require(payloadLength > 0 && payloadLength <= 65535, "Invalid extended APDU fixture size.");
    Data result{0, 0xda, 0, 0, 0,
                static_cast<std::uint8_t>(payloadLength >> 8),
                static_cast<std::uint8_t>(payloadLength)};
    for (std::size_t i = 0; i < payloadLength; ++i) result.push_back(static_cast<std::uint8_t>(i));
    result.insert(result.end(), {0, 0});
    return result;
}

void inspectKnownFrames() {
    const auto request = T1Decoder::inspect(Data{0, 0xc1, 1, 0xfe, 0x3e});
    const auto response = T1Decoder::inspect(Data{0, 0xe1, 1, 0xfe, 0x1e});
    const auto ack = T1Decoder::inspect(Data{0, 0x90, 0, 0x90});
    require(request && request->type == T1BlockType::S && request->checksum == T1Checksum::Lrc,
            "Known IFS request did not validate.");
    require(response && response->pcb == 0xe1 && response->length == 1, "Known IFS response did not validate.");
    require(ack && ack->type == T1BlockType::R, "Known R-block did not validate.");
    // Fixed CRC wire vectors independently checked against upstream CCID's
    // table-based checksum, including its high-byte-first wire convention.
    const Data crcRequest{0, 0xc1, 1, 0xfe, 0x54, 0x4e};
    const Data crcResponse{0, 0xe1, 1, 0xfe, 0x57, 0x75};
    const Data crcData{0, 0, 2, 0x90, 0, 0x9c, 0x6d};
    require(frame(0xc1, {0xfe}, 0, T1Checksum::Crc) == crcRequest, "Test CRC generator disagrees with known vector.");
    for (const auto& data : {crcRequest, crcResponse, crcData}) {
        const auto parsed = T1Decoder::inspect(data);
        require(parsed && parsed->checksum == T1Checksum::Crc, "Known CRC frame did not validate.");
        require(!T1Decoder::inspect(data, T1Checksum::Lrc), "Explicit LRC accepted a CRC frame.");
    }
    require(!T1Decoder::inspect(Data{0, 0xc1, 1, 0xfe, 0x3e}, T1Checksum::Crc),
            "Explicit CRC accepted an LRC frame.");
}

void malformedFrames() {
    const std::vector<Data> malformed{
        {}, {0}, {0, 0, 0}, {0, 0xc1, 1, 0xfe, 0x3f}, // Short/bad LRC
        {0, 0xc1, 1, 0xfe, 0x4e, 0x54}, // Reversed CRC wire bytes
        frame(0, Data(255, 0)), frame(0, success, 0x08), frame(0, success, 0x80),
        frame(1, command), frame(0x81, {0}), frame(0xa0), frame(0x83),
        frame(0xc0, {0}), frame(0xc1), frame(0xc1, {0}), frame(0xc1, {255}),
        frame(0xc2, {0}), frame(0xc3), frame(0xc3, {0}), frame(0xc4)
    };
    for (const auto& data : malformed) require(!T1Decoder::inspect(data), "Malformed T=1 frame accepted.");
    auto trailing = frame(0, command);
    trailing.push_back(0);
    require(!T1Decoder::inspect(trailing), "Trailing data accepted as a T=1 frame.");
    require(T1Decoder::inspect(frame(0, Data(254, 0))).has_value(), "254-byte frame was rejected.");
}

void handshakeAndSimpleExchange() {
    for (const auto checksum : {T1Checksum::Lrc, T1Checksum::Crc}) {
        Session session(checksum);
        session.handshake();
        require(session.entries.empty(), "IFS control frames appeared as APDUs.");
        session.send(false, 0, command);
        session.send(true, 0xc3, {2});
        session.send(false, 0xe3, {2});
        session.send(true, 0, success);
        require(session.entries.size() == 2, "Simple T=1 exchange or WTX handling failed.");
        require(!session.entries[0].incoming && session.entries[0].data == command,
                "Command TPDU envelope was not stripped.");
        require(session.entries[1].incoming && session.entries[1].data == success,
                "Response TPDU envelope was not stripped.");
        require(session.entries[0].sequence == 19 && session.entries[1].sequence == 19,
                "Response did not retain the APDU command's CCID sequence.");
        require(session.entries[0].timestampUs == 1002 && session.entries[1].timestampUs == 1005,
                "APDU timestamps did not use first data fragments.");
        session.send(false, 0x40, command);
        session.send(true, 0x40, success);
        require(session.entries.size() == 4, "Sequence state failed across consecutive exchanges.");
    }
}

void bothDirectionChaining() {
    for (const auto checksum : {T1Checksum::Lrc, T1Checksum::Crc}) {
        Session session(checksum);
        const auto longCommand = extendedCommand(700);
        Data longResponse(1024, 0x5a);
        longResponse.insert(longResponse.end(), success.begin(), success.end());
        session.chain(false, longCommand);
        require(session.entries.size() == 1 && session.entries[0].data == longCommand,
                "Chained command was incomplete or duplicated.");
        const auto responseStart = session.time;
        session.chain(true, longResponse);
        require(session.entries.size() == 2 && session.entries[1].data == longResponse,
                "254-byte T=1 response chain was not reconstructed.");
        require(session.entries[0].timestampUs == 1000 && session.entries[1].timestampUs == responseStart,
                "Chain timestamp changed after acknowledgements.");
        require(session.entries[0].sequence == 17 && session.entries[1].sequence == 17,
                "T=1 acknowledgements overwrote APDU sequence metadata.");
    }
}

void retransmissions() {
    Session session;
    const auto longCommand = extendedCommand(300);
    Data longResponse(500, 0x31);
    longResponse.insert(longResponse.end(), success.begin(), success.end());
    session.chain(false, longCommand, 0, true);
    session.chain(true, longResponse, 0, true);
    require(session.entries.size() == 2 && session.entries[0].data == longCommand &&
            session.entries[1].data == longResponse, "Duplicate I-block retransmissions changed APDU bytes.");

    Session requestRetry;
    requestRetry.send(false, 0, command);
    requestRetry.send(true, 0x80); // Requests retransmission of N(S)=0.
    requestRetry.send(false, 0, command);
    requestRetry.send(true, 0, success);
    requestRetry.send(true, 0, success); // Duplicate final response.
    require(requestRetry.entries.size() == 2, "R-block retry or final response was counted twice.");
}

void gapsAndConflictingRetransmissions() {
    Session gap;
    gap.send(false, 0, command);
    gap.send(true, 0x20, Data(254, 0x22));
    // No host R-block: accepting this continuation would hide a capture gap.
    gap.send(true, 0x40, success);
    require(gap.entries.size() == 1, "Unacknowledged response continuation was emitted.");
    gap.send(false, 0x40, command);
    gap.send(true, 0, success);
    require(gap.entries.size() == 3, "Decoder failed to recover after observing a response boundary.");

    Session changed;
    changed.send(false, 0, command);
    changed.send(true, 0x20, Data(254, 0x22));
    changed.send(false, 0x90);
    changed.send(true, 0x20, Data(254, 0x23)); // Same N(S), different bytes.
    changed.send(true, 0x40, success);
    require(changed.entries.size() == 1, "Conflicting retransmission produced a response.");
}

void transportErrorsAndRecovery() {
    for (const auto errorPcb : Data{0x81, 0x82, 0x91, 0x92}) {
        Session session;
        session.send(false, 0, command);
        session.send(true, errorPcb);
        session.send(false, 0, command);
        session.send(true, 0, success);
        const std::size_t firstCount = (errorPcb & 0x10) == 0 ? 2 : 1;
        require(session.entries.size() == firstCount,
                "Correctly requested retry was lost or inconsistent error was falsely completed.");
        session.send(false, 0x40, command);
        session.send(true, 0x40, success);
        require(session.entries.size() == firstCount + 2, "Clean transaction after error recovery was lost.");
    }
    Session broken;
    broken.send(false, 0, command);
    auto bad = frame(0x20, Data(254, 0x44));
    bad.back() ^= 1;
    broken.raw(true, bad);
    broken.send(false, 0x90);
    broken.send(true, 0x40, success);
    require(broken.entries.size() == 1, "Checksum failure was stitched into a complete response.");

    for (const auto retryPcb : Data{0x80, 0x81}) {
        Session missingRetry;
        missingRetry.send(false, 0, command);
        missingRetry.send(true, retryPcb);
        missingRetry.send(true, 0, success); // No requested host I-block retry was observed.
        require(missingRetry.entries.size() == 1, "Missing requested retry was silently accepted.");
    }

    Session responseRetry;
    responseRetry.send(false, 0, command);
    responseRetry.send(true, 0x20, Data(254, 0x44));
    responseRetry.send(false, 0x81);
    responseRetry.send(true, 0x20, Data(254, 0x44));
    responseRetry.send(false, 0x90);
    responseRetry.send(true, 0x40, success);
    require(responseRetry.entries.size() == 2 && responseRetry.entries[1].data.size() == 256,
            "Response retransmission after an R-error duplicated or lost bytes.");
}

void externallyReportedGaps() {
    Session commandGap;
    commandGap.send(false, 0x20, {0, 0xda, 0, 0, 8, 1, 2, 3, 4});
    commandGap.decoder.transportGap();
    // These four bytes are the final data bytes of the interrupted command,
    // but are also a syntactically valid APDU in isolation.
    commandGap.send(false, 0x40, {0, 0x84, 0, 0});
    commandGap.send(true, 0, success);
    require(commandGap.entries.empty(), "An APDU-looking tail escaped transport-gap quarantine.");
    commandGap.send(false, 0, command);
    commandGap.send(true, 0x40, success);
    require(commandGap.entries.size() == 2, "Decoder failed to recover after a gap's response boundary.");

    Session responseGap;
    responseGap.send(false, 0, command);
    responseGap.send(true, 0x20, Data(254, 0x11));
    responseGap.decoder.transportGap();
    responseGap.send(false, 0x90);
    responseGap.send(true, 0x40, success);
    require(responseGap.entries.size() == 1, "An incomplete response escaped transport-gap quarantine.");

    Session unseenResponseStart;
    unseenResponseStart.send(false, 0, command);
    // First response I-block was not delivered to the decoder. The host's
    // acknowledgement is positive evidence that this capture has a gap.
    unseenResponseStart.send(false, 0x90);
    unseenResponseStart.send(true, 0x40, success);
    require(unseenResponseStart.entries.size() == 1,
            "A response tail was emitted after acknowledgment of an unseen first I-block.");

    Session unseenCommandStart;
    unseenCommandStart.send(true, 0x90);
    unseenCommandStart.send(false, 0x40, {0, 0x84, 0, 0});
    unseenCommandStart.send(true, 0, success);
    require(unseenCommandStart.entries.empty(), "Unseen command start was mistaken for a new command.");
}

void resetAndAbort() {
    for (const auto type : Data{0, 2}) {
        Session session;
        session.send(false, 0, command);
        session.send(true, 0x20, Data(254, 0x55));
        session.send(false, static_cast<std::uint8_t>(0xc0 | type));
        session.send(true, static_cast<std::uint8_t>(0xe0 | type));
        require(session.entries.size() == 1, "S reset/abort appeared as an APDU.");
        // RESYNCH restarts N(S), ABORT retains the existing sequence history.
        const std::uint8_t next = type == 0 ? 0 : 0x40;
        session.send(false, next, command);
        session.send(true, next, success);
        require(session.entries.size() == 3 && session.entries.back().data == success,
                "S reset/abort did not discard partial data and allow recovery.");
    }
    Session explicitReset;
    explicitReset.send(false, 0x20, Data(command.begin(), command.begin() + 3));
    explicitReset.decoder.reset();
    explicitReset.send(false, 0, command);
    explicitReset.send(true, 0, success);
    require(explicitReset.entries.size() == 2, "Explicit reset retained partial command bytes.");
}

void addressAndChecksumIsolation() {
    Session addressed(T1Checksum::Lrc, 0x12);
    addressed.handshake();
    addressed.send(false, 0, command);
    addressed.send(true, 0, success);
    require(addressed.entries.size() == 2, "Opposite-direction NAD nibbles were not reversed.");

    Session wrongAddress(T1Checksum::Lrc, 0x12);
    wrongAddress.send(false, 0, command);
    wrongAddress.raw(true, frame(0, success, 0x12)); // Must be 0x21 on the return path.
    require(wrongAddress.entries.size() == 1, "Response from a wrong NAD was matched.");

    Session switchChecksum;
    switchChecksum.send(false, 0, command);
    switchChecksum.raw(true, frame(0, success, 0, T1Checksum::Crc));
    require(switchChecksum.entries.size() == 1, "Checksum scheme silently changed in an exchange.");
    switchChecksum.decoder.reset();
    switchChecksum.checksum = T1Checksum::Crc;
    switchChecksum.send(false, 0, command);
    switchChecksum.send(true, 0, success);
    require(switchChecksum.entries.size() == 3, "Reset failed to clear inferred checksum scheme.");
}

void incompleteAndInvalidApdus() {
    Session orphan;
    orphan.chain(true, Data(600, 0x33));
    require(orphan.entries.empty(), "Orphan response was shown without a captured command.");
    Session incomplete;
    incomplete.send(false, 0x20, Data(command.begin(), command.begin() + 3));
    require(incomplete.entries.empty(), "Incomplete command was emitted.");
    incomplete.send(true, 0, success);
    require(incomplete.entries.empty(), "Response to an incomplete command was emitted.");
    for (const Data invalid : {Data{0}, Data{0, 0xa4, 0}, Data{0, 0xa4, 0, 0, 5, 1}}) {
        Session badApdu;
        badApdu.send(false, 0, invalid);
        badApdu.send(true, 0, success);
        require(badApdu.entries.empty(), "Malformed command APDU was emitted.");
    }
}

void boundedMessages() {
    Session maximum;
    const auto maxCommand = extendedCommand(65535);
    maximum.chain(false, maxCommand);
    Data maxResponse(65536, 0x42);
    maxResponse.insert(maxResponse.end(), success.begin(), success.end());
    maximum.chain(true, maxResponse);
    require(maximum.entries.size() == 2 && maximum.entries[0].data.size() == 65544 &&
            maximum.entries[1].data.size() == 65538, "Maximum-size APDU was truncated.");

    Session tooLargeCommand;
    tooLargeCommand.chain(false, Data(65545, 0x11));
    tooLargeCommand.send(true, 0, success);
    require(tooLargeCommand.entries.empty(), "Oversized command was emitted.");
    Session tooLargeResponse;
    tooLargeResponse.send(false, 0, command);
    tooLargeResponse.chain(true, Data(65539, 0x22));
    require(tooLargeResponse.entries.size() == 1, "Oversized response was emitted.");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"known LRC and CRC frames", inspectKnownFrames},
        {"malformed transport frames", malformedFrames},
        {"IFS/WTX and simple exchanges", handshakeAndSimpleExchange},
        {"command and response chaining", bothDirectionChaining},
        {"duplicate I-block retransmissions", retransmissions},
        {"gaps and conflicting retries", gapsAndConflictingRetransmissions},
        {"transport errors and recovery", transportErrorsAndRecovery},
        {"externally reported gaps and unseen I-blocks", externallyReportedGaps},
        {"resynchronization, abort and reset", resetAndAbort},
        {"address and checksum isolation", addressAndChecksumIsolation},
        {"orphan, incomplete and invalid APDUs", incompleteAndInvalidApdus},
        {"bounded maximum-size messages", boundedMessages}
    };
    try {
        for (const auto& [name, test] : tests) {
            test();
            std::cout << "PASS " << name << '\n';
        }
        std::cout << tests.size() << " T=1 test groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
