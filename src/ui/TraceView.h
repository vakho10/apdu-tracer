#pragma once

#include <QStringList>

class QPlainTextEdit;

namespace apdu {

struct TraceLimits {
    int maxRows = 20000;
    // Row limits alone allow gigabytes of long extended APDUs, so the
    // document's UTF-16 text is bounded as well (8 Mi characters = 16 MiB).
    int maxCharacters = 8 * 1024 * 1024;
};

// Appends rows to a no-wrap trace view and trims the oldest complete rows to
// the limits. With follow, the view scrolls to the newest row. Otherwise the
// first visible row stays in place while older rows are trimmed above it.
void appendTraceRows(QPlainTextEdit& view, const QStringList& rows, bool follow,
                     const TraceLimits& limits = {});

} // namespace apdu
