#include "MainWindow.h"
#include "TraceWorker.h"
#include "capture/UsbPcapBackend.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <exception>

namespace apdu {
namespace {
constexpr int addressRole = Qt::UserRole + 1;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("APDU Tracer — Native"));
    resize(1100, 680);
    setMinimumSize(760, 420);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    auto* captureBar = new QHBoxLayout;
    captureBar->addWidget(new QLabel(QStringLiteral("Capture device:")));
    devices_ = new QComboBox;
    devices_->setMinimumWidth(170);
    devices_->setToolTip(QStringLiteral(
        "USBPcap captures a USB root hub. Pick the hub to capture every device on it, "
        "or a device listed under it to capture only that USB address."));
    captureBar->addWidget(devices_, 1);
    refresh_ = new QPushButton(QStringLiteral("Refresh"));
    captureBar->addWidget(refresh_);
    captureBar->addWidget(new QLabel(QStringLiteral("USB address:")));
    address_ = new QSpinBox;
    address_->setRange(0, 127);
    address_->setSpecialValueText(QStringLiteral("All"));
    address_->setToolTip(QStringLiteral(
        "USB address to capture on this root hub. During live capture the USBPcap driver "
        "discards other devices' traffic. All also captures devices attached later."));
    captureBar->addWidget(address_);
    start_ = new QPushButton(QStringLiteral("Start capture"));
    stop_ = new QPushButton(QStringLiteral("Stop"));
    saveRaw_ = new QCheckBox(QStringLiteral("Save raw capture…"));
    saveRaw_->setToolTip(QStringLiteral(
        "Also write the unmodified USBPcap stream to a .pcap file, to decode it again later "
        "in another transport mode or open it in Wireshark."));
    captureBar->addWidget(saveRaw_);
    captureBar->addWidget(start_);
    captureBar->addWidget(stop_);
    layout->addLayout(captureBar);

    auto* tools = new QHBoxLayout;
    open_ = new QPushButton(QStringLiteral("Open PCAP…"));
    save_ = new QPushButton(QStringLiteral("Save text…"));
    clear_ = new QPushButton(QStringLiteral("Clear"));
    tools->addWidget(open_);
    tools->addWidget(save_);
    tools->addWidget(clear_);
    tools->addWidget(new QLabel(QStringLiteral("Reader transport:")));
    transport_ = new QComboBox;
    transport_->addItem(QStringLiteral("Auto"), static_cast<int>(TransportMode::Auto));
    transport_->addItem(QStringLiteral("APDU"), static_cast<int>(TransportMode::Apdu));
    transport_->addItem(QStringLiteral("T=1"), static_cast<int>(TransportMode::T1));
    transport_->setToolTip(QStringLiteral(
        "Auto detects T=1 from protocol exchanges. Choose T=1 for a TPDU reader when capturing mid-session, "
        "or APDU to keep an APDU reader's bytes unchanged."));
    tools->addWidget(transport_);
    tools->addWidget(new QLabel(QStringLiteral("Driver buffer:")));
    buffer_ = new QComboBox;
    for (const int mebibytes : {4, 8, 16, 32, 64, 128})
        buffer_->addItem(QStringLiteral("%1 MiB").arg(mebibytes), static_cast<quint32>(mebibytes) * 1024 * 1024);
    buffer_->setCurrentIndex(buffer_->findData(CaptureOptions::defaultBufferBytes));
    buffer_->setToolTip(QStringLiteral(
        "Kernel memory USBPcap reserves for live capture. The driver silently drops packets "
        "when it fills; choose more for busy hubs or when capturing the whole root hub."));
    tools->addWidget(buffer_);
    tools->addStretch();
    tools->addWidget(new QLabel(QStringLiteral(">> Sent     << Received")));
    autoScroll_ = new QCheckBox(QStringLiteral("Auto-scroll"));
    autoScroll_->setChecked(true);
    tools->addWidget(autoScroll_);
    layout->addLayout(tools);

