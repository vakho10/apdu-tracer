#include "TraceWorker.h"
#include "capture/UsbPcapBackend.h"
#include "core/TraceDecoder.h"

#include <QDateTime>
#include <QFile>
#include <QMutexLocker>
#include <exception>
#include <stdexcept>

namespace apdu {

TraceWorker::TraceWorker(Source source, QString path, int device, QObject* parent)
    : QThread(parent), source_(source), path_(std::move(path)), device_(device) {}

TraceWorker::~TraceWorker() {
    stop();
    wait();
}

void TraceWorker::stop() { stopSource_.request_stop(); }

QStringList TraceWorker::takeLines() {
    QMutexLocker lock(&mutex_);
    QStringList result;
    result.swap(pending_);
    pendingChars_ = 0;
    return result;
}

quint64 TraceWorker::droppedLines() {
    QMutexLocker lock(&mutex_);
    return dropped_;
}

void TraceWorker::run() {
    try {
        TraceDecoder decoder([this](const TraceEntry& entry) {
            if (entry.direction == Direction::Atr || (device_ != 0 && entry.device != device_))
                return;
            const auto bytes = QByteArray(reinterpret_cast<const char*>(entry.data.data()),
                                          static_cast<qsizetype>(entry.data.size()));
            const auto time = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(entry.timestampUs / 1000))
                                  .toString(QStringLiteral("HH:mm:ss.zzz"));
            const auto line = QStringLiteral("[%1] bus %2 / device %3 / slot %4 / seq %5  %6  %7")
                                  .arg(time).arg(entry.bus).arg(entry.device)
                                  .arg(entry.slot).arg(entry.sequence)
                                  .arg(entry.direction == Direction::Command ? QStringLiteral(">>") : QStringLiteral("<<"))
                                  .arg(QString::fromLatin1(bytes.toHex(' ').toUpper()));
            while (!stopSource_.stop_requested()) {
                {
                    QMutexLocker lock(&mutex_);
                    if (pending_.size() < 10000 && pendingChars_ + line.size() <= 4 * 1024 * 1024) {
                        pending_.append(line);
                        pendingChars_ += line.size();
                        return;
                    }
                    if (source_ == Source::Live) {
                        ++dropped_;
                        return;
                    }
                }
                // Pause even inside a single large decoded PCAP record. File
                // replay is lossless; stop can always interrupt backpressure.
                QThread::msleep(10);
            }
        });
        const auto token = stopSource_.get_token();
        if (source_ == Source::Live) {
            UsbPcapBackend::capture(path_.toStdString(), token,
                [&decoder](std::span<const uint8_t> bytes) { decoder.feed(bytes); },
                [this] { emit ready(QStringLiteral("Capturing on %1").arg(path_)); });
            // Stopping can cancel a read in the middle of a record. An
            // intentionally cancelled live stream need not end at EOF.
        } else {
            QFile file(path_);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error(file.errorString().toStdString());
            emit ready(QStringLiteral("Reading %1").arg(path_));
            while (!token.stop_requested()) {
                const QByteArray bytes = file.read(64 * 1024);
                if (bytes.isEmpty()) {
                    if (file.error() != QFileDevice::NoError)
                        throw std::runtime_error(file.errorString().toStdString());
                    decoder.finish();
                    break;
                }
                decoder.feed(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                                      static_cast<size_t>(bytes.size())));
                // Offline replay can outrun painting; apply backpressure
                // instead of silently dropping entries in large files.
                while (!token.stop_requested()) {
                    { QMutexLocker lock(&mutex_); if (pending_.size() < 1000) break; }
                    QThread::msleep(10);
                }
            }
        }
    } catch (const std::exception& error) {
        emit failed(QString::fromUtf8(error.what()));
    }
}

} // namespace apdu
