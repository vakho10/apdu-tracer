#include "TraceDecoder.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace apdu {
namespace {

using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t maxPacketBytes = 16 * 1024 * 1024;
constexpr std::size_t maxCcidPayload = 65544;
constexpr std::size_t maxChannels = 4096;
constexpr std::size_t maxEndpointStreams = 256;

std::uint16_t read16(Bytes b, std::size_t i, bool little = true) {
    return little ? static_cast<std::uint16_t>(b[i] | (b[i + 1] << 8))
                  : static_cast<std::uint16_t>((b[i] << 8) | b[i + 1]);
}

std::uint32_t read32(Bytes b, std::size_t i, bool little = true) {
    std::uint32_t value = 0;
    for (std::size_t n = 0; n != 4; ++n) {
        value = (value << 8) | b[i + (little ? 3 - n : n)];
    }
    return value;
}

bool knownType(std::uint8_t type, bool incoming) {
    if (incoming) {
        return type >= 0x80 && type <= 0x84;
    }
    switch (type) {
    case 0x61: case 0x62: case 0x63: case 0x65: case 0x69: case 0x6a:
    case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
    case 0x71: case 0x72: case 0x73:
        return true;
    default:
        return false;
    }
}

// ISO 7816 case 1, 2S, 3S, 4S, 2E, 3E and 4E. This checks framing, not INS.
bool completeCommandApdu(Bytes data) {
    if (data.size() == 4 || data.size() == 5) {
        return true;
    }
    if (data.size() < 6) {
        return false;
    }
    if (data[4] != 0) {
        const auto end = std::size_t{5} + data[4];
        return data.size() == end || data.size() == end + 1;
    }
    if (data.size() < 7) {
        return false;
    }
    if (data.size() == 7) {
        return true; // Two-byte extended Le, including 0000.
    }
    const auto lc = read16(data, 5, false);
    const auto end = std::size_t{7} + lc;
    return lc != 0 && (data.size() == end || data.size() == end + 2);
}

using EndpointKey = std::tuple<std::uint16_t, std::uint16_t, std::uint8_t>;
using SlotKey = std::tuple<std::uint16_t, std::uint16_t, std::uint8_t>;

struct Assembly {
    std::vector<std::uint8_t> bytes;
    std::uint64_t timestampUs{};
};

struct Pending {
    std::uint8_t sequence{};
    Direction direction{};
};

} // namespace

struct TraceDecoder::Impl {
    enum class Stage { GlobalHeader, RecordHeader, Packet };

    EntryHandler handler;
    Stage stage{Stage::GlobalHeader};
    std::vector<std::uint8_t> buffer;
    std::size_t needed{24};
    bool little{true};
    bool nanoseconds{false};
    bool finished{false};
    std::uint32_t snaplen{};
    std::uint64_t timestampUs{};
    std::map<EndpointKey, Assembly> streams;
    std::map<SlotKey, Pending> pending;

    explicit Impl(EntryHandler h) : handler(std::move(h)) {
        if (!handler) {
            throw std::invalid_argument("A trace entry handler is required.");
        }
    }

    void forgetDevice(std::uint16_t bus, std::uint16_t device) {
        const auto sameDevice = [=](const auto& item) {
            return std::get<0>(item.first) == bus && std::get<1>(item.first) == device;
        };
        std::erase_if(pending, sameDevice);
        std::erase_if(streams, sameDevice);
    }

    void globalHeader(Bytes b) {
        // Inspect raw magic before interpreting file-endian fields.
        const auto magic = read32(b, 0);
        switch (magic) {
        case 0xa1b2c3d4: little = true; nanoseconds = false; break;
        case 0xd4c3b2a1: little = false; nanoseconds = false; break;
        case 0xa1b23c4d: little = true; nanoseconds = true; break;
        case 0x4d3cb2a1: little = false; nanoseconds = true; break;
        case 0x0a0d0d0a:
            throw std::runtime_error("PCAPNG is not supported. Export as classic PCAP with USBPcap link type 249.");
        default:
            throw std::runtime_error("Not a supported classic PCAP capture (invalid magic).");
        }
        if (read16(b, 4, little) != 2 || read16(b, 6, little) != 4) {
            throw std::runtime_error("Only classic PCAP version 2.4 is supported.");
        }
        snaplen = read32(b, 16, little);
        if (snaplen == 0 || snaplen > maxPacketBytes) {
            throw std::runtime_error("PCAP snapshot length must be between 1 byte and 16 MiB.");
        }
        if (read32(b, 20, little) != 249) {
            throw std::runtime_error("This capture does not use USBPcap link type 249.");
        }
        stage = Stage::RecordHeader;
        needed = 16;
    }

