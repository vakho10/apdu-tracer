#include "CcidChannel.h"
#include "ApduFraming.h"

#include <algorithm>

namespace apdu {
namespace {
using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t maxCommand = 65544;
constexpr std::size_t maxResponse = 65538;

bool append(std::vector<std::uint8_t>& to, Bytes bytes, std::size_t maximum) {
    if (bytes.size() > maximum - to.size()) return false;
    to.insert(to.end(), bytes.begin(), bytes.end());
    return true;
}

// Reply type of a command that neither changes card state nor ends an
// exchange, or zero for any other command.
std::uint8_t sideCommandReply(std::uint8_t type) {
    switch (type) {
    case 0x6b: return 0x83; // Escape -> RDR_to_PC_Escape
    case 0x6a: return 0x81; // T0APDU -> SlotStatus
    case 0x6e: return 0x81; // IccClock -> SlotStatus
    default: return 0;
    }
}
} // namespace

CcidChannel::CcidChannel(TransportMode mode) : configured_(mode), mode_(mode) {}

void CcidChannel::clearExchange() {
    phase_ = Phase::Idle;
    command_ = {};
    response_ = {};
}

void CcidChannel::resetTransport() {
    clearExchange();
    mode_ = configured_;
    t1_.reset();
    polls_ = {};
}

void CcidChannel::emitProbeFallback(std::vector<TraceEntry>& out) const {
    if (phase_ == Phase::Probe && validCommandApdu(command_.data))
        out.push_back(command_);
}

std::vector<TraceEntry> CcidChannel::finish() {
    std::vector<TraceEntry> out;
    emitProbeFallback(out);
    resetTransport();
    return out;
}

void CcidChannel::discardDamagedExchange() {
    clearExchange();
    polls_ = {};
    // Keep the known transport and its recovery guard. Starting a fresh T=1
    // decoder here could promote an APDU-shaped chain tail into a new command.
    t1_.transportGap();
}

std::vector<TraceEntry> CcidChannel::transportGap() {
    std::vector<TraceEntry> out;
    emitProbeFallback(out);
    discardDamagedExchange();
    return out;
}

void CcidChannel::emitT1(bool incoming, Bytes bytes, const TraceEntry& metadata,
                         std::vector<TraceEntry>& out) {
    for (auto& apdu : t1_.feed(incoming, bytes, metadata.timestampUs, metadata.sequence)) {
        auto entry = metadata;
        entry.direction = apdu.incoming ? Direction::Response : Direction::Command;
        entry.timestampUs = apdu.timestampUs;
        entry.sequence = apdu.sequence;
        entry.data = std::move(apdu.data);
        out.push_back(std::move(entry));
    }
}

bool CcidChannel::matchesProbe(Bytes bytes) const {
    const auto request = T1Decoder::inspect(command_.data);
    const auto response = T1Decoder::inspect(bytes);
    if (!request || !response || request->checksum != response->checksum ||
        response->nad != static_cast<std::uint8_t>((request->nad << 4) | (request->nad >> 4)))
        return false;
    if (request->type == T1BlockType::S) {
        // Infer from a matched IFS negotiation, not arbitrary control bytes.
        return request->pcb == 0xc1 && response->pcb == 0xe1 &&
               response->length == request->length &&
               std::equal(command_.data.begin() + 3, command_.data.begin() + 3 + request->length,
                          bytes.begin() + 3);
    }
    if (request->type == T1BlockType::I && (request->pcb & 0x20) != 0)
        return response->type == T1BlockType::R && (response->pcb & 0x0f) == 0 &&
               ((response->pcb >> 4) & 1) == (((request->pcb >> 6) & 1) ^ 1);
    return false;
}

std::vector<TraceEntry> CcidChannel::feed(Bytes message, TraceEntry metadata) {
    std::vector<TraceEntry> out;
    if (message.size() < 10) return out;
    const bool incoming = (metadata.endpoint & 0x80) != 0;
    const auto body = message.subspan(10);
    const auto sequence = message[6];
    metadata.slot = message[5];
    metadata.sequence = sequence;
    metadata.direction = incoming ? Direction::Response : Direction::Command;

    if (!incoming) {
        const auto level = static_cast<unsigned>(message[8]) | (static_cast<unsigned>(message[9]) << 8);
        if ((message[0] == 0x65 || message[0] == 0x6c) && body.empty() &&
            message[7] == 0 && level == 0) {
            // Read-only polls may occur between transport fragments. Their
            // replies must not consume the pending XfrBlock, even on SLOT_BUSY.
            polls_[sequence] = message[0] == 0x65 ? 0x81 : 0x82;
            return out;
        }
        if (const auto reply = sideCommandReply(message[0])) {
            // Escape, T0APDU and IccClock leave the card's protocol state
            // intact. Track only their reply so it cannot end an exchange.
            polls_[sequence] = reply;
            return out;
        }
        emitProbeFallback(out);
        polls_[sequence] = 0;
        if (message[0] == 0x69) {
            // A PIN pad sends its own APDU/T=1 block, which advances the T=1
            // N(S) of both sides unseen. Keep the transport mode, forget the
            // sequence history, and consume the reply and its time extensions.
            clearExchange();
            t1_.reset();
            phase_ = Phase::Secure;
            wireSequence_ = sequence;
            return out;
        }
        if (message[0] == 0x62) {
            resetTransport();
            if (body.empty() && message[7] <= 3 && level == 0) {
                phase_ = Phase::Atr;
                wireSequence_ = sequence;
            }
            return out;
        }
        if (message[0] != 0x6f) {
            // Power, parameter, clock rate, mechanical, abort and unknown
            // operations can change the card/protocol state or abandon an
            // exchange. Do not reuse an inferred mode after them.
            resetTransport();
            return out;
        }
        if (phase_ == Phase::T1) discardDamagedExchange(); // Missing CCID reply.
        if (level == 0x10) {
            if (mode_ != TransportMode::T1 && phase_ == Phase::ResponseNext && body.empty()) {
                phase_ = Phase::Response;
                wireSequence_ = sequence;
            } else {
                discardDamagedExchange();
            }
            return out;
        }
        if (level == 2 || level == 3) {
            if (mode_ == TransportMode::T1 || phase_ != Phase::CommandNext || body.empty() ||
                !append(command_.data, body, maxCommand)) {
                discardDamagedExchange();
                return out;
            }
            wireSequence_ = sequence;
            phase_ = level == 2 ? Phase::Apdu : Phase::CommandAck;
            if (level == 2) {
                if (validCommandApdu(command_.data)) out.push_back(command_);
                else clearExchange();
            }
            return out;
        }

        clearExchange();
        wireSequence_ = sequence;
        if (level == 1 && mode_ != TransportMode::T1 && !body.empty() && body.size() <= maxCommand) {
            command_ = metadata;
            command_.data.assign(body.begin(), body.end());
            phase_ = Phase::CommandAck;
            t1_.reset();
            return out;
        }
        if (level != 0) { discardDamagedExchange(); return out; }
        if (mode_ == TransportMode::T1) {
            phase_ = Phase::T1;
            emitT1(false, body, metadata, out);
            return out;
        }
        if (mode_ == TransportMode::Auto) {
            if (const auto frame = T1Decoder::inspect(body)) {
                // A valid complete I-block containing an APDU, whose entire
                // bytes are not also an APDU, is unambiguous enough to start.
                if (frame->type == T1BlockType::I && (frame->pcb & 0x20) == 0 &&
                    !validCommandApdu(body) && validCommandApdu(body.subspan(3, frame->length))) {
                    mode_ = TransportMode::T1;
                    phase_ = Phase::T1;
                    emitT1(false, body, metadata, out);
                    return out;
                }
                // A raw-valid I-block may be an ordinary APDU even if its
                // response resembles T=1. Preserve it unless mode is known.
                if ((frame->type == T1BlockType::S && frame->pcb == 0xc1) ||
                    (frame->type == T1BlockType::I && (frame->pcb & 0x20) != 0 &&
                     !validCommandApdu(body))) {
                    command_ = metadata;
                    command_.data.assign(body.begin(), body.end());
                    phase_ = Phase::Probe;
                    return out;
                }
            }
        }
        if (validCommandApdu(body)) {
            command_ = metadata;
            command_.data.assign(body.begin(), body.end());
            phase_ = Phase::Apdu;
            out.push_back(command_);
        }
        return out;
    }

    const auto status = message[7];
    if (message[0] == 0x81 || message[0] == 0x82 || message[0] == 0x83) {
        if (polls_[sequence] == message[0]) {
            if ((status & 3) != 0) {
                emitProbeFallback(out);
                resetTransport(); // Inactive or removed card invalidates chains.
            } else if ((status & 0xc0) != 0x80) polls_[sequence] = 0;
        }
        return out;
    }
    if (mode_ == TransportMode::T1 && phase_ != Phase::Atr && phase_ != Phase::Secure &&
        message[0] == 0x80 &&
        (phase_ != Phase::T1 || sequence != wireSequence_)) {
        discardDamagedExchange();
        return out;
    }
    if (phase_ == Phase::Idle || phase_ == Phase::CommandNext || phase_ == Phase::ResponseNext ||
        sequence != wireSequence_) return out;
    if (message[0] == 0x80 && (status & 0xc0) == 0x80 && (status & 0x3f) == 0 &&
        body.empty()) return out;
    if (phase_ == Phase::Secure) {
        // PIN entry may fail or be cancelled; neither damages the transport.
        if ((status & 3) != 0) resetTransport();
        else clearExchange();
        return out;
    }
    if (message[0] != 0x80 || status != 0 || message[8] != 0) {
        emitProbeFallback(out);
        if ((status & 3) != 0) resetTransport();
        else discardDamagedExchange();
        return out;
    }
    const auto chain = message[9];
    if (phase_ == Phase::Atr) {
        if (chain == 0 && body.size() >= 2 && body.size() <= 33 && (body[0] == 0x3b || body[0] == 0x3f)) {
            metadata.direction = Direction::Atr;
            metadata.data.assign(body.begin(), body.end());
            out.push_back(std::move(metadata));
        }
        clearExchange();
        return out;
    }
    if (phase_ == Phase::CommandAck) {
        if (chain == 0x10 && body.empty()) phase_ = Phase::CommandNext;
        else clearExchange();
        return out;
    }
    if (phase_ == Phase::Probe) {
        if (chain == 0 && matchesProbe(body)) {
            mode_ = TransportMode::T1;
            t1_.reset();
            emitT1(false, command_.data, command_, out);
            emitT1(true, body, metadata, out);
            clearExchange();
            return out;
        }
        if (!validCommandApdu(command_.data)) { clearExchange(); return out; }
        out.push_back(command_);
        phase_ = Phase::Apdu;
    }
    if (phase_ == Phase::T1) {
        if (chain == 0) emitT1(true, body, metadata, out);
        else t1_.transportGap();
        clearExchange();
        return out;
    }
    if (phase_ == Phase::Apdu) {
        if (chain == 0 && body.size() >= 2 && body.size() <= maxResponse) {
            metadata.sequence = command_.sequence;
            metadata.data.assign(body.begin(), body.end());
            out.push_back(std::move(metadata));
            clearExchange();
        } else if (chain == 1 && body.size() <= maxResponse) {
            response_ = metadata;
            response_.sequence = command_.sequence;
            response_.data.assign(body.begin(), body.end());
            phase_ = Phase::ResponseNext;
        } else clearExchange();
        return out;
    }
    if (phase_ == Phase::Response) {
        if ((chain != 2 && chain != 3) || !append(response_.data, body, maxResponse)) {
            clearExchange();
        } else if (chain == 3) phase_ = Phase::ResponseNext;
        else {
            if (response_.data.size() >= 2) out.push_back(std::move(response_));
            clearExchange();
        }
    }
    return out;
}
} // namespace apdu