    trace_ = new QPlainTextEdit;
    trace_->setReadOnly(true);
    trace_->setLineWrapMode(QPlainTextEdit::NoWrap);
    trace_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    trace_->setMaximumBlockCount(20000);
    trace_->setPlaceholderText(QStringLiteral(
        "Choose your reader (or its USB root hub) and start capture, or open a USBPcap .pcap file.\n\n"
        "APDUs appear here as hexadecimal text.\n"
        "Supports APDU and T=1 USB CCID readers. ATRs and transport-control messages are omitted."));
    layout->addWidget(trace_, 1);
    layout->addWidget(new QLabel(QStringLiteral(
        "Showing recent APDUs (up to 20,000 rows / 16 MiB of text). Save exports the displayed trace.")));
    setCentralWidget(central);
    status_ = new QLabel(QStringLiteral("Ready"));
    count_ = new QLabel(QStringLiteral("0 APDUs"));
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(count_);

    poll_ = new QTimer(this);
    poll_->setInterval(50);
    connect(poll_, &QTimer::timeout, this, &MainWindow::drainLines);
    connect(refresh_, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(devices_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) address_->setValue(devices_->itemData(index, addressRole).toInt());
    });
    connect(start_, &QPushButton::clicked, this, &MainWindow::startLive);
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (worker_) {
            worker_->stop();
            stop_->setEnabled(false);
            status_->setText(QStringLiteral("Stopping…"));
        }
    });
    connect(open_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Open USBPcap capture"),
            {}, QStringLiteral("Classic PCAP (*.pcap *.cap);;All files (*)"));
        if (!path.isEmpty()) openCapture(path);
    });
    connect(saveRaw_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked) {
            rawPath_.clear();
            saveRaw_->setToolTip({});
            return;
        }
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save raw USBPcap capture"),
            QStringLiteral("apdu-capture.pcap"), QStringLiteral("Classic PCAP (*.pcap)"));
        if (path.isEmpty()) {
            saveRaw_->setChecked(false);
            return;
        }
        rawPath_ = path;
        saveRaw_->setToolTip(QStringLiteral("The next live capture is saved to %1").arg(path));
    });
    connect(clear_, &QPushButton::clicked, this, &MainWindow::clearTrace);
    connect(save_, &QPushButton::clicked, this, &MainWindow::saveTrace);
    refreshDevices();
    setBusy(false);
}

MainWindow::~MainWindow() {
    if (worker_) { worker_->stop(); worker_->wait(); }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (worker_) { worker_->stop(); worker_->wait(); }
    event->accept();
}

QString MainWindow::traceText() const { return trace_->toPlainText(); }

void MainWindow::refreshDevices() {
    const auto previousPath = devices_->currentData().toString();
    const auto previousAddress = devices_->currentData(addressRole).toInt();
    const auto address = address_->value();
    devices_->clear();
    try {
        int previous = -1;
        for (const auto& device : UsbPcapBackend::devices()) {
            const auto path = QString::fromStdString(device.path);
            if (path == previousPath && (previous < 0 || device.address == previousAddress))
                previous = devices_->count();
            devices_->addItem(QString::fromStdString(device.label), path);
            devices_->setItemData(devices_->count() - 1, device.address, addressRole);
        }
        if (previous >= 0) devices_->setCurrentIndex(previous);
        // Keep a manually chosen address when the same entry is still listed.
        if (previous >= 0 && devices_->currentData(addressRole).toInt() == previousAddress)
            address_->setValue(address);
        status_->setText(devices_->count() ? QStringLiteral("Ready — select your reader")
            : QStringLiteral("No USBPcap devices found. Install USBPcap and reboot; offline replay is available."));
    } catch (const std::exception& error) {
        status_->setText(QString::fromUtf8(error.what()));
    }
    start_->setEnabled(devices_->count() > 0);
}

void MainWindow::startLive() {
    if (worker_ || devices_->currentIndex() < 0) return;
    startWorker(new TraceWorker(TraceWorker::Source::Live, devices_->currentData().toString(),
                                address_->value(), this, static_cast<TransportMode>(transport_->currentData().toInt()),
                                rawPath_, buffer_->currentData().toUInt()));
    sessionRawPath_ = rawPath_;
    // A path is used once, so a later capture never silently overwrites it.
    saveRaw_->setChecked(false);
}

