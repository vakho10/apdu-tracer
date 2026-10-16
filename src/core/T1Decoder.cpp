#include "T1Decoder.h"
#include "ApduFraming.h"

#include <algorithm>
#include <array>
#include <utility>

namespace apdu {
namespace {

using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t maxCommand = 65544;
constexpr std::size_t maxResponse = 65538;

std::uint8_t reverseAddress(std::uint8_t address) {
    return static_cast<std::uint8_t>((address << 4) | (address >> 4));
}

// T=1 uses the reflected 0x1021 polynomial, initial value FFFF, no final
// complement, and transmits the high byte first. This independent bit-at-a-time
// implementation follows the wire parameters used by the upstream CCID driver:
// https://github.com/LudovicRousseau/CCID/blob/master/src/openct/checksum.c
std::uint16_t checksumCrc(Bytes bytes) {
    std::uint16_t remainder = 0xffff;
    for (const auto byte : bytes) {
        remainder ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            remainder = static_cast<std::uint16_t>((remainder >> 1) ^
                ((remainder & 1) != 0 ? 0x8408 : 0));
        }
    }
    return remainder;
}

struct Lane {
    std::optional<std::uint8_t> sequence;
    std::vector<std::uint8_t> lastFrame;
    bool more{};
    bool acknowledged{};
    bool retryRequired{};
};

struct Message {
    std::vector<std::uint8_t> data;
    std::uint64_t timestampUs{};
    std::uint8_t ccidSequence{};
};

} // namespace

std::optional<T1FrameInfo> T1Decoder::inspect(Bytes frame, T1Checksum expected) {
    if (frame.size() < 4 || frame[2] == 255 || (frame[0] & 0x88) != 0) {
        return std::nullopt;
    }
    const auto bodyEnd = std::size_t{3} + frame[2];
    T1Checksum checksum{};
    if (frame.size() == bodyEnd + 1 && expected != T1Checksum::Crc) {
        std::uint8_t value = 0;
        for (const auto byte : frame) {
            value ^= byte;
        }
        if (value != 0) {
            return std::nullopt;
        }
        checksum = T1Checksum::Lrc;
    } else if (frame.size() == bodyEnd + 2 && expected != T1Checksum::Lrc) {
        const auto actual = static_cast<std::uint16_t>((frame[bodyEnd] << 8) | frame[bodyEnd + 1]);
        if (checksumCrc(frame.first(bodyEnd)) != actual) {
            return std::nullopt;
        }
        checksum = T1Checksum::Crc;
    } else {
        return std::nullopt;
    }

    const auto pcb = frame[1];
    T1BlockType type{};
    if ((pcb & 0x80) == 0) {
        if ((pcb & 0x1f) != 0) {
            return std::nullopt;
        }
        type = T1BlockType::I;
    } else if ((pcb & 0xc0) == 0x80) {
        if ((pcb & 0x20) != 0 || (pcb & 0x0f) > 2 || frame[2] != 0) {
            return std::nullopt;
        }
        type = T1BlockType::R;
    } else {
        const auto subtype = pcb & 0x1f;
        if (subtype > 3) {
            return std::nullopt;
        }
        const bool hasValue = subtype == 1 || subtype == 3;
        if (frame[2] != (hasValue ? 1 : 0)) {
            return std::nullopt;
        }
        if (hasValue && (frame[3] == 0 || (subtype == 1 && frame[3] == 255))) {
            return std::nullopt;
        }
        type = T1BlockType::S;
    }
    return T1FrameInfo{type, checksum, frame[0], pcb, frame[2]};
}

struct T1Decoder::Impl {
    T1Checksum configured;
    T1Checksum checksum;
    std::optional<std::uint8_t> hostAddress;
    std::array<Lane, 2> lanes;
    Message command;
    Message response;
    bool awaitingResponse{};
    std::uint8_t commandSequence{};
    bool discardUntilResponseEnd{};
    std::optional<std::uint8_t> resetRequest;
    bool resetRequestIncoming{};

