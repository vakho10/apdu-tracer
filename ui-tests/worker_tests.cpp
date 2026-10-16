#include "core/TraceDecoder.h"
#include "ui/MainWindow.h"
#include "ui/RawCaptureWriter.h"
#include "ui/TraceWorker.h"

#include <QApplication>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void append16(QByteArray& bytes, quint16 value) {
    for (int shift = 0; shift < 16; shift += 8) bytes.append(static_cast<char>(value >> shift));
}
void append32(QByteArray& bytes, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.append(static_cast<char>(value >> shift));
}

// Put many complete CCID commands inside one USB record. The decoder delivers
// them in one feed() call, exercising callback-level backpressure.
void writeCapture(const QString& path, int count, bool extended, bool finalProbe = false) {
    QByteArray apdu = QByteArray::fromHex(extended ? "00DA000000FFFF" : "00A40400");
    if (extended) apdu.append(QByteArray(65537, '\0')); // Lc=65535, Le=0000
    QByteArray message;
    message.append('\x6f');
    append32(message, static_cast<quint32>(apdu.size()));
    message.append(QByteArray(5, '\0'));
    message.append(apdu);
    QByteArray probe;
    if (finalProbe) {
        probe.append('\x6f');
        append32(probe, 5);
        probe.append('\0');   // slot
        probe.append('\x29'); // sequence 41
        probe.append(QByteArray(3, '\0'));
        probe.append(QByteArray::fromHex("00C101FE3E")); // raw-valid APDU / IFS request
    }
    QByteArray packet;
    append16(packet, 27);
    packet.append(QByteArray(8, '\0')); // IRP
    append32(packet, 0); // status
    append16(packet, 9); // bulk URB
    packet.append('\0'); // OUT submission
    append16(packet, 1); // bus
    append16(packet, 5); // device
    packet.append('\x01'); // endpoint
    packet.append('\x03'); // bulk
    append32(packet, static_cast<quint32>(message.size() * count + probe.size()));
    for (int i = 0; i < count; ++i) packet.append(message);
    packet.append(probe);
    QByteArray file;
    append32(file, 0xa1b2c3d4);
    append16(file, 2);
    append16(file, 4);
    append32(file, 0);
    append32(file, 0);
    append32(file, 16 * 1024 * 1024);
    append32(file, 249);
    append32(file, 1);
    append32(file, 0);
    append32(file, static_cast<quint32>(packet.size()));
    append32(file, static_cast<quint32>(packet.size()));
    file.append(packet);
    QFile output(path);
    require(output.open(QIODevice::WriteOnly) && output.write(file) == file.size(), "Could not write fixture");
}

