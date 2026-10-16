#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace apdu {

enum class Direction { Command, Response, Atr };
enum class TransportMode { Auto, Apdu, T1 };

struct TraceEntry {
    std::uint64_t timestampUs{};
    std::uint16_t bus{}, device{};
    std::uint8_t endpoint{}, slot{}, sequence{};
    Direction direction{Direction::Command};
    std::vector<std::uint8_t> data;
};

// Incremental classic-PCAP / USBPcap / CCID decoder, independent of Qt.
// Callbacks run synchronously in feed(), finish() and cancel(); they must not reenter.
// Invalid PCAP/USBPcap throws std::runtime_error; discard the decoder after an error.
// See PROTOCOL.md for CCID chaining, T=1 reconstruction, and detection limits.
class TraceDecoder {
public:
    using EntryHandler = std::function<void(const TraceEntry&)>;

    explicit TraceDecoder(EntryHandler handler, TransportMode mode = TransportMode::Auto);
    ~TraceDecoder();
    TraceDecoder(const TraceDecoder&) = delete;
    TraceDecoder& operator=(const TraceDecoder&) = delete;

    void feed(std::span<const std::uint8_t> bytes);
    // Validates the final PCAP record boundary and flushes complete ambiguous
    // commands. Incomplete CCID messages, chains and pending replies are omitted.
    void finish();
    // Stop a live/cancelled stream at any byte boundary. Flush complete ambiguous
    // commands, discard partial packets/chains, and reject further feed() calls.
    void cancel();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace apdu
