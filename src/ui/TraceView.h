#pragma once

#include <QTableView>
#include "TraceModel.h"

namespace apdu {

// Table of decoded APDUs with a text filter. Copy (Ctrl+C) and the context
// menu use the same line format as Save.
class TraceView final : public QTableView {
    Q_OBJECT
public:
    explicit TraceView(TraceLimits limits = {}, QWidget* parent = nullptr);

    // Appends rows and trims the oldest ones to the limits. With follow, the
    // view shows the newest row. Otherwise the row at the top of the view
    // stays there while rows are added or trimmed above it.
    void appendRows(QList<TraceRow> rows, bool follow);
    void clearRows();
    void setFilterText(const QString& text);
    void setPlaceholderText(const QString& text);

    TraceModel& traceModel() { return *model_; }
    const TraceModel& traceModel() const { return *model_; }
    int shownRowCount() const;
    QString allText() const;   // Every row, oldest first.
    QString shownText() const; // Rows that pass the filter.
    QString selectedText() const;
    QString selectedHex() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QList<int> selectedSourceRows() const;

    TraceModel* model_;
    TraceFilterModel* filter_;
    QString placeholder_;
};

} // namespace apdu
