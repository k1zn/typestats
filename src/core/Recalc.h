#pragma once

#include "KeyRecord.h"

#include <QVector>

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

// Pause value that marks the first element of a fragment.
constexpr float kFragmentStart = -2147483648.0f;

struct TextModel
{
    // The records the model is built from: the document's without leading releases and auto-repeated
    // modifiers (the first one gets dt = 60 s). Record indexes below refer to this vector.
    KeyRecords records;
    QVector<bool> recErased; // per record: a character later removed by BackSpace / Ctrl+BackSpace

    // One element per key press that made it into the text.
    QVector<QString> names;   // KeyDisplayName; a "character" is a name of length 1
    QVector<quint32> flags;   // flags of the source record
    QVector<int> recIndex;    // index of the source record
    QVector<float> pauses;    // ms since the previous element, or kFragmentStart
    bool erased(int i) const { return i >= 0 && i < recIndex.size() && recErased[recIndex[i]]; }
    bool isChar(int i) const { return names[i].size() == 1; }
    int size() const { return names.size(); }

    // The text: paragraphs separated by '\n', runs carry TextStyle flags (unstyled text has no run).
    QString text;
    QVector<TextRun> runs;

    // Parallel maps sorted by text position: position -> element / klavogram record / raw record.
    QVector<int> mapPos, mapElem, mapKlav, mapRec;

    QVector<KlavRecord> klav;
};

namespace Recalc {
// Builds the text model of a document; the document itself is not touched.
TextModel run(const KeyRecords &document, const RecalcOptions &opt);

// The steps of run(), for tests.
// Drops leading releases and auto-repeated modifier presses; sets dt of the first record to 60 s.
KeyRecords normalized(const KeyRecords &recs);
// Marks the characters removed by BackSpace / Ctrl+BackSpace (one flag per record).
QVector<bool> erasedRecords(const KeyRecords &recs);

// FUN_0040382c: index of the first element >= x.
int lowerBound(const QVector<int> &v, int x);
// FUN_0040373c: v[i], or last+1 past the end, 0 for an empty vector.
int at(const QVector<int> &v, int i);
}
