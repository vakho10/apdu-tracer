#include "RawCaptureWriter.h"

#include <algorithm>

namespace apdu {

RawCaptureWriter::RawCaptureWriter(QString path) : file_(std::move(path)) {}

bool RawCaptureWriter::open() {
    // QFile buffers writes; Truncate starts each session with a fresh file.
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail();
    return true;
}

bool RawCaptureWriter::fail() {
    if (!failed_) error_ = file_.errorString();
    failed_ = true;
    return false;
}

bool RawCaptureWriter::write(std::span<const std::uint8_t> bytes) {
    if (failed_ || !file_.isOpen() || bytes.empty()) return true;
    const auto size = static_cast<qint64>(bytes.size());
    if (file_.write(reinterpret_cast<const char*>(bytes.data()), size) != size) {
        fail();
        file_.close();
        return false;
    }
    track(bytes);
    return true;
}

void RawCaptureWriter::track(std::span<const std::uint8_t> bytes) {
    const auto read32 = [this](std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto byte = header_[offset + (little_ ? 3 - i : i)];
            value = (value << 8) | byte;
        }
        return value;
    };
    std::size_t i = 0;
    while (i < bytes.size() && state_ != State::Untracked) {
        if (state_ == State::RecordData) {
            const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, bytes.size() - i));
            i += take;
            remaining_ -= take;
            if (remaining_ == 0) {
                state_ = State::RecordHeader;
                complete_ = written_ + static_cast<qint64>(i);
            }
            continue;
        }
        const std::size_t need = state_ == State::GlobalHeader ? 24 : 16;
        const auto take = std::min(need - headerFill_, bytes.size() - i);
        std::copy_n(bytes.data() + i, take, header_.data() + headerFill_);
        headerFill_ += take;
        i += take;
        if (headerFill_ < need) break;
        headerFill_ = 0;
        if (state_ == State::GlobalHeader) {
            little_ = true;
            switch (read32(0)) {
            case 0xa1b2c3d4: case 0xa1b23c4d: break;
            case 0xd4c3b2a1: case 0x4d3cb2a1: little_ = false; break;
            default: state_ = State::Untracked; continue;
            }
            snaplen_ = read32(16);
            state_ = State::RecordHeader;
        } else {
            const auto captured = read32(8);
            // The decoder rejects this too; keep the bytes for diagnosis.
            if (captured > snaplen_) { state_ = State::Untracked; continue; }
            remaining_ = captured;
            state_ = captured ? State::RecordData : State::RecordHeader;
        }
        if (state_ == State::RecordHeader) complete_ = written_ + static_cast<qint64>(i);
    }
    written_ += static_cast<qint64>(bytes.size());
}

bool RawCaptureWriter::finish() {
    if (!file_.isOpen()) return !failed_;
    const qint64 keep = state_ == State::Untracked ? written_ : complete_;
    if (!file_.flush() || (keep < written_ && !file_.resize(keep))) fail();
    file_.close();
    return !failed_;
}

} // namespace apdu
