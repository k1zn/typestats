#pragma once

#include "KeyRecord.h"

#include <QVector>

#include <algorithm>
#include <utility>

// Port of Recalculate (0x40ce40), see re/text_reconstruction.md.

struct RecalcOptions
{
    int splitMs = 2000;        // UpDown1 "Пауза разбиения", 200..10000; registry "Pause", default 2000
    bool onlyText = true;      // CheckBox2 "Только текст"; registry "TextOnly", default 1
    bool byPauses = false;     // CheckBox3 "Разбивать по паузам"; registry "SplitOnEnter", default 0
    bool onlyInjected = false; // CheckBox6 "Только PCmo" (hidden in the original)
};

namespace TextStyle {
enum : quint8 { Erased = 0x01, Marked = 0x02, Separator = 0x04, Injected = 0x08, Comment = 0x10 };
}

struct TextRun
{
    int start = 0;
    int length = 0;
    quint8 style = 0;
};

// One press or release on the klavogram.
struct KlavRecord
{
    qint64 t = 0;      // absolute time, µs
    qint64 tSeg = 0;   // time since the last record before the current fragment
    qint64 tDraw = 0;  // drawing time: the gap before every fragment is squeezed to 200 ms
    quint32 flags = 0; // record flags
    char16_t ch = 0;
    bool down = false;
    bool erased = false;        // a press whose character was later removed
    bool fragmentStart = false; // the first press after a split pause
};

struct TextAnchor
{
    int pos = 0;  // position in the text
    int elem = 0; // text element
    int klav = 0; // klavogram record
    int rec = 0;  // record
};

struct TextModel
{
    // Different for every Recalc::run (copies share it): what is computed from a model can be kept
    // while the serial stays the same.
    quint64 serial = 0;

    // The records the model is built from: the document's without leading releases and auto-repeated
    // modifiers (the first one gets dt = 60 s). Record indexes below refer to this vector.
    KeyRecords records;
    QVector<bool> recErased; // per record: a character later removed by BackSpace / Ctrl+BackSpace

    // One element per key press that made it into the text.
    QVector<QString> names;   // KeyDisplayName; a "character" is a name of length 1
    QVector<quint32> flags;   // flags of the source record
    QVector<int> recIndex;    // index of the source record
    QVector<float> pauses;    // ms since the previous element
    bool erased(int i) const { return i >= 0 && i < recIndex.size() && recErased[recIndex[i]]; }
    bool isChar(int i) const { return names[i].size() == 1; }
    int size() const { return names.size(); }

    // Fragments: runs of elements typed without a pause longer than the split pause. Sorted first
    // elements; the pause of such an element is the gap between fragments and belongs to neither.
    QVector<int> fragmentStarts;
    bool startsFragment(int i) const;
    // [begin, end) of the fragment with element i; i == size() gives the last one.
    std::pair<int, int> fragmentAt(int i) const;

    // The text: paragraphs separated by '\n', runs carry TextStyle flags (unstyled text has no run).
    QString text;
    QVector<TextRun> runs;

    QVector<KlavRecord> klav;

    // Points where a text position, an element, a klavogram record and a record line up; every
    // field is non-decreasing. The lookups take the first anchor at or after the argument and give
    // the end (one past the last anchor) when there is none.
    QVector<TextAnchor> anchors;
    int elementAt(int pos) const { return std::min(lookup(&TextAnchor::pos, pos, &TextAnchor::elem), size()); }
    int klavAt(int pos) const { return lookup(&TextAnchor::pos, pos, &TextAnchor::klav); }
    int recordAt(int pos) const { return lookup(&TextAnchor::pos, pos, &TextAnchor::rec); }
    int klavOfElement(int elem) const { return lookup(&TextAnchor::elem, elem, &TextAnchor::klav); }
    int recordOfElement(int elem) const { return lookup(&TextAnchor::elem, elem, &TextAnchor::rec); }
    int elementOfRecord(int rec) const { return std::min(lookup(&TextAnchor::rec, rec, &TextAnchor::elem), size()); }
    int elementOfKlav(int klav) const { return std::min(lookup(&TextAnchor::klav, klav, &TextAnchor::elem), size()); }
    int positionOfElement(int elem) const { return std::min<int>(lookup(&TextAnchor::elem, elem, &TextAnchor::pos), text.size()); }

private:
    int lookup(int TextAnchor::*key, int x, int TextAnchor::*value) const;
};

namespace Recalc {
// Builds the text model of a document; the document itself is not touched.
TextModel run(const KeyRecords &document, const RecalcOptions &opt);

// The steps of run(), for tests.
// Drops leading releases and auto-repeated modifier presses; sets dt of the first record to 60 s.
KeyRecords normalized(const KeyRecords &recs);
// Marks the characters removed by BackSpace / Ctrl+BackSpace (one flag per record).
QVector<bool> erasedRecords(const KeyRecords &recs);
}
