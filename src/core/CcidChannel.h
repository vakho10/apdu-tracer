#pragma once
#include "TraceDecoder.h"
#include "T1Decoder.h"
#include <array>

namespace apdu {
// One logical card slot. Wire CCID sequence numbers are checked per transfer;
// emitted entries keep the first command's sequence across transport fragments.
class CcidChannel {
public:
    explicit CcidChannel(TransportMode mode = TransportMode::Auto);
    std::vector<TraceEntry> feed(std::span<const std::uint8_t> message, TraceEntry metadata);
    std::vector<TraceEntry> finish();
    std::vector<TraceEntry> transportGap();

private:
    enum class Phase { Idle, Apdu, CommandAck, CommandNext, ResponseNext, Response,
                       Atr, Probe, T1 };
    void clearExchange();
    void resetTransport();
    void discardDamagedExchange();
    void emitProbeFallback(std::vector<TraceEntry>& out) const;
    void emitT1(bool incoming, std::span<const std::uint8_t> bytes,
                const TraceEntry& metadata, std::vector<TraceEntry>& out);
    bool matchesProbe(std::span<const std::uint8_t> bytes) const;

    TransportMode configured_;
    TransportMode mode_;
    Phase phase_ = Phase::Idle;
    std::uint8_t wireSequence_ = 0;
    TraceEntry command_, response_;
    std::array<std::uint8_t, 256> polls_{};
    T1Decoder t1_;
};
} // namespace apdu