    void recordHeader(Bytes b) {
        const auto seconds = read32(b, 0, little);
        const auto fraction = read32(b, 4, little);
        const auto captured = read32(b, 8, little);
        const auto original = read32(b, 12, little);
        if (fraction >= (nanoseconds ? 1000000000u : 1000000u)) {
            throw std::runtime_error("PCAP record has an invalid timestamp fraction.");
        }
        if (captured > snaplen || captured > maxPacketBytes || captured > original) {
            throw std::runtime_error("PCAP record length is invalid or exceeds the snapshot limit.");
        }
        if (captured < original) {
            throw std::runtime_error("PCAP packet is truncated by the capture snapshot length. Capture again with full packets.");
        }
        if (captured < 27) {
            throw std::runtime_error("PCAP packet is shorter than the USBPcap header.");
        }
        timestampUs = static_cast<std::uint64_t>(seconds) * 1000000 +
                      (nanoseconds ? fraction / 1000 : fraction);
        stage = Stage::Packet;
        needed = captured;
    }

    void emit(Bytes message, const EndpointKey& key, std::uint64_t time, Direction direction) {
        TraceEntry entry;
        entry.timestampUs = time;
        entry.bus = std::get<0>(key);
        entry.device = std::get<1>(key);
        entry.endpoint = std::get<2>(key);
        entry.slot = message[5];
        entry.sequence = message[6];
        entry.direction = direction;
        entry.data.assign(message.begin() + 10, message.end());
        handler(entry);
    }

    void ccidMessage(Bytes message, const EndpointKey& key, std::uint64_t time) {
        const bool incoming = (std::get<2>(key) & 0x80) != 0;
        const SlotKey slot{std::get<0>(key), std::get<1>(key), message[5]};
        const auto sequence = message[6];
        if (!incoming) {
            // One outstanding command per slot; any subsequent command replaces
            // it, including unsupported secure/escape/chaining transactions.
            pending.erase(slot);
            const auto body = message.subspan(10);
            const bool command = message[0] == 0x6f && read16(message, 8) == 0 &&
                                 completeCommandApdu(body);
            const bool powerOn = message[0] == 0x62 && body.empty() &&
                                 message[7] <= 3 && read16(message, 8) == 0;
            if (!command && !powerOn) {
                return;
            }
            if (pending.size() >= maxChannels) {
                throw std::runtime_error("Too many simultaneous CCID slots in the capture.");
            }
            pending.emplace(slot, Pending{sequence, command ? Direction::Response : Direction::Atr});
            if (command) {
                emit(message, key, time, Direction::Command);
            }
            return;
        }

        const auto found = pending.find(slot);
        if (found == pending.end() || found->second.sequence != sequence) {
            return; // Orphan responses cannot safely be distinguished from ATRs.
        }
        const auto status = message[7];
        if ((status & 0xc0) == 0x80 && (status & 0x3c) == 0 &&
            message.size() == 10) {
            return; // Time extension: the same sequence still awaits its response.
        }
        const auto direction = found->second.direction;
        pending.erase(found);
        if (message[0] != 0x80 || status != 0 || message[8] != 0 || message[9] != 0) {
            return; // Failed/inactive slots and CCID chaining are not APDU responses.
        }
        const auto body = message.subspan(10);
        if (direction == Direction::Atr) {
            if (body.size() >= 2 && body.size() <= 33 && (body[0] == 0x3b || body[0] == 0x3f)) {
                emit(message, key, time, direction);
            }
        } else if (body.size() >= 2 && body.size() <= 65538) {
            emit(message, key, time, direction);
        }
    }

