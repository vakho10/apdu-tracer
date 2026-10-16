#include "TraceModel.h"

#include <QColor>
#include <QDateTime>
#include <QFontDatabase>
#include <algorithm>

namespace apdu {
namespace {

QString hex(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

bool isHexFilter(const QString& text) {
    return !text.isEmpty() && std::all_of(text.begin(), text.end(), [](QChar c) {
        return c.isDigit() || (c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'));
    });
}

} // namespace

TraceRow makeTraceRow(const TraceEntry& entry) {
    TraceRow row;
    row.timestampUs = entry.timestampUs;
    row.bus = entry.bus;
    row.device = entry.device;
    row.slot = entry.slot;
    row.sequence = entry.sequence;
    row.direction = entry.direction;
    row.data = QByteArray(reinterpret_cast<const char*>(entry.data.data()),
                          static_cast<qsizetype>(entry.data.size()));
    row.time = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(entry.timestampUs / 1000))
                   .toString(QStringLiteral("HH:mm:ss.zzz"));
    if (entry.direction == Direction::Response) {
        if (const auto status = responseStatus(entry.data)) {
            row.status = status->kind;
            row.info = QStringLiteral("%1 — %2")
                           .arg(QString::asprintf("%04X", status->word), QString::fromStdString(status->text));
        }
    } else if (const auto name = commandName(entry.data); !name.empty()) {
        row.info = QString::fromStdString(name);
    } else if (entry.data.size() >= 4) {
        row.info = QString::asprintf("INS %02X", entry.data[1]);
    }
    return row;
}

QString formatTraceLine(const TraceRow& row) {
    return QStringLiteral("[%1] bus %2 / device %3 / slot %4 / seq %5  %6  %7")
        .arg(row.time).arg(row.bus).arg(row.device).arg(row.slot).arg(row.sequence)
        .arg(row.direction == Direction::Command ? QStringLiteral(">>") : QStringLiteral("<<"))
        .arg(hex(row.data));
}

TraceModel::TraceModel(TraceLimits limits, QObject* parent)
    : QAbstractTableModel(parent), limits_(limits) {}

int TraceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int TraceModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TraceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const auto& r = row(index.row());
    const bool command = r.direction == Direction::Command;
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Number: return r.id;
        case Time: return r.time;
        case Dir: return command ? QStringLiteral(">>") : QStringLiteral("<<");
        case Bus: return r.bus;
        case Device: return r.device;
        case Slot: return r.slot;
        case Sequence: return r.sequence;
        case Length: return r.data.size();
        case Info: return r.info;
        case Data:
            // Long extended APDUs would make painting slow; Copy and Save
            // always use all bytes.
            if (r.data.size() <= maxDisplayBytes) return hex(r.data);
            return hex(r.data.first(maxDisplayBytes)) + QStringLiteral(" …");
        }
        break;
    case Qt::ToolTipRole:
        switch (index.column()) {
        case Time:
            return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(r.timestampUs / 1000))
                       .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                + QString::asprintf("%03u", static_cast<unsigned>(r.timestampUs % 1000));
        case Dir: return command ? QStringLiteral("Sent to the card") : QStringLiteral("Received from the card");
        case Info: return r.info;
        case Data: {
            constexpr qsizetype tooltipBytes = 512;
            QStringList lines;
            for (qsizetype offset = 0; offset < std::min(r.data.size(), tooltipBytes); offset += 16)
                lines.append(hex(r.data.mid(offset, 16)));
            if (r.data.size() > tooltipBytes)
                lines.append(QStringLiteral("… %1 bytes in total").arg(r.data.size()));
            return lines.join(QLatin1Char('\n'));
        }
        }
        break;
    case Qt::ForegroundRole:
        if (index.column() == Info && !command) {
            if (r.status == StatusKind::Error) return QColor(0xd3, 0x2f, 0x2f);
            if (r.status == StatusKind::Warning) return QColor(0xc7, 0x7c, 0x02);
        }
        break;
    case Qt::FontRole:
        if (index.column() == Data || index.column() == Time)
            return QFontDatabase::systemFont(QFontDatabase::FixedFont);
        break;
    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case Number: case Bus: case Device: case Slot: case Sequence: case Length:
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        case Dir:
            return QVariant::fromValue(Qt::AlignCenter);
        }
        break;
    }
    return {};
}

