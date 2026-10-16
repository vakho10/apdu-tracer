#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace apdu {

enum class T1Checksum { Auto, Lrc, Crc };
enum class T1BlockType { I, R, S };

struct T1FrameInfo {
    T1BlockType type{};
    T1Checksum checksum{};
    std::uint8_t nad{}, pcb{}, length{};
};

struct T1Apdu {
    bool incoming{};
    std::vector<std::uint8_t> data;
    std::uint64_t timestampUs{};
    std::uint8_t sequence{};
};

// Passive T=1 transport reconstruction for one CCID slot. Each feed is one
// complete T=1 frame, after CCID framing has been removed. No reader I/O occurs.
// Invalid frames, gaps and unrecoverable errors discard the incomplete exchange;
// valid identical retransmissions after R-block retry requests are supported.
// No partial APDU is emitted. Responses require a complete captured command.
// reset() forgets transport sequence/checksum/address state after a card reset;
// transportGap() preserves the recovery guard when captured data is lost.
class T1Decoder {
public:
    explicit T1Decoder(T1Checksum checksum = T1Checksum::Auto);
    ~T1Decoder();
    T1Decoder(const T1Decoder&) = delete;
    T1Decoder& operator=(const T1Decoder&) = delete;
    T1Decoder(T1Decoder&&) noexcept;
    T1Decoder& operator=(T1Decoder&&) noexcept;

    static std::optional<T1FrameInfo> inspect(std::span<const std::uint8_t> frame,
                                             T1Checksum checksum = T1Checksum::Auto);
    void reset();
    // A lost/failed USB or CCID transfer is not a protocol reset. Keep the
    // observed transport parameters, discard pending bytes, and quarantine
    // continuations until a valid response-end or resync/abort boundary.
    void transportGap();
    std::vector<T1Apdu> feed(bool incoming, std::span<const std::uint8_t> frame,
                             std::uint64_t timestampUs, std::uint8_t ccidSequence);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace apdu
