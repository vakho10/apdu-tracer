#include "TraceView.h"

#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>

namespace apdu {

void appendTraceRows(QPlainTextEdit& view, const QStringList& rows, bool follow,
                     const TraceLimits& limits) {
    if (rows.isEmpty()) return;
    auto* scrollBar = view.verticalScrollBar();
    // In no-wrap mode the scroll bar counts rows: its value is the first
    // visible row. Both limits are applied here, not by setMaximumBlockCount,
    // so the number of rows removed above the view is known exactly.
    const int firstVisible = scrollBar->value();
    view.appendPlainText(rows.join(QLatin1Char('\n')));

    auto* document = view.document();
    int removedRows = 0;
    int removedCharacters = 0;
    auto block = document->begin();
    while ((document->blockCount() - removedRows > limits.maxRows ||
            document->characterCount() - removedCharacters > limits.maxCharacters) &&
           block.next().isValid()) {
        removedCharacters += block.length();
        ++removedRows;
        block = block.next();
    }
    if (removedCharacters > 0) {
        QTextCursor cursor(document);
        cursor.setPosition(removedCharacters, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    scrollBar->setValue(follow ? scrollBar->maximum() : std::max(0, firstVisible - removedRows));
}

} // namespace apdu
