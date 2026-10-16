#pragma once

#include <QFile>
#include <QString>
#include <array>
#include <cstdint>
#include <span>

namespace apdu {

// Tees a live classic-PCAP byte stream into a file. Chunks may split headers
// and records anywhere; the writer follows record boundaries so finish() can
// drop a record cut in half by Stop and leave a file that replays cleanly.
// Writes are buffered by QFile. After an error further writes are ignored.
class RawCaptureWriter {
public:
    explicit RawCaptureWriter(QString path);
    bool open();
    // Returns false only for the write that first fails.
    bool write(std::span<const std::uint8_t> bytes);
    // Flushes, trims an incomplete final record, and closes. Safe to repeat.
    bool finish();
    bool failed() const { return failed_; }
    QString errorString() const { return error_; }
    QString path() const { return file_.fileName(); }

private:
    void track(std::span<const std::uint8_t> bytes);
    bool fail();

    QFile file_;
    QString error_;
    bool failed_ = false;
    // Boundary tracking. Unknown layouts stop tracking and keep every byte.
    enum class State { GlobalHeader, RecordHeader, RecordData, Untracked } state_ = State::GlobalHeader;
    std::array<std::uint8_t, 24> header_{};
    std::size_t headerFill_ = 0;
    bool little_ = true;
    std::uint32_t snaplen_ = 0;
    std::uint64_t remaining_ = 0; // payload bytes left in the current record
    qint64 written_ = 0;
    qint64 complete_ = 0; // end of the last complete header or record
};

} // namespace apdu