    explicit Impl(T1Checksum desired) : configured(desired), checksum(desired) {}

    void clearExchange() {
        command = {};
        response = {};
        awaitingResponse = false;
    }

    void reset() {
        clearExchange();
        checksum = configured;
        hostAddress.reset();
        lanes = {};
        discardUntilResponseEnd = false;
        resetRequest.reset();
    }

    void damagedExchange() {
        clearExchange();
        // A valid final response establishes a new exchange boundary. Waiting
        // for that boundary avoids interpreting the tail of a broken chain as
        // a fresh APDU. A successful S(RESYNCH)/S(ABORT) exchange also recovers.
        discardUntilResponseEnd = true;
    }

    void remember(Lane& lane, Bytes frame, const T1FrameInfo& info) {
        lane.sequence = static_cast<std::uint8_t>((info.pcb >> 6) & 1);
        lane.lastFrame.assign(frame.begin(), frame.end());
        lane.more = (info.pcb & 0x20) != 0;
        lane.acknowledged = false;
        lane.retryRequired = false;
    }

    std::vector<T1Apdu> feed(bool incoming, Bytes frame, std::uint64_t time, std::uint8_t ccidSeq) {
        const auto info = T1Decoder::inspect(frame, checksum);
        if (!info) {
            damagedExchange();
            return {};
        }
        const auto address = incoming ? reverseAddress(info->nad) : info->nad;
        if (hostAddress && *hostAddress != address) {
            damagedExchange();
            return {};
        }
        hostAddress = address;
        checksum = info->checksum;
        auto& lane = lanes[incoming ? 1 : 0];
        auto& opposite = lanes[incoming ? 0 : 1];

        if (info->type == T1BlockType::S) {
            const auto subtype = static_cast<std::uint8_t>(info->pcb & 0x1f);
            if (subtype == 0 || subtype == 2) {
                clearExchange();
                // RESYNCH resets I-block sequence numbers; ABORT terminates
                // only the APDU, so retain the sequence history for ABORT.
                if (subtype == 0) {
                    lanes = {};
                } else {
                    for (auto& direction : lanes) {
                        direction.more = false;
                        direction.acknowledged = true;
                        direction.retryRequired = false;
                    }
                }
                if ((info->pcb & 0x20) == 0) {
                    resetRequest = subtype;
                    resetRequestIncoming = incoming;
                    discardUntilResponseEnd = true;
                } else if (!resetRequest ||
                           (*resetRequest == subtype && resetRequestIncoming != incoming)) {
                    resetRequest.reset();
                    discardUntilResponseEnd = false;
                }
            }
            // IFS and WTX do not contain application data or change I-block N(S).
            return {};
        }

        if (info->type == T1BlockType::R) {
            if (!opposite.sequence) {
                // An acknowledgement/retry refers to an I-block we have not
                // captured. In particular, a host R-block before any observed
                // response must not let a later response tail appear complete.
                damagedExchange();
                return {};
            }
            const auto expectedSequence = static_cast<std::uint8_t>((info->pcb >> 4) & 1);
            if ((info->pcb & 0x0f) != 0) {
                if (opposite.sequence && expectedSequence == *opposite.sequence) {
                    // The USB capture can contain valid bytes even when the
                    // card received them with a physical transmission error.
                    // Preserve them, but require an identical observed retry.
                    opposite.retryRequired = true;
                    opposite.acknowledged = false;
                } else {
                    damagedExchange();
                }
                return {};
            }
            if (opposite.sequence && expectedSequence != *opposite.sequence) {
                if (opposite.retryRequired) {
                    damagedExchange(); // A requested retry is missing from the capture.
                } else {
                    opposite.acknowledged = true;
                }
            } else if (opposite.sequence) {
                opposite.retryRequired = true;
                opposite.acknowledged = false;
            }
            // N(R) equal to the last N(S) asks for retransmission. Keep the
            // original bytes and suppress its identical retry when it arrives.
            return {};
        }

        const auto sequence = static_cast<std::uint8_t>((info->pcb >> 6) & 1);
        const bool more = (info->pcb & 0x20) != 0;
        if (discardUntilResponseEnd) {
            remember(lane, frame, *info);
            if (incoming && !more && !resetRequest) {
                discardUntilResponseEnd = false;
                lanes = {};
            }
            return {};
        }

        if (lane.sequence && sequence == *lane.sequence) {
            if (std::ranges::equal(lane.lastFrame, frame)) {
                lane.retryRequired = false;
                return {}; // Identical retransmission, including completed APDUs.
            }
            damagedExchange(); // Same N(S), different bytes: corruption or a gap.
            return {};
        }
        if (lane.retryRequired || opposite.retryRequired) {
            damagedExchange(); // Data advanced without the requested retransmission.
            if (incoming && !more) {
                discardUntilResponseEnd = false;
                lanes = {};
            }
            return {};
        }
        if (lane.more && !lane.acknowledged) {
            damagedExchange(); // A continuation requires the opposite R-block.
            if (incoming && !more) {
                discardUntilResponseEnd = false;
                lanes = {};
            }
            return {};
        }
        remember(lane, frame, *info);

        const auto body = frame.subspan(3, info->length);
        if (body.empty()) {
            damagedExchange();
            return {};
        }
        if (!opposite.more) {
            opposite.acknowledged = true; // Opposite I-block implicitly acknowledges final I-block.
        }

        if (!incoming) {
            if (!response.data.empty()) {
                damagedExchange(); // Host I-block inside response chaining is not a new APDU.
                return {};
            }
            if (command.data.empty()) {
                // A fresh complete command can follow a response absent from
                // the capture. Its predecessor must no longer match anything.
                awaitingResponse = false;
                command.timestampUs = time;
                command.ccidSequence = ccidSeq;
            }
            if (body.size() > maxCommand - command.data.size()) {
                damagedExchange();
                return {};
            }
            command.data.insert(command.data.end(), body.begin(), body.end());
            if (more) {
                return {};
            }
            if (!validCommandApdu(command.data)) {
                damagedExchange();
                return {};
            }
            commandSequence = command.ccidSequence;
            awaitingResponse = true;
            T1Apdu result{false, std::move(command.data), command.timestampUs, commandSequence};
            command = {};
            return {std::move(result)};
        }

        if (!awaitingResponse || !command.data.empty()) {
            return {}; // Mid-capture orphan responses have no safe APDU association.
        }
        if (response.data.empty()) {
            response.timestampUs = time;
        }
        if (body.size() > maxResponse - response.data.size()) {
            damagedExchange();
            return {};
        }
        response.data.insert(response.data.end(), body.begin(), body.end());
        if (more) {
            return {};
        }
        if (response.data.size() < 2) {
            damagedExchange();
            return {};
        }
        T1Apdu result{true, std::move(response.data), response.timestampUs, commandSequence};
        response = {};
        awaitingResponse = false;
        return {std::move(result)};
    }
};

T1Decoder::T1Decoder(T1Checksum checksum) : impl_(std::make_unique<Impl>(checksum)) {}
T1Decoder::~T1Decoder() = default;
T1Decoder::T1Decoder(T1Decoder&&) noexcept = default;
T1Decoder& T1Decoder::operator=(T1Decoder&&) noexcept = default;

void T1Decoder::reset() {
    impl_->reset();
}

void T1Decoder::transportGap() {
    impl_->damagedExchange();
}

std::vector<T1Apdu> T1Decoder::feed(bool incoming, Bytes frame, std::uint64_t time, std::uint8_t sequence) {
    return impl_->feed(incoming, frame, time, sequence);
}

} // namespace apdu