    void bulkPayload(Bytes payload, const EndpointKey& key) {
        if (payload.empty()) {
            return;
        }
        const bool incoming = (std::get<2>(key) & 0x80) != 0;
        if (!streams.contains(key)) {
            if (!knownType(payload[0], incoming)) {
                return; // Do not search arbitrary bulk bytes for embedded CCID headers.
            }
            if (streams.size() >= maxEndpointStreams) {
                throw std::runtime_error("Too many USB endpoint streams in the capture.");
            }
            streams.emplace(key, Assembly{});
        }
        auto& assembly = streams.at(key);
        while (!payload.empty()) {
            if (assembly.bytes.empty()) {
                if (!knownType(payload[0], incoming)) {
                    streams.erase(key);
                    return;
                }
                assembly.timestampUs = timestampUs;
            }
            std::size_t target = 10;
            if (assembly.bytes.size() >= 10) {
                target += read32(assembly.bytes, 1);
            }
            const auto take = std::min(payload.size(), target - assembly.bytes.size());
            assembly.bytes.insert(assembly.bytes.end(), payload.begin(), payload.begin() + take);
            payload = payload.subspan(take);
            if (assembly.bytes.size() < 10) {
                break;
            }
            const auto bodySize = read32(assembly.bytes, 1);
            if (bodySize > maxCcidPayload) {
                // This may be unrelated bulk traffic that coincidentally started
                // with a CCID opcode. Drop state instead of allocating its length.
                forgetDevice(std::get<0>(key), std::get<1>(key));
                return;
            }
            if (assembly.bytes.size() == 10 + bodySize) {
                ccidMessage(assembly.bytes, key, assembly.timestampUs);
                assembly.bytes.clear();
            }
        }
        if (assembly.bytes.empty()) {
            streams.erase(key);
        }
    }

    void packet(Bytes b) {
        // USBPcap fields remain little-endian even in a big-endian PCAP file.
        const auto headerLength = read16(b, 0);
        const auto dataLength = read32(b, 23);
        if (headerLength < 27 || headerLength > b.size() ||
            dataLength != b.size() - headerLength) {
            throw std::runtime_error("USBPcap header or payload length is invalid or truncated.");
        }
        if (b[22] != 3 || (b[21] & 0x0f) == 0 || (b[21] & 0x70) != 0 || (b[16] & 0xfe) != 0) {
            return; // Only bulk transfers on a valid non-control endpoint.
        }
        const auto bus = read16(b, 17);
        const auto device = read16(b, 19);
        const bool completion = (b[16] & 1) != 0;
        const bool incoming = (b[21] & 0x80) != 0;
        if (completion && read32(b, 10) != 0) {
            // A failed transfer creates a gap; never join fragments across it.
            forgetDevice(bus, device);
            return;
        }
        if (incoming != completion) {
            return; // OUT submission / IN completion only: no double counting IRPs.
        }
        bulkPayload(b.subspan(headerLength, dataLength), {bus, device, b[21]});
    }

    void feed(Bytes bytes) {
        if (finished) {
            throw std::logic_error("Cannot feed a finished trace decoder.");
        }
        while (!bytes.empty()) {
            const auto take = std::min(bytes.size(), needed - buffer.size());
            buffer.insert(buffer.end(), bytes.begin(), bytes.begin() + take);
            bytes = bytes.subspan(take);
            // PCAPNG's magic is enough to provide a useful diagnostic even for
            // captures shorter than the classic global header.
            if (stage == Stage::GlobalHeader && buffer.size() >= 4 &&
                read32(buffer, 0) == 0x0a0d0d0a) {
                throw std::runtime_error("PCAPNG is not supported. Export as classic PCAP with USBPcap link type 249.");
            }
            if (buffer.size() != needed) {
                continue;
            }
            switch (stage) {
            case Stage::GlobalHeader: globalHeader(buffer); break;
            case Stage::RecordHeader: recordHeader(buffer); break;
            case Stage::Packet:
                packet(buffer);
                stage = Stage::RecordHeader;
                needed = 16;
                break;
            }
            buffer.clear();
        }
    }
};

TraceDecoder::TraceDecoder(EntryHandler handler) : impl_(std::make_unique<Impl>(std::move(handler))) {}
TraceDecoder::~TraceDecoder() = default;

void TraceDecoder::feed(std::span<const std::uint8_t> bytes) {
    impl_->feed(bytes);
}

void TraceDecoder::finish() {
    if (impl_->finished) {
        return;
    }
    impl_->finished = true;
    if (impl_->stage != Impl::Stage::RecordHeader || !impl_->buffer.empty()) {
        throw std::runtime_error("Capture ended inside a PCAP header or packet (truncated stream).");
    }
    impl_->streams.clear();
    impl_->pending.clear();
}

} // namespace apdu
