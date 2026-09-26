#include "core/TraceDecoder.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Data = std::vector<std::uint8_t>;
using apdu::Direction;
using apdu::TraceDecoder;
using apdu::TraceEntry;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put(Data& data, std::uint64_t value, std::size_t width, bool little = true) {
    for (std::size_t i = 0; i < width; ++i) {
        const auto shift = 8 * (little ? i : width - i - 1);
        data.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

void set32(Data& data, std::size_t offset, std::uint32_t value, bool little = true) {
    Data field;
    put(field, value, 4, little);
    std::copy(field.begin(), field.end(), data.begin() + offset);
}

Data ccid(std::uint8_t type, const Data& body = {}, std::uint8_t slot = 0,
          std::uint8_t sequence = 7, std::uint8_t first = 0,
          std::uint8_t second = 0, std::uint8_t third = 0) {
    Data result{type};
    put(result, body.size(), 4);
    result.insert(result.end(), {slot, sequence, first, second, third});
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

Data usb(const Data& body, std::uint8_t endpoint = 1, bool completion = false,
         std::uint16_t device = 5, std::uint16_t bus = 1,
         std::uint32_t status = 0, std::uint8_t transfer = 3,
         std::uint64_t irp = 0x0123456789abcdefULL) {
    Data result;
    put(result, 27, 2);
    put(result, irp, 8);
    put(result, status, 4);
    put(result, 9, 2); // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER
    result.push_back(completion ? 1 : 0);
    put(result, bus, 2);
    put(result, device, 2);
    result.insert(result.end(), {endpoint, transfer});
    put(result, body.size(), 4);
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

struct Capture {
    bool little;
    bool nano;
    Data data;
    explicit Capture(bool isLittle = true, bool isNano = false) : little(isLittle), nano(isNano) {
        put(data, nano ? 0xa1b23c4d : 0xa1b2c3d4, 4, little);
        put(data, 2, 2, little);
        put(data, 4, 2, little);
        put(data, 0, 4, little);
        put(data, 0, 4, little);
        put(data, 1024 * 1024, 4, little);
        put(data, 249, 4, little);
    }
    void add(const Data& packet, std::uint32_t seconds = 1700000000,
             std::uint32_t fraction = 123456) {
        put(data, seconds, 4, little);
        put(data, fraction, 4, little);
        put(data, packet.size(), 4, little);
        put(data, packet.size(), 4, little);
        data.insert(data.end(), packet.begin(), packet.end());
    }
};

const Data selectApdu{0x00, 0xa4, 0x04, 0x00, 0x07, 0xa0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
const Data responseApdu{0x90, 0x00};

std::vector<TraceEntry> decode(const Data& data, std::size_t chunk = 4096) {
    std::vector<TraceEntry> entries;
    TraceDecoder decoder([&](const TraceEntry& e) { entries.push_back(e); });
    for (std::size_t offset = 0; offset < data.size(); offset += chunk) {
        decoder.feed(std::span(data).subspan(offset, std::min(chunk, data.size() - offset)));
    }
    decoder.finish();
    return entries;
}

void expectError(const Data& data, const std::string& text) {
    try {
        (void)decode(data, 1);
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find(text) != std::string::npos,
                "Expected error containing '" + text + "', got: " + e.what());
        return;
    }
    throw std::runtime_error("Expected error containing '" + text + "'.");
}

Capture fixture(bool little = true, bool nano = false) {
    Capture capture(little, nano);
    capture.add(usb(ccid(0x6f, selectApdu)), 1700000000, nano ? 123456789 : 123456);
    capture.add(usb(ccid(0x80, responseApdu), 0x81, true), 1700000000, nano ? 124456789 : 124456);
    return capture;
}

void streamingAndByteOrder() {
    for (const bool little : {false, true}) {
        for (const bool nano : {false, true}) {
            const auto capture = fixture(little, nano);
            for (std::size_t split = 0; split <= capture.data.size(); ++split) {
                std::vector<TraceEntry> entries;
                TraceDecoder decoder([&](const TraceEntry& entry) { entries.push_back(entry); });
                decoder.feed(std::span(capture.data).first(split));
                decoder.feed(std::span(capture.data).subspan(split));
                decoder.finish();
                require(entries.size() == 2, "Every split must decode one command and one response.");
                require(entries[0].data == selectApdu && entries[1].data == responseApdu, "Incorrect decoded bytes.");
                require(entries[0].direction == Direction::Command && entries[1].direction == Direction::Response,
                        "Incorrect directions.");
                require(entries[0].timestampUs == 1700000000123456ULL, "Incorrect PCAP timestamp conversion.");
                require(entries[1].timestampUs == 1700000000124456ULL, "Incorrect response timestamp.");
                require(entries[0].bus == 1 && entries[0].device == 5 && entries[0].endpoint == 1 &&
                        entries[0].slot == 0 && entries[0].sequence == 7 && entries[1].endpoint == 0x81,
                        "Incorrect USB/CCID metadata.");
            }
            require(decode(capture.data, 1).size() == 2, "Single-byte streaming failed.");
        }
    }
}

void usbPhasesAndOtherTraffic() {
    Capture capture;
    const auto command = ccid(0x6f, selectApdu);
    const auto response = ccid(0x80, responseApdu);
    capture.add(usb(command, 1, false));
    capture.add(usb(command, 1, true)); // Same IRP payload must not repeat the command.
    capture.add(usb(response, 0x81, false));
    capture.add(usb(response, 0x81, true));
    capture.add(usb(command, 1, false, 5, 1, 0, 1)); // Interrupt
    capture.add(usb(command, 0, false, 5, 1, 0, 2)); // Control
    capture.add(usb(command, 0x81, true)); // Wrong opcode direction
    capture.add(usb(response, 1, false));
    capture.add(usb(Data{0, 0x6f, 0, 0, 0})); // No byte scanning for signatures
    const auto entries = decode(capture.data, 3);
    require(entries.size() == 2, "Wrong USB phases or unrelated traffic produced entries.");
}

void fragmentReassembly() {
    const auto command = ccid(0x6f, selectApdu);
    const auto response = ccid(0x80, Data{0x01, 0x02, 0x90, 0x00});
    for (std::size_t cut = 1; cut < command.size(); ++cut) {
        Capture capture;
        capture.add(usb(Data(command.begin(), command.begin() + cut)), 12, 3);
        capture.add(usb(Data(command.begin() + cut, command.end())), 12, 4);
        for (const auto byte : response) {
            capture.add(usb(Data{byte}, 0x81, true));
        }
        const auto entries = decode(capture.data, 7);
        require(entries.size() == 2 && entries[0].data == selectApdu,
                "CCID split across USB transfers was not reassembled.");
        require(entries[0].timestampUs == 12000003, "Reassembly must retain the first fragment timestamp.");
    }
    Capture combined;
    auto first = ccid(0x6f, selectApdu, 0, 1);
    const auto second = ccid(0x6f, Data{0, 0x84, 0, 0, 8}, 1, 2);
    first.insert(first.end(), second.begin(), second.end());
    combined.add(usb(first));
    require(decode(combined.data).size() == 2, "Multiple CCID messages in one transfer were lost.");
}

void deviceSlotAndSequenceIsolation() {
    Capture capture;
    capture.add(usb(ccid(0x6f, selectApdu, 0, 8), 1, false, 5, 1));
    capture.add(usb(ccid(0x6f, selectApdu, 1, 8), 2, false, 5, 1));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 8), 1, false, 6, 1));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 8), 1, false, 5, 2));
    capture.add(usb(ccid(0x80, responseApdu, 0, 9), 0x81, true, 5, 1)); // Wrong sequence
    capture.add(usb(ccid(0x80, responseApdu, 2, 8), 0x81, true, 5, 1)); // Wrong slot
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true, 9, 1)); // Wrong device
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true, 5, 2));
    capture.add(usb(ccid(0x80, responseApdu, 1, 8), 0x82, true, 5, 1));
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true, 6, 1));
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true, 5, 1));
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true, 5, 1)); // Already consumed
    const auto entries = decode(capture.data);
    require(entries.size() == 8, "CCID slot/sequence pairing crossed devices, slots or buses.");
    require(entries[4].bus == 2 && entries[5].slot == 1 && entries[6].device == 6,
            "Interleaved response identities were lost.");
}