void losslessReplay(const QString& path) {
    apdu::TraceWorker worker(apdu::TraceWorker::Source::File, path, 0);
    QEventLoop loop;
    QTimer drain, deadline;
    qsizetype count = 0;
    bool failed = false;
    QObject::connect(&worker, &apdu::TraceWorker::failed, &loop, [&](const QString&) { failed = true; });
    QObject::connect(&worker, &QThread::finished, &loop, &QEventLoop::quit);
    QObject::connect(&drain, &QTimer::timeout, &loop, [&] { count += worker.takeLines().size(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { failed = true; worker.stop(); loop.quit(); });
    drain.start(10);
    deadline.start(10000);
    worker.start();
    loop.exec();
    worker.stop();
    worker.wait();
    count += worker.takeLines().size();
    require(!failed && count == 12000 && worker.droppedLines() == 0, "Offline replay lost entries in a large record");
}

void cancellationWithFullQueue(const QString& path) {
    apdu::TraceWorker worker(apdu::TraceWorker::Source::File, path, 0);
    QEventLoop loop;
    QTimer stop;
    bool blocked = false;
    QObject::connect(&worker, &QThread::finished, &loop, &QEventLoop::quit);
    QObject::connect(&stop, &QTimer::timeout, &loop, [&] { blocked = worker.isRunning(); worker.stop(); });
    stop.setSingleShot(true);
    stop.start(200);
    worker.start();
    loop.exec();
    worker.wait();
    const auto lines = worker.takeLines();
    qsizetype characters = 0;
    for (const auto& line : lines) characters += line.size();
    require(blocked && !lines.isEmpty() && characters <= 4 * 1024 * 1024 && worker.droppedLines() == 0,
            "Worker did not bound long APDUs or cancel backpressure safely");
}

void cancellationFlushesProbe(const QString& path) {
    apdu::TraceWorker worker(apdu::TraceWorker::Source::File, path, 0);
    QString failure;
    // The main thread intentionally does not drain. Read failure only after
    // wait() joins the worker, so this direct connection has no concurrent read.
    QObject::connect(&worker, &apdu::TraceWorker::failed, &worker,
                     [&](const QString& message) { failure = message; }, Qt::DirectConnection);
    worker.start();
    require(!worker.wait(200), "Probe fixture finished instead of pausing on a full replay batch");
    worker.stop();
    require(worker.wait(2000), "Flushing the pending probe blocked cancellation");
    const auto lines = worker.takeLines();
    require(failure.isEmpty(), "Cancelling a pending probe reported a capture failure");
    require(lines.size() == 1001 && worker.droppedLines() == 0,
            "Stop lost the buffered raw-valid command or prior APDU entries");
    require(lines.back().contains(QStringLiteral("seq 41"))
            && lines.back().endsWith(QStringLiteral(">>  00 C1 01 FE 3E")),
            "Stop did not preserve the unresolved probe as its original raw APDU");
}

// One USBPcap bulk record carrying one CCID message, as a live stream sees it.
void appendRecord(QByteArray& stream, quint32 time, bool in, const QByteArray& ccid) {
    QByteArray packet;
    append16(packet, 27);
    packet.append(QByteArray(8, '\0')); // IRP
    append32(packet, 0);
    append16(packet, 9);
    packet.append(in ? '\x01' : '\0');
    append16(packet, 1);
    append16(packet, 5);
    packet.append(in ? '\x82' : '\x01');
    packet.append('\x03');
    append32(packet, static_cast<quint32>(ccid.size()));
    packet.append(ccid);
    append32(stream, time);
    append32(stream, 0);
    append32(stream, static_cast<quint32>(packet.size()));
    append32(stream, static_cast<quint32>(packet.size()));
    stream.append(packet);
}

QByteArray ccidMessage(char type, quint8 sequence, const QByteArray& payload) {
    QByteArray message;
    message.append(type);
    append32(message, static_cast<quint32>(payload.size()));
    message.append('\0'); // slot
    message.append(static_cast<char>(sequence));
    message.append(QByteArray(3, '\0'));
    message.append(payload);
    return message;
}

std::vector<apdu::TraceEntry> decode(const QByteArray& bytes, bool cancel) {
    std::vector<apdu::TraceEntry> entries;
    apdu::TraceDecoder decoder([&](const apdu::TraceEntry& entry) { entries.push_back(entry); });
    decoder.feed(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                          static_cast<size_t>(bytes.size())));
    if (cancel) decoder.cancel(); else decoder.finish();
    return entries;
}

// Live chunks are arbitrary stream slices; Stop can cut the last record. The
// saved file must end on a record boundary and replay to the same entries.
void rawCaptureReplays(const QString& path) {
    QByteArray stream;
    append32(stream, 0xa1b2c3d4);
    append16(stream, 2);
    append16(stream, 4);
    append32(stream, 0);
    append32(stream, 0);
    append32(stream, 65535);
    append32(stream, 249);
    constexpr int exchanges = 40;
    for (int i = 0; i < exchanges; ++i) {
        const auto sequence = static_cast<quint8>(i);
        appendRecord(stream, 100 + i, false, ccidMessage('\x6f', sequence,
            QByteArray::fromHex("00A4040007A0000000030000") + QByteArray(1, static_cast<char>(i))));
        appendRecord(stream, 100 + i, true, ccidMessage('\x80', sequence,
            QByteArray(i % 7, static_cast<char>(i)) + QByteArray::fromHex("9000")));
    }
    const auto complete = stream.size();
    QByteArray cut;
    appendRecord(cut, 999, false, ccidMessage('\x6f', 99, QByteArray::fromHex("00B0000010")));
    stream.append(cut.left(cut.size() / 2));

    apdu::RawCaptureWriter writer(path);
    require(writer.open(), "Could not open raw capture output");
    const int sizes[] = {1, 3, 24, 7, 16, 100, 2, 513};
    qsizetype offset = 0;
    for (int i = 0; offset < stream.size(); ++i) {
        const auto size = std::min<qsizetype>(sizes[i % std::size(sizes)], stream.size() - offset);
        require(writer.write(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(stream.constData() + offset),
                                                      static_cast<size_t>(size))), "Raw capture write failed");
        offset += size;
    }
    require(writer.finish() && !writer.failed(), "Raw capture finish failed");

    QFile saved(path);
    require(saved.open(QIODevice::ReadOnly), "Could not reopen raw capture");
    const auto bytes = saved.readAll();
    require(bytes == stream.left(complete), "Raw capture was not trimmed to the last complete record");
    // finish() would throw if the file still ended mid-record.
    const auto replayed = decode(bytes, false);
    const auto live = decode(stream, true);
    require(replayed.size() == 2 * exchanges, "Raw capture replay lost entries");
    const auto same = [](const apdu::TraceEntry& a, const apdu::TraceEntry& b) {
        return a.timestampUs == b.timestampUs && a.bus == b.bus && a.device == b.device && a.slot == b.slot
            && a.sequence == b.sequence && a.direction == b.direction && a.data == b.data;
    };
    require(std::equal(replayed.begin(), replayed.end(), live.begin(), live.end(), same),
            "Raw capture replay differs from the live decode");

    apdu::RawCaptureWriter missing(QFileInfo(path).dir().filePath(QStringLiteral("missing/raw.pcap")));
    require(!missing.open() && missing.failed() && !missing.errorString().isEmpty(),
            "Opening an unwritable raw capture path did not report an error");
}

void boundedWindow(const QString& path) {
    apdu::MainWindow window;
    QEventLoop loop;
    QTimer deadline;
    bool success = false;
    QObject::connect(&window, &apdu::MainWindow::sessionFinished, &loop,
                     [&](bool ok) { success = ok; loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(15000);
    window.openCapture(path);
    loop.exec();
    const auto text = window.traceText();
    require(success && !text.isEmpty() && text.size() <= 8 * 1024 * 1024,
            "Window did not retain a bounded trace of long APDUs");
}
} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "Could not create fixture directory");
        const auto small = directory.filePath(QStringLiteral("many.pcap"));
        const auto large = directory.filePath(QStringLiteral("long.pcap"));
        const auto probe = directory.filePath(QStringLiteral("probe.pcap"));
        writeCapture(small, 12000, false);
        writeCapture(large, 100, true);
        writeCapture(probe, 1000, false, true);
        losslessReplay(small);
        cancellationWithFullQueue(large);
        cancellationFlushesProbe(probe);
        rawCaptureReplays(directory.filePath(QStringLiteral("raw.pcap")));
        boundedWindow(large);
        std::cout << "Lossless replay, bounded text, cancellation, pending probe flush, and raw capture replay passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
