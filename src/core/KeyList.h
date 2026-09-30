#pragma once

#include "Recalc.h"

#include <QLocale>

#include <limits>

// ListView1 of the main window (Пауза / Длительность / Клавиша): the presses visible on the
// klavogram. Port of FUN_00437d98 + FUN_00404c44, see re/text_reconstruction.md.

struct KeyListRow
{
    QString pause;    // ms since the previous listed press; empty for the first row and above 59 s
    QString duration; // hold time, ms; empty if the release is not reached
    QString key;      // KeyDisplayName, "\r" as "[Enter]"
};

namespace KeyList {
// FUN_00414500: klavogram scroll position (ms of drawing time) after the caret moved to text
// position selStart: the element there is put at the left edge, 0.1 ms in.
float scrollForPosition(const TextModel &m, int selStart);

// Presses with drawing time (KlavRecord::tDraw, µs) in [fromUs, toUs], at most `limit` of them
// (negative: all). The main window passes the time span visible on the klavogram and the number of
// rows that fit in the list.
QVector<KeyListRow> rows(const QVector<KlavRecord> &klav, const QLocale &loc,
                         double fromUs = -std::numeric_limits<double>::infinity(),
                         double toUs = std::numeric_limits<double>::infinity(), int limit = -1);
}