void atrTimeExtensionsAndFailures() {
    Capture capture;
    capture.add(usb(ccid(0x62)));
    capture.add(usb(ccid(0x80, {}, 0, 7, 0x80, 1), 0x81, true));
    capture.add(usb(ccid(0x80, Data{0x3b, 0x00}), 0x81, true));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 8, 2))); // bBWI ignored at APDU exchange level
    capture.add(usb(ccid(0x80, {}, 0, 8, 0x80, 2), 0x81, true));
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 9)));
    capture.add(usb(ccid(0x80, {}, 0, 9, 0x40, 0xfe), 0x81, true));
    capture.add(usb(ccid(0x80, responseApdu, 0, 9), 0x81, true)); // Failed request was consumed
    const auto entries = decode(capture.data);
    require(entries.size() == 4 && entries[0].direction == Direction::Atr,
            "ATR or time extension incorrectly shown as an APDU.");
    require(entries[1].direction == Direction::Command && entries[2].direction == Direction::Response,
            "Time extension lost pending command state.");
}

void unsupportedAndStaleTransactions() {
    Capture capture;
    capture.add(usb(ccid(0x80, responseApdu), 0x81, true)); // Orphan
    capture.add(usb(ccid(0x6f, selectApdu, 0, 7, 0, 1, 0))); // Chained command
    capture.add(usb(ccid(0x80, responseApdu), 0x81, true));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 8)));
    capture.add(usb(ccid(0x80, responseApdu, 0, 8, 0, 0, 1), 0x81, true)); // Chained response
    capture.add(usb(ccid(0x80, responseApdu, 0, 8), 0x81, true));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 9)));
    capture.add(usb(ccid(0x69, {}, 0, 9))); // Secure command replaces pending
    capture.add(usb(ccid(0x80, responseApdu, 0, 9), 0x81, true));
    capture.add(usb(ccid(0x6f, selectApdu, 0, 10)));
    capture.add(usb(ccid(0x63, {}, 0, 11))); // Power off replaces pending
    capture.add(usb(ccid(0x80, responseApdu, 0, 10), 0x81, true));
    require(decode(capture.data).size() == 3, "Unsupported/orphan/stale payload emitted as an APDU.");
}

