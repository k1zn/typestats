#pragma once

#include "ExtraStats.h"
#include "FingerZones.h"
#include "Recalc.h"

#include <QLocale>
#include <QStringList>
#include <functional>
#include <optional>

// Statistical histograms window (Form4), see re/histograms.md.
// The window shows one page of bars; double-clicking a bar drills down into a more detailed page.

namespace Histograms {

// Texts of the pages (translatable; the defaults are the original's).
struct Names
{
    QString allKeys = QStringLiteral("Все клавиши");
    QString key = QStringLiteral("Клавиша");
    QString pairs = QStringLiteral("Длительности сочетаний");
    QString allFingers = QStringLiteral("Все пальцы");
    // Bars of a finger page: how the press relates to the previous one.
    QStringList relations{QStringLiteral("Клавиша"), QStringLiteral("Палец"), QStringLiteral("Рука"),
                          QStringLiteral("Прочее")};
    // Titles of the pages behind those bars.
    QStringList relationTitles{QStringLiteral("Двойное нажатие на клавишу"), QStringLiteral("Разные клавиши"),
                               QStringLiteral("Та же рука (другой палец)"), QStringLiteral("Другая рука")};
    QStringList fingersShort{QStringLiteral("ЛМ"), QStringLiteral("ЛБ"), QStringLiteral("ЛС"), QStringLiteral("ЛУ"),
                             QStringLiteral("ПУ"), QStringLiteral("ПС"), QStringLiteral("ПБ"), QStringLiteral("ПМ"),
                             QStringLiteral("Прочие")};
    QStringList fingers{QStringLiteral("Левый мизинец"), QStringLiteral("Левый безымянный"),
                        QStringLiteral("Левый средний"), QStringLiteral("Левый указательный"),
                        QStringLiteral("Правый указательный"), QStringLiteral("Правый средний"),
                        QStringLiteral("Правый безымянный"), QStringLiteral("Правый мизинец"),
                        QStringLiteral("Остальные клавиши")};
    QString extra = QStringLiteral("Дополнительная статистика");
};

// What a page shows.
struct Node
{
    enum Kind { AllKeys, Key, Pair, AllFingers, Finger, FingerRelation, Extra };
    Kind kind = AllKeys;
    quint8 key = 0;     // Key, Pair: scan code (low byte of the record flags)
    quint8 prevKey = 0; // Pair: the key pressed before it
    int finger = 0;     // Finger, FingerRelation: 0..8
    int relation = 0;   // FingerRelation: 0 same key, 1 same finger, 2 same hand, 3 other hand
    bool operator==(const Node &) const = default;
};

struct Bar
{
    float value = 0; // ms (pause before the press), or the speed for Node::Extra
    QString label;
    int count = -1;  // presses behind an average; -1 if the bar is a single press
    quint8 key = 0;  // AllKeys, Key: the key of the bar
    int rec = -1;    // single presses: record of the previous press (where a double click goes)
};

struct Page
{
    QString title;
    QVector<Bar> bars;
};

// Name of a key by its scan code.
using KeyLabel = std::function<QString(quint8 key)>;
// Labels taken from the recording itself: the character the key produced most often.
KeyLabel labelsFromRecords(const KeyRecords &recs);

struct Source
{
    const KeyRecords *recs = nullptr; // after Recalc::run
    int recBegin = 0, recEnd = 0;     // recordRange()
    quint32 splitUs = 2000000;        // RecalcOptions::splitMs * 1000
    FingerZones zones;
    KeyLabel label;
    Names names;
};

// 0x44b91c: records of the elements [b, e) (Stats::range).
std::pair<int, int> recordRange(const TextModel &m, int b, int e);

// Any page but Node::Extra.
Page build(const Source &src, const Node &node);
// The list of the extra statistics window as bars, in list order.
Page fromExtra(const QVector<ExtraStats::Row> &rows, const Names &names = {});

// The page a double click on bar `index` opens; nothing for pages of single presses
// (there the click scrolls to elementOf(bar.rec)) and for Node::Extra (selects the row of the list).
std::optional<Node> drill(const Node &node, const Page &page, int index);
// 0x44d0dc: text element of a record.
int elementOf(const TextModel &m, int rec);

// Hint of a bar: "12,345 (7) label".
QString hint(const Bar &bar, const QLocale &loc);

} // namespace Histograms
