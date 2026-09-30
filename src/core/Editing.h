#pragma once

#include "Recalc.h"

#include <functional>

// Editing of a recording and copying of its text, see re/editing.md. Record indexes are those of
// TextModel::records: the normalized records the text was built from.
namespace Editing {

// FUN_004254b4: records covered by a selection of the text - from right after the record of the
// element before the selection up to the record of the element at its end. Nothing selected: {0, 0}.
std::pair<int, int> recordRange(const TextModel &m, int selStart, int selLength);

// "Удалить" (0x419a08): drops records [from, to) and the later releases of the presses dropped.
// Releases of earlier presses stay; the time of what is dropped goes to the next record kept.
void deleteRange(KeyRecords &recs, int from, int to);

// "Удалить нетекстовые клавиши" (0x428dd4): in [from, to) only characters, Shift and BackSpace
// stay; a Shift left with nothing typed under it goes too. Releases are matched as in deleteRange.
void removeNonText(KeyRecords &recs, int from, int to);

// FUN_00419e58: the first record of the marked run that record i closes (releases are skipped);
// -1 when i is not marked.
int labelStart(const KeyRecords &recs, int i);

// "Пометить" (0x419ed4): marks the presses in [from, to). The label text the run had (or the one of
// the next press, which is cleared) moves to the first record of the run. Returns that record, or
// -1 when there is none to carry a label.
int markRange(KeyRecords &recs, int from, int to);

// "Удалить метку" (0x41a70c) for the record under the mouse: a plain comment is cleared; a marked
// run is unmarked and loses its comments.
void removeLabel(KeyRecords &recs, int i);

// "Копировать" / "Копировать без ошибок": names of elements [b, e); an erased space is a space again.
QString copyText(const TextModel &m, int b, int e, bool skipErased);

// Characters of the key with this scan code in some layout, like ToUnicodeEx: n > 0 characters in
// `out` (the last one is typed), n < 0 a dead key (its accent in out[0]), 0 nothing.
using ToUnicode = std::function<int(quint8 scan, bool shift, bool caps, char16_t out[2])>;

// "Преобразовать в текущую раскладку" (0x429e80): the presses of [from, to) get the characters
// their keys give in another layout. Shift and CapsLock are followed by the records, CapsLock
// starting as `caps`; only ch and DeadKey change. VK_PACKET presses keep their characters.
void convertLayout(KeyRecords &recs, int from, int to, const ToUnicode &toUnicode, bool caps);

struct TagOptions
{
    bool color = true;     // CopyBlock1: corrections in red
    bool strike = true;    // CopyBlock2: corrections struck through
    bool colorNext = true; // CopyBlock3: the character after a correction in dark red
};
// "Копировать с тегами" (0x42a718): BB code for a blog.
QString copyTagged(const TextModel &m, int b, int e, const TagOptions &opt);

}
