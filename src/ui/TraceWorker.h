#pragma once

#include <QList>
#include <QMutex>
#include <QThread>
#include <stop_token>
#include "capture/UsbPcapBackend.h"
#include "core/TraceDecoder.h"
#include "TraceModel.h"

namespace apdu {

// Only the bounded row queue crosses threads. Capture and decoding both run
// on this worker, so high-volume USB traffic never queues raw bytes on the GUI.
class TraceWorker final : public QThread {
    Q_OBJECT
public:
    enum class Source { Live, File };
    // rawCapturePath, when set for a live capture, receives the unmodified
    // USBPcap stream as a classic .pcap that can be replayed later.
    // For live capture a non-zero device also becomes the USBPcap driver's
    // address filter, so other devices' traffic never fills its buffer.
    TraceWorker(Source source, QString path, int device, QObject* parent = nullptr,
                TransportMode mode = TransportMode::Auto, QString rawCapturePath = {},
                quint32 driverBufferBytes = CaptureOptions::defaultBufferBytes);
    ~TraceWorker() override;
    void stop();
    QList<TraceRow> takeRows();
    quint64 droppedRows();

signals:
    void ready(const QString& message);
    void failed(const QString& message);

protected:
    void run() override;

private:
    Source source_;
    QString path_;
    int device_;
    TransportMode mode_;
    QString rawCapturePath_;
    quint32 driverBufferBytes_;
    std::stop_source stopSource_;
    QMutex mutex_;
    QList<TraceRow> pending_;
    qsizetype pendingBytes_ = 0;
    quint64 dropped_ = 0;
};

} // namespace apdu
