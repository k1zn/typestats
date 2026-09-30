#pragma once

#include "Recalc.h"

#include <QLocale>
#include <QStringList>

// Extra statistics window (Form3), port of 0x43ff3c; see re/extra_stats.md.

namespace ExtraStats {

// RadioGroup1.ItemIndex
enum Kind { Pairs, Triples, Quads, Words, WordsWithErrors, Sentences, Template, KindCount };

// "Фильтр по символам" (0x43fcac): three optional character sets.
struct CharFilter
{
    bool onlyOn = false, anyOn = false, excludeOn = false;
    QString only, any, exclude;
    bool pass(QStringView text) const;
};

// One position of a template.
struct TemplateItem
{
    enum Code : quint8 {
        RuLower, RuUpper, Any, Digit, Punct, Erased, Literal, Finger, Slash, Hand, LatLower = 10, LatUpper
    };
    Code code = Literal;
    QChar ch; // the template character the code came from
    bool operator==(const TemplateItem &) const = default;
};
QVector<TemplateItem> parseTemplate(QStringView pattern);

struct Occurrence
{
    float speed = 0; // characters per minute
    int pos = 0;     // element the occurrence starts at
    QString text;
};

// Everything of the given kind among elements [b, e) (Stats::range), in text order.
// fingers is fingerSeries() of the model; only templates use it.
QVector<Occurrence> collect(const TextModel &m, const QVector<quint8> &fingers, int b, int e, Kind kind,
                            const QString &pattern = {}, const CharFilter &filter = {});

// A row of the list: value is the start element, or the count with "Средние значения".
struct Row
{
    float speed = 0;
    int value = 0;
    QString text;
};

// Sort modes: 0 speed, 1 text, 2 count ascending, 3 count descending (2 and 3 need averages).
// The rows come in ascending order; `descending` is only the output order of the list.
QVector<Row> rows(const QVector<Occurrence> &occ, bool averages, int sortMode, bool descending = false);

// The lower list shown for a selected row in the averages mode: occurrences of text, slowest first.
QVector<Occurrence> occurrences(const QVector<Occurrence> &occ, const QString &text);

// State of the column-header sorting (TntListView1ColumnClick).
struct Sort
{
    int mode = 0;
    bool descending = false;
    void clickColumn(int column, bool averages);
    void setAverages(bool averages);
    // Column captions with the sort arrow on the current column.
    QStringList headers(bool averages, const QStringList &captions = {QStringLiteral("Скорость"),
                                                                      QStringLiteral("Текст"),
                                                                      QStringLiteral("Кол-во")}) const;
};

// Two decimals, halves rounded away from zero (FloatToStrF ffFixed of the original).
QString formatSpeed(float speed, const QLocale &loc);

// "Сохранить": header and rows as tab-separated text, in list order.
QString toText(const QVector<Row> &rows, bool averages, const QLocale &loc);

// Templates of the combo box: ExStats.ini, a plain list of lines (UTF-16 LE with BOM as the
// original writes it; UTF-8 and cp1251 are read too).
class TemplateList
{
public:
    explicit TemplateList(const QString &path = {});
    const QStringList &items() const { return m_items; }
    // Adds the template if it is new and saves. Returns false if it was there already.
    bool add(const QString &pattern);
    bool remove(const QString &pattern);

    static QStringList decode(const QByteArray &bytes);
    static QByteArray encode(const QStringList &items);

private:
    void save() const;
    QString m_path;
    QStringList m_items;
};

} // namespace ExtraStats