void apduFraming() {
    const std::vector<Data> valid{
        {0, 0x84, 0, 0}, {0, 0x84, 0, 0, 8}, {0, 0xa4, 0, 0, 1, 0x23},
        {0, 0xa4, 0, 0, 1, 0x23, 0}, {0, 0x84, 0, 0, 0, 1, 0},
        {0, 0xa4, 0, 0, 0, 0, 1, 0x23}, {0, 0xa4, 0, 0, 0, 0, 1, 0x23, 0, 0}
    };
    const std::vector<Data> invalid{
        {}, {0, 0xa4, 0}, {0, 0xa4, 0, 0, 0, 1}, {0, 0xa4, 0, 0, 5, 1},
        {0, 0xa4, 0, 0, 0, 0, 0, 1}, {0, 0xa4, 0, 0, 0, 0, 1, 1, 0}
    };
    for (const auto& body : valid) {
        Capture capture;
        capture.add(usb(ccid(0x6f, body)));
        require(decode(capture.data).size() == 1, "Valid ISO 7816 APDU case suppressed.");
    }
    for (const auto& body : invalid) {
        Capture capture;
        capture.add(usb(ccid(0x6f, body)));
        capture.add(usb(ccid(0x80, responseApdu), 0x81, true));
        require(decode(capture.data).empty(), "Malformed APDU or corresponding response was shown.");
    }
    // Largest extended case-4 APDU: seven header bytes, 65535 data bytes,
    // and two Le bytes. This also exercises record parsing beyond 16-bit lengths.
    Data longest{0, 0xda, 0, 0, 0, 0xff, 0xff};
    longest.resize(65542, 0x42);
    longest.insert(longest.end(), {0, 0});
    Capture maximum;
    maximum.add(usb(ccid(0x6f, longest)));
    const auto entries = decode(maximum.data, 127);
    require(entries.size() == 1 && entries[0].data == longest, "Largest extended APDU was lost or truncated.");
}

