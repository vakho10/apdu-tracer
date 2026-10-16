#include "core/TraceDecoder.h"
#include "ui/MainWindow.h"
#include "ui/RawCaptureWriter.h"
#include "ui/TraceView.h"
#include "ui/TraceWorker.h"

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
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
    QObject::connect(&drain, &QTimer::timeout, &loop, [&] { count += worker.takeRows().size(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] { failed = true; worker.stop(); loop.quit(); });
    drain.start(10);
    deadline.start(10000);
    worker.start();
    loop.exec();
    worker.stop();
    worker.wait();
    count += worker.takeRows().size();
    require(!failed && count == 12000 && worker.droppedRows() == 0, "Offline replay lost entries in a large record");
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
    const auto rows = worker.takeRows();
    qsizetype bytes = 0;
    for (const auto& row : rows) bytes += row.data.size();
    require(blocked && !rows.isEmpty() && bytes <= 4 * 1024 * 1024 && worker.droppedRows() == 0,
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
    const auto rows = worker.takeRows();
    require(failure.isEmpty(), "Cancelling a pending probe reported a capture failure");
    require(rows.size() == 1001 && worker.droppedRows() == 0,
            "Stop lost the buffered raw-valid command or prior APDU entries");
    const auto& probeRow = rows.back();
    require(probeRow.sequence == 41 && probeRow.direction == apdu::Direction::Command
            && probeRow.data == QByteArray::fromHex("00C101FE3E")
            && apdu::formatTraceLine(probeRow).endsWith(QStringLiteral("seq 41  >>  00 C1 01 FE 3E")),
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

// Rows labelled "row N" in the Info column, each with four APDU bytes.
QList<apdu::TraceRow> rows(int first, int count) {
    QList<apdu::TraceRow> result;
    for (int i = first; i < first + count; ++i) {
        apdu::TraceRow row;
        row.info = QStringLiteral("row %1").arg(i);
        row.data = QByteArray(4, static_cast<char>(i));
        result.append(row);
    }
    return result;
}

apdu::TraceRow entryRow(apdu::Direction direction, const char* hex, quint8 sequence = 7) {
    const auto bytes = QByteArray::fromHex(hex);
    apdu::TraceEntry entry;
    entry.timestampUs = 1700000000123456ULL;
    entry.bus = 1;
    entry.device = 5;
    entry.sequence = sequence;
    entry.direction = direction;
    entry.data.assign(bytes.begin(), bytes.end());
    return apdu::makeTraceRow(entry);
}

QString firstVisibleRow(apdu::TraceView& view) {
    return view.indexAt(QPoint(2, 2)).siblingAtColumn(apdu::TraceModel::Info).data().toString();
}

void traceViewKeepsPosition() {
    apdu::TraceView view({100, 1024 * 1024});
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(500, 220);
    view.show();
    const auto& model = view.traceModel();

    view.appendRows(rows(0, 100), false);
    view.scrollTo(view.model()->index(40, 0), QAbstractItemView::PositionAtTop);
    QApplication::processEvents();
    require(firstVisibleRow(view) == QStringLiteral("row 40"),
            QStringLiteral("Fixture could not scroll to row 40: top '%1', scroll %2 of %3, viewport %4")
                .arg(firstVisibleRow(view)).arg(view.verticalScrollBar()->value())
                .arg(view.verticalScrollBar()->maximum()).arg(view.viewport()->height()).toUtf8().constData());

    // 30 new rows trim rows 0-29 above the view. Reading must stay on row 40.
    view.appendRows(rows(100, 30), false);
    QApplication::processEvents();
    require(model.rowCount() == 100, "Row limit was not applied");
    require(model.row(0).info == QStringLiteral("row 30"), "Oldest rows were not trimmed");
    require(firstVisibleRow(view) == QStringLiteral("row 40"),
            "Trimming old rows moved the view while auto-scroll was off");

    // With a filter, the anchored row is the first visible matching row.
    view.setFilterText(QStringLiteral("row 4"));
    QApplication::processEvents();
    view.scrollTo(view.model()->index(0, 0), QAbstractItemView::PositionAtTop);
    require(firstVisibleRow(view) == QStringLiteral("row 4") || firstVisibleRow(view) == QStringLiteral("row 40"),
            "Filter did not keep matching rows only");
    const auto anchored = firstVisibleRow(view);
    view.appendRows(rows(130, 5), false);
    QApplication::processEvents();
    require(firstVisibleRow(view) == anchored, "Filtered view moved while auto-scroll was off");
    view.setFilterText({});

    // When the anchored row itself is trimmed, the view stays at the top.
    view.scrollTo(view.model()->index(10, 0), QAbstractItemView::PositionAtTop);
    view.appendRows(rows(135, 95), false);
    QApplication::processEvents();
    require(view.verticalScrollBar()->value() == 0, "View was not clamped to the oldest remaining row");

    // Following the trace still shows the newest row.
    view.appendRows(rows(230, 5), true);
    QApplication::processEvents();
    require(view.verticalScrollBar()->value() == view.verticalScrollBar()->maximum(),
            "Auto-scroll did not follow the newest row");
}

void modelLimitsAndIds() {
    apdu::TraceModel model({3, 1000});
    require(model.append(rows(0, 5)) == 2 && model.rowCount() == 3, "Row limit trimmed the wrong number of rows");
    require(model.row(0).id == 3 && model.row(2).id == 5, "Rows were not numbered from 1 in arrival order");
    require(model.rowForId(2) == -1 && model.rowForId(4) == 1 && model.rowForId(6) == -1, "Row id lookup failed");
    require(model.bytes() == 12, "Byte total does not match the kept rows");
    model.clear();
    model.append(rows(0, 1));
    require(model.rowCount() == 1 && model.row(0).id == 1 && model.bytes() == 4, "Clear did not restart numbering");

    apdu::TraceModel bytes({100, 10});
    bytes.append(rows(0, 5));
    require(bytes.rowCount() == 2 && bytes.bytes() == 8, "Byte limit was not applied");
    apdu::TraceRow huge;
    huge.data = QByteArray(64, 'x');
    bytes.append({huge});
    require(bytes.rowCount() == 1 && bytes.bytes() == 64, "The newest row must be kept even above the byte limit");
}

void rowsAndFilter() {
    const auto select = entryRow(apdu::Direction::Command, "00A4040005A00000000300");
    const auto success = entryRow(apdu::Direction::Response, "6F109000");
    const auto missing = entryRow(apdu::Direction::Response, "6A82");
    const auto unknown = entryRow(apdu::Direction::Command, "00990000");
    require(select.info == QStringLiteral("SELECT") && unknown.info == QStringLiteral("INS 99"),
            "Command rows were not named");
    require(success.info == QStringLiteral("9000 — Success") && success.status == apdu::StatusKind::Success,
            "Success status was not described");
    require(missing.info == QStringLiteral("6A82 — File or application not found")
            && missing.status == apdu::StatusKind::Error, "Error status was not described");
    require(apdu::formatTraceLine(select).endsWith(
                QStringLiteral("bus 1 / device 5 / slot 0 / seq 7  >>  00 A4 04 00 05 A0 00 00 00 03 00")),
            "Exported line format changed");

    apdu::TraceModel model;
    model.append({select, success, missing, unknown});
    apdu::TraceFilterModel filter(&model);
    const auto shown = [&filter](const char* text) {
        filter.setFilterText(QString::fromUtf8(text));
        return filter.rowCount();
    };
    require(shown("") == 4, "An empty filter must show every row");
    require(shown("90 00") == 1 && shown("9000") == 1, "Hex filter with or without spaces");
    require(shown("09 00") == 0, "Hex filter must match whole bytes only");
    require(shown("a0 00 00 00 03") == 1, "Hex filter inside a command");
    require(shown("select") == 1, "Text filter is case-insensitive");
    require(shown("not found") == 1, "Text filter matches status meanings");
    require(shown("zz") == 0, "Unmatched filter");

    apdu::TraceRow longRow;
    longRow.data = QByteArray(apdu::TraceModel::maxDisplayBytes + 10, '\x11');
    model.append({longRow});
    const auto display = model.data(model.index(4, apdu::TraceModel::Data)).toString();
    require(display.endsWith(QStringLiteral(" …")) && display.size() < longRow.data.size() * 3,
            "Long APDUs must be shortened for display");
    require(model.text(4).endsWith(QStringLiteral(">> ") + QStringLiteral(" 11").repeated(longRow.data.size())),
            "Exported text must keep all bytes");
}

void copySelectedRows() {
    apdu::TraceView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(500, 220);
    view.show();
    view.appendRows({entryRow(apdu::Direction::Command, "00A4040000", 1),
                     entryRow(apdu::Direction::Response, "6A82", 1),
                     entryRow(apdu::Direction::Command, "00B0000010", 2)}, true);
    auto* selection = view.selectionModel();
    const auto flags = QItemSelectionModel::Select | QItemSelectionModel::Rows;
    selection->select(view.model()->index(2, 0), flags);
    selection->select(view.model()->index(0, 0), flags);
    const auto expected = view.traceModel().text(0) + QLatin1Char('\n') + view.traceModel().text(2);
    require(view.selectedText() == expected, "Selected rows were not copied oldest first in the export format");
    require(view.selectedHex() == QStringLiteral("00 A4 04 00 00\n00 B0 00 00 10"), "APDU hex copy");
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&view, &copy);
    require(QApplication::clipboard()->text() == expected, "Ctrl+C did not copy the selected rows");
}

QPushButton* button(QWidget& window, const QString& text) {
    for (auto* candidate : window.findChildren<QPushButton*>())
        if (candidate->text() == text) return candidate;
    throw std::runtime_error("Button not found");
}

QLabel* apduCount(QWidget& window) {
    for (auto* label : window.findChildren<QLabel*>())
        if (label->text().endsWith(QStringLiteral(" APDUs"))) return label;
    throw std::runtime_error("APDU count label not found");
}

void clearDuringSession(const QString& path) {
    apdu::MainWindow window;
    QEventLoop loop;
    QTimer deadline;
    bool finished = false, success = false;
    QObject::connect(&window, &apdu::MainWindow::sessionFinished, &loop,
                     [&](bool ok) { finished = true; success = ok; loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.setSingleShot(true);
    deadline.start(15000);

    // 30,000 APDUs. Clear discards at most the 10,000-row worker queue and the
    // rows shown so far, so APDUs always arrive after it.
    window.openCapture(path);
    auto* clear = button(window, QStringLiteral("Clear"));
    auto* stop = button(window, QStringLiteral("Stop"));
    require(clear->isEnabled(), "Clear was disabled while a session was running");

    while (window.traceText().isEmpty() && deadline.isActive())
        QApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
    require(!finished && stop->isEnabled(), "Replay ended before Clear could be tested mid-session");
    clear->click();
    require(window.traceText().isEmpty() && apduCount(window)->text() == QStringLiteral("0 APDUs"),
            "Clear did not empty the display and counter during a session");
    require(stop->isEnabled(), "Clear stopped the running session");

    if (!finished) loop.exec();
    require(finished && success, "Session did not finish successfully after Clear");
    const auto shown = window.traceText().split(QLatin1Char('\n'), Qt::SkipEmptyParts).size();
    const auto counted = apduCount(window)->text().section(QLatin1Char(' '), 0, 0).toLongLong();
    require(shown > 0 && counted < 30000,
            QStringLiteral("Display did not continue with only APDUs after Clear: %1 rows shown, %2 counted")
                .arg(shown).arg(counted).toUtf8().constData());
    // The counter counts every APDU since Clear; the table keeps the newest 20,000.
    require(shown == std::min<qint64>(counted, apdu::TraceLimits{}.maxRows),
            "APDU counter does not match the rows shown after Clear");
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
    const auto& model = window.traceView().traceModel();
    require(success && model.rowCount() > 0 && model.bytes() <= apdu::TraceLimits{}.maxBytes,
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
        const auto busy = directory.filePath(QStringLiteral("busy.pcap"));
        writeCapture(busy, 30000, false);
        losslessReplay(small);
        cancellationWithFullQueue(large);
        cancellationFlushesProbe(probe);
        rawCaptureReplays(directory.filePath(QStringLiteral("raw.pcap")));
        boundedWindow(large);
        traceViewKeepsPosition();
        modelLimitsAndIds();
        rowsAndFilter();
        copySelectedRows();
        clearDuringSession(busy);
        std::cout << "Lossless replay, bounded rows, cancellation, pending probe flush, raw capture replay, "
                     "table position, limits, filter, copy, and Clear during a session passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
