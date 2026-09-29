#pragma once

#include "Recalc.h"

#include <QLocale>

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

// Presses visible on the klavogram: drawing time in [start, start + widthPx·1000 / zoom] µs with
// start = scrollMs·1000 − 10, at most maxRows of them (rows that fit in the list,
// LVM_GETCOUNTPERPAGE). zoom is px per ms (klavogram +0x6c, 0.25 by default, 0.04..300).
// widthPx < 0: no right edge. maxRows >= 0x7fffffff: all presses, no window at all.
QVector<KeyListRow> rows(const QVector<KlavRecord> &klav, float scrollMs, int widthPx, float zoom, int maxRows,
                         const QLocale &loc);
}
