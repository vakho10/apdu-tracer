#include "ui/MainWindow.h"
#include "ui/TraceWorker.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

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
void writeCapture(const QString& path, int count, bool extended) {
    QByteArray apdu = QByteArray::fromHex(extended ? "00DA000000FFFF" : "00A40400");
    if (extended) apdu.append(QByteArray(65537, '\0')); // Lc=65535, Le=0000
    QByteArray message;
    message.append('\x6f');
    append32(message, static_cast<quint32>(apdu.size()));
    message.append(QByteArray(5, '\0'));
    message.append(apdu);
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
    append32(packet, static_cast<quint32>(message.size() * count));
    for (int i = 0; i < count; ++i) packet.append(message);
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
        writeCapture(small, 12000, false);
        writeCapture(large, 100, true);
        losslessReplay(small);
        cancellationWithFullQueue(large);
        boundedWindow(large);
        std::cout << "Lossless replay, bounded text, and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
