#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace apdu {

enum class Direction { Command, Response, Atr };

struct TraceEntry {
    std::uint64_t timestampUs{};
    std::uint16_t bus{}, device{};
    std::uint8_t endpoint{}, slot{}, sequence{};
    Direction direction{Direction::Command};
    std::vector<std::uint8_t> data;
};

// Incremental classic-PCAP / USBPcap / CCID decoder, independent of Qt.
// Callbacks run synchronously in feed(); the callback must not reenter the decoder.
// Invalid PCAP/USBPcap throws std::runtime_error; discard the decoder after an error.
// See PROTOCOL.md for the intentionally limited APDU-level CCID support.
class TraceDecoder {
public:
    using EntryHandler = std::function<void(const TraceEntry&)>;

    explicit TraceDecoder(EntryHandler handler);
    ~TraceDecoder();
    TraceDecoder(const TraceDecoder&) = delete;
    TraceDecoder& operator=(const TraceDecoder&) = delete;

    void feed(std::span<const std::uint8_t> bytes);
    // Validates that the PCAP stream ended at a record boundary. Unfinished CCID
    // messages and commands still awaiting responses are omitted at capture end.
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace apdu
