#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QList>
#include <QSortFilterProxyModel>
#include <QString>
#include <deque>
#include "core/ApduInfo.h"
#include "core/TraceDecoder.h"

namespace apdu {

// One displayed APDU. The worker thread builds rows, including the time and
// info text, so the GUI thread only formats hex for the rows it paints.
struct TraceRow {
    quint64 id = 0; // Assigned by TraceModel: 1 for the first row after a clear.
    quint64 timestampUs = 0;
    quint16 bus = 0, device = 0;
    quint8 slot = 0, sequence = 0;
    Direction direction = Direction::Command;
    StatusKind status = StatusKind::Success; // Responses only.
    QByteArray data;
    QString time; // Local HH:mm:ss.zzz
    QString info; // Command name or status word meaning.
};

TraceRow makeTraceRow(const TraceEntry& entry);

// The exported line format, unchanged from the text view:
// [HH:mm:ss.zzz] bus 1 / device 5 / slot 0 / seq 7  >>  00 A4 04 00 00
QString formatTraceLine(const TraceRow& row);

struct TraceLimits {
    int maxRows = 20000;
    // Row limits alone allow gigabytes of long extended APDUs, so the APDU
    // bytes are bounded as well.
    qint64 maxBytes = 8 * 1024 * 1024;
};

class TraceModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Number, Time, Dir, Bus, Device, Slot, Sequence, Length, Info, Data, ColumnCount };
    static constexpr int maxDisplayBytes = 256;

    explicit TraceModel(TraceLimits limits = {}, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Appends rows, then trims the oldest rows to the limits. The newest row
    // is always kept. Returns the number of rows trimmed.
    int append(QList<TraceRow> rows);
    void clear();

    const TraceRow& row(int index) const { return rows_[static_cast<std::size_t>(index)]; }
    // Model row of a row id, or -1 when that row was trimmed or cleared.
    int rowForId(quint64 id) const;
    qint64 bytes() const { return bytes_; }
    // Hex bytes at byte boundaries ("9000" or "90 00") or info text, case-insensitive.
    bool matches(int index, const QString& filter) const;
    QString text(int index) const { return formatTraceLine(row(index)); }

private:
    TraceLimits limits_;
    std::deque<TraceRow> rows_;
    qint64 bytes_ = 0;
    quint64 nextId_ = 1;
};

class TraceFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TraceFilterModel(TraceModel* source, QObject* parent = nullptr);
    void setFilterText(const QString& text);
    const QString& filterText() const { return filter_; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    TraceModel* source_;
    QString filter_;
};

} // namespace apdu