void malformedCaptures() {
    expectError({}, "truncated");
    expectError(Data{0x0a, 0x0d, 0x0d, 0x0a}, "PCAPNG");
    expectError(Data(24, 0), "magic");
    auto broken = fixture().data;
    broken[4] = 3;
    expectError(broken, "version");
    broken = fixture().data;
    set32(broken, 20, 1);
    expectError(broken, "link type");
    broken = fixture().data;
    set32(broken, 16, 0);
    expectError(broken, "snapshot");
    broken = fixture().data;
    set32(broken, 16, 0xffffffff);
    expectError(broken, "snapshot");
    broken = fixture().data;
    set32(broken, 28, 1000000);
    expectError(broken, "timestamp");
    broken = fixture().data;
    set32(broken, 32, 0xfffffffe);
    set32(broken, 36, 0xfffffffe);
    expectError(broken, "length");
    broken = fixture().data;
    set32(broken, 36, 0);
    expectError(broken, "length");
    broken = fixture().data;
    set32(broken, 36, 1000);
    expectError(broken, "truncated");
    broken = fixture().data;
    broken[40] = 26;
    expectError(broken, "USBPcap");
    broken = fixture().data;
    set32(broken, 63, 0xfffffffe);
    expectError(broken, "USBPcap");
    broken = fixture().data;
    broken.pop_back();
    expectError(broken, "truncated");
    broken = Capture{}.data;
    broken.push_back(0);
    expectError(broken, "truncated");
}

void boundedCcidAndTransferFailures() {
    Capture capture;
    auto oversized = ccid(0x6f);
    set32(oversized, 1, 0xffffffff);
    capture.add(usb(oversized));
    capture.add(usb(ccid(0x6f, selectApdu)));
    capture.add(usb(ccid(0x80, responseApdu), 0x81, true));
    require(decode(capture.data).size() == 2, "Oversized CCID length prevented recovery.");

    Capture failure;
    const auto command = ccid(0x6f, selectApdu);
    failure.add(usb(Data(command.begin(), command.begin() + 10)));
    failure.add(usb({}, 1, true, 5, 1, 0xc0000001));
    failure.add(usb(Data(command.begin() + 10, command.end())));
    failure.add(usb(ccid(0x80, responseApdu), 0x81, true));
    failure.add(usb(command));
    failure.add(usb(ccid(0x80, responseApdu), 0x81, true, 5, 1, 0xc0000001));
    failure.add(usb(ccid(0x80, responseApdu), 0x81, true));
    require(decode(failure.data).size() == 1, "Fragments or responses were retained across failed USB transfer.");

    Capture incomplete;
    incomplete.add(usb(Data(command.begin(), command.begin() + 9)));
    require(decode(incomplete.data).empty(), "Unfinished CCID message emitted at capture end.");
}

void lifecycle() {
    TraceDecoder decoder([](const TraceEntry&) {});
    decoder.feed(Capture{}.data);
    decoder.finish();
    decoder.finish();
    try {
        decoder.feed({});
    } catch (const std::logic_error&) {
        return;
    }
    throw std::runtime_error("feed() after finish() should be rejected.");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
            const auto capture = fixture();
            std::ofstream file(argv[2], std::ios::binary);
            file.write(reinterpret_cast<const char*>(capture.data.data()), static_cast<std::streamsize>(capture.data.size()));
            require(file.good(), "Could not write synthetic fixture.");
            std::cout << "Wrote synthetic USBPcap fixture: SELECT command and 90 00 response.\n";
            return 0;
        }
        const std::vector<std::pair<const char*, std::function<void()>>> tests{
            {"streaming and PCAP byte order/resolution", streamingAndByteOrder},
            {"USB phases and unrelated traffic", usbPhasesAndOtherTraffic},
            {"USB fragmentation and multiple CCID messages", fragmentReassembly},
            {"bus/device/slot/sequence isolation", deviceSlotAndSequenceIsolation},
            {"ATR, time extensions and CCID errors", atrTimeExtensionsAndFailures},
            {"unsupported and stale transactions", unsupportedAndStaleTransactions},
            {"short and extended APDU framing", apduFraming},
            {"malformed/truncated PCAP and USBPcap", malformedCaptures},
            {"bounded CCID and USB transfer failures", boundedCcidAndTransferFailures},
            {"decoder lifecycle", lifecycle}
        };
        for (const auto& [name, test] : tests) {
            test();
            std::cout << "PASS " << name << '\n';
        }
        std::cout << tests.size() << " test groups passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