QVariant TraceModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case Number: return QStringLiteral("#");
        case Time: return QStringLiteral("Time");
        case Dir: return QStringLiteral("Dir");
        case Bus: return QStringLiteral("Bus");
        case Device: return QStringLiteral("Device");
        case Slot: return QStringLiteral("Slot");
        case Sequence: return QStringLiteral("Seq");
        case Length: return QStringLiteral("Length");
        case Info: return QStringLiteral("Info");
        case Data: return QStringLiteral("Data");
        }
    } else if (role == Qt::ToolTipRole) {
        switch (section) {
        case Dir: return QStringLiteral(">> sent to the card, << received from the card");
        case Sequence: return QStringLiteral("CCID sequence number of the command's first fragment");
        case Length: return QStringLiteral("APDU length in bytes");
        case Info: return QStringLiteral("Command name, or the meaning of the response status word");
        case Data: return QStringLiteral("APDU bytes. Long APDUs are shortened here; Copy and Save keep all bytes.");
        }
    }
    return {};
}

int TraceModel::append(QList<TraceRow> rows) {
    if (rows.isEmpty()) return 0;
    const auto first = static_cast<int>(rows_.size());
    beginInsertRows({}, first, first + static_cast<int>(rows.size()) - 1);
    for (auto& r : rows) {
        r.id = nextId_++;
        bytes_ += r.data.size();
        rows_.push_back(std::move(r));
    }
    endInsertRows();

    int removed = 0;
    qint64 removedBytes = 0;
    const auto count = static_cast<int>(rows_.size());
    while (count - removed > 1 &&
           (count - removed > limits_.maxRows || bytes_ - removedBytes > limits_.maxBytes)) {
        removedBytes += rows_[static_cast<std::size_t>(removed)].data.size();
        ++removed;
    }
    if (removed > 0) {
        beginRemoveRows({}, 0, removed - 1);
        rows_.erase(rows_.begin(), rows_.begin() + removed);
        bytes_ -= removedBytes;
        endRemoveRows();
    }
    return removed;
}

void TraceModel::clear() {
    beginResetModel();
    rows_.clear();
    bytes_ = 0;
    nextId_ = 1;
    endResetModel();
}

int TraceModel::rowForId(quint64 id) const {
    if (rows_.empty() || id < rows_.front().id || id > rows_.back().id) return -1;
    return static_cast<int>(id - rows_.front().id);
}

bool TraceModel::matches(int index, const QString& filter) const {
    if (filter.isEmpty()) return true;
    const auto& r = row(index);
    if (r.info.contains(filter, Qt::CaseInsensitive)) return true;
    auto compact = filter;
    compact.remove(QLatin1Char(' '));
    if (!isHexFilter(compact)) return false;
    const auto needle = compact.toLower().toLatin1();
    const auto haystack = r.data.toHex();
    // Whole bytes only, so "09 00" does not match the middle of "90 00".
    // An odd number of digits is a nibble search and may start anywhere.
    for (auto at = haystack.indexOf(needle); at >= 0; at = haystack.indexOf(needle, at + 1))
        if (at % 2 == 0 || needle.size() % 2 != 0) return true;
    return false;
}

TraceFilterModel::TraceFilterModel(TraceModel* source, QObject* parent)
    : QSortFilterProxyModel(parent), source_(source) {
    setSourceModel(source);
}

void TraceFilterModel::setFilterText(const QString& text) {
    const auto trimmed = text.trimmed();
    if (trimmed == filter_) return;
    filter_ = trimmed;
    invalidateFilter();
}

bool TraceFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    return source_->matches(sourceRow, filter_);
}

} // namespace apdu