void MainWindow::openCapture(const QString& path) {
    if (worker_) return;
    startWorker(new TraceWorker(TraceWorker::Source::File, path, address_->value(), this,
                                static_cast<TransportMode>(transport_->currentData().toInt())));
}

void MainWindow::startWorker(TraceWorker* worker) {
    clearTrace();
    sessionRawPath_.clear();
    worker_ = worker;
    failed_ = false;
    setBusy(true);
    status_->setText(QStringLiteral("Starting…"));
    connect(worker, &TraceWorker::ready, this, [this](const QString& message) {
        status_->setText(message);
    });
    connect(worker, &TraceWorker::failed, this, [this](const QString& message) {
        failed_ = true;
        status_->setText(message);
        status_->setToolTip(message);
    });
    connect(worker, &QThread::finished, this, [this, worker] {
        drainLines();
        const auto dropped = worker->droppedLines();
        poll_->stop();
        worker_ = nullptr;
        worker->deleteLater();
        setBusy(false);
        if (!failed_) {
            auto message = dropped ? QStringLiteral("Capture ended — %1 APDUs dropped while display was busy").arg(dropped)
                                   : QStringLiteral("Session ended — %1 APDUs").arg(total_);
            if (!sessionRawPath_.isEmpty())
                message += QStringLiteral("; raw capture saved to %1").arg(sessionRawPath_);
            status_->setText(message);
        }
        emit sessionFinished(!failed_);
    });
    poll_->start();
    worker->start();
}

void MainWindow::drainLines() {
    if (!worker_) return;
    const auto lines = worker_->takeLines();
    if (!lines.isEmpty()) {
        const int previous = trace_->verticalScrollBar()->value();
        trace_->appendPlainText(lines.join('\n'));
        // Row limits alone allow gigabytes of long extended APDUs. Trim
        // complete oldest rows to bound the document's UTF-16 text as well.
        auto* document = trace_->document();
        constexpr int maxCharacters = 8 * 1024 * 1024;
        int removeCharacters = 0;
        auto block = document->begin();
        while (document->characterCount() - removeCharacters > maxCharacters && block.next().isValid()) {
            removeCharacters += block.length();
            block = block.next();
        }
        if (removeCharacters > 0) {
            QTextCursor cursor(document);
            cursor.setPosition(removeCharacters, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        if (autoScroll_->isChecked())
            trace_->verticalScrollBar()->setValue(trace_->verticalScrollBar()->maximum());
        else
            trace_->verticalScrollBar()->setValue(previous);
        total_ += static_cast<quint64>(lines.size());
        count_->setText(QStringLiteral("%1 APDUs").arg(total_));
    }
    const auto dropped = worker_->droppedLines();
    if (dropped && !failed_)
        status_->setText(QStringLiteral("Capture active — %1 APDUs dropped while display was busy").arg(dropped));
}

void MainWindow::setBusy(bool busy) {
    devices_->setEnabled(!busy);
    transport_->setEnabled(!busy);
    buffer_->setEnabled(!busy);
    address_->setEnabled(!busy);
    refresh_->setEnabled(!busy);
    saveRaw_->setEnabled(!busy);
    start_->setEnabled(!busy && devices_->count() > 0);
    stop_->setEnabled(busy);
    open_->setEnabled(!busy);
    clear_->setEnabled(!busy);
    save_->setEnabled(!busy && total_ > 0);
}

void MainWindow::clearTrace() {
    trace_->clear();
    total_ = 0;
    count_->setText(QStringLiteral("0 APDUs"));
    save_->setEnabled(false);
    status_->setToolTip({});
}

void MainWindow::saveTrace() {
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save displayed APDUs"),
        QStringLiteral("apdu-trace.txt"), QStringLiteral("Text files (*.txt)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto text = (traceText() + '\n').toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit())
        QMessageBox::warning(this, QStringLiteral("Could not save trace"), file.errorString());
    else
        status_->setText(QStringLiteral("Saved %1").arg(path));
}

} // namespace apdu
