#include "TraceView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <algorithm>

namespace apdu {

TraceView::TraceView(TraceLimits limits, QWidget* parent)
    : QTableView(parent), model_(new TraceModel(limits, this)), filter_(new TraceFilterModel(model_, this)) {
    setModel(filter_);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setAlternatingRowColors(true);
    setShowGrid(false);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setCornerButtonEnabled(false);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    const QFontMetrics text(font());
    const QFontMetrics fixed(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    verticalHeader()->hide();
    // Fixed row heights keep scrolling fast with tens of thousands of rows.
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultSectionSize(std::max(text.height(), fixed.height()) + 6);

    auto* header = horizontalHeader();
    header->setStretchLastSection(true);
    header->setHighlightSections(false);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const auto width = [&text](const QString& sample) { return text.horizontalAdvance(sample) + 18; };
    setColumnWidth(TraceModel::Number, width(QStringLiteral("000000")));
    setColumnWidth(TraceModel::Time, fixed.horizontalAdvance(QStringLiteral("00:00:00.000")) + 18);
    setColumnWidth(TraceModel::Dir, width(QStringLiteral("Dir")));
    for (const auto column : {TraceModel::Bus, TraceModel::Device, TraceModel::Slot, TraceModel::Sequence})
        setColumnWidth(column, width(QStringLiteral("Device")));
    setColumnWidth(TraceModel::Length, width(QStringLiteral("Length")));
    setColumnWidth(TraceModel::Info, width(QStringLiteral("611C — Success, 28 more bytes available")));
}

void TraceView::appendRows(QList<TraceRow> rows, bool follow) {
    if (rows.isEmpty()) return;
    // Qt lays items out lazily. Without this, the top row and the scroll
    // range can be stale and scrolling would be clamped to the old range.
    executeDelayedItemsLayout();
    quint64 anchor = 0;
    if (!follow) {
        if (const auto top = indexAt(QPoint(0, 0)); top.isValid())
            anchor = model_->row(filter_->mapToSource(top).row()).id;
    }
    const int horizontal = horizontalScrollBar()->value();
    model_->append(std::move(rows));
    executeDelayedItemsLayout();
    if (follow) {
        scrollToBottom();
    } else if (anchor != 0) {
        const int row = model_->rowForId(anchor);
        if (row >= 0) scrollTo(filter_->mapFromSource(model_->index(row, 0)), QAbstractItemView::PositionAtTop);
        else scrollToTop(); // The anchored row itself was trimmed.
    }
    horizontalScrollBar()->setValue(horizontal);
}

void TraceView::clearRows() {
    model_->clear();
}

void TraceView::setFilterText(const QString& text) {
    filter_->setFilterText(text);
    viewport()->update();
}

void TraceView::setPlaceholderText(const QString& text) {
    placeholder_ = text;
    viewport()->update();
}

int TraceView::shownRowCount() const {
    return filter_->rowCount();
}

QString TraceView::allText() const {
    QStringList lines;
    lines.reserve(model_->rowCount());
    for (int row = 0; row < model_->rowCount(); ++row) lines.append(model_->text(row));
    return lines.join(QLatin1Char('\n'));
}

QString TraceView::shownText() const {
    QStringList lines;
    lines.reserve(filter_->rowCount());
    for (int row = 0; row < filter_->rowCount(); ++row)
        lines.append(model_->text(filter_->mapToSource(filter_->index(row, 0)).row()));
    return lines.join(QLatin1Char('\n'));
}

QList<int> TraceView::selectedSourceRows() const {
    QList<int> rows;
    for (const auto& index : selectionModel()->selectedRows())
        rows.append(filter_->mapToSource(index).row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

QString TraceView::selectedText() const {
    QStringList lines;
    for (const int row : selectedSourceRows()) lines.append(model_->text(row));
    return lines.join(QLatin1Char('\n'));
}

QString TraceView::selectedHex() const {
    QStringList lines;
    for (const int row : selectedSourceRows())
        lines.append(QString::fromLatin1(model_->row(row).data.toHex(' ').toUpper()));
    return lines.join(QLatin1Char('\n'));
}

void TraceView::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Copy)) {
        if (const auto text = selectedText(); !text.isEmpty()) QApplication::clipboard()->setText(text);
        event->accept();
        return;
    }
    QTableView::keyPressEvent(event);
}

void TraceView::contextMenuEvent(QContextMenuEvent* event) {
    const bool selected = selectionModel()->hasSelection();
    QMenu menu(this);
    auto* copyRows = menu.addAction(QStringLiteral("Copy rows"), this, [this] {
        QApplication::clipboard()->setText(selectedText());
    });
    copyRows->setShortcut(QKeySequence::Copy);
    copyRows->setEnabled(selected);
    menu.addAction(QStringLiteral("Copy APDU bytes (hex)"), this, [this] {
        QApplication::clipboard()->setText(selectedHex());
    })->setEnabled(selected);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Select all"), this, &QTableView::selectAll)->setEnabled(shownRowCount() > 0);
    menu.exec(event->globalPos());
}

void TraceView::paintEvent(QPaintEvent* event) {
    QTableView::paintEvent(event);
    if (shownRowCount() > 0) return;
    const auto message = model_->rowCount() > 0 ? QStringLiteral("No APDUs match the filter.") : placeholder_;
    if (message.isEmpty()) return;
    QPainter painter(viewport());
    painter.setPen(palette().placeholderText().color());
    painter.drawText(viewport()->rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, message);
}

} // namespace apdu
