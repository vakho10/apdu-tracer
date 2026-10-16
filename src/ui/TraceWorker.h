#pragma once

#include <QMutex>
#include <QStringList>
#include <QThread>
#include <stop_token>
#include "core/TraceDecoder.h"

namespace apdu {

// Only the bounded text queue crosses threads. Capture and decoding both run
// on this worker, so high-volume USB traffic never queues raw bytes on the GUI.
class TraceWorker final : public QThread {
    Q_OBJECT
public:
    enum class Source { Live, File };
    // rawCapturePath, when set for a live capture, receives the unmodified
    // USBPcap stream as a classic .pcap that can be replayed later.
    TraceWorker(Source source, QString path, int device, QObject* parent = nullptr,
                TransportMode mode = TransportMode::Auto, QString rawCapturePath = {});
    ~TraceWorker() override;
    void stop();
    QStringList takeLines();
    quint64 droppedLines();

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
    std::stop_source stopSource_;
    QMutex mutex_;
    QStringList pending_;
    qsizetype pendingChars_ = 0;
    quint64 dropped_ = 0;
};

} // namespace apdu
