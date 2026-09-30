// The core against what the original TypeStats.exe showed for the golden files.
// tests/golden/orig/<name>.json is written by re/scripts/diffstand.py (one entry per option set:
// ListView2, text, character styles, ListView2 for a number of selections, Form3 lists).

#include "core/ExtraStats.h"
#include "core/FingerZones.h"
#include "core/Graphs.h"
#include "core/Histograms.h"
#include "core/KeyList.h"
#include "core/MainStats.h"
#include "core/TsfFile.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cfloat>
#include <cmath>
#include <cstring>

namespace {

// RichEdit colours (COLORREF) of the original, see re/text_reconstruction.md.
constexpr int kBlue = 0xFF0000, kRed = 0x0000FF, kGreen = 0x008000;

QStringList rows(const QJsonArray &a)
{
    QStringList r;
    for (const QJsonValue &v : a) {
        const QJsonArray row = v.toArray();
        r << row[0].toString() + QLatin1Char('\t') + row[1].toString();
    }
    return r;
}

QStringList portRows(const TextModel &m, const RecalcOptions &opt, int selStart, int selLen)
{
    const auto [b, e] = Stats::range(m, selStart, selLen, opt.byPauses);
    const QStringList v = Stats::format(Stats::compute(m, b, e, opt.splitMs, opt.byPauses), QLocale(QLocale::Russian));
    const QStringList n = Stats::rowNames();
    QStringList r;
    for (int i = 0; i < n.size(); ++i)
        r << n[i] + QLatin1Char('\t') + v[i];
    return r;
}

// "color underline" per character, as the original's RichEdit shows it; paragraph marks are skipped.
QStringList portStyles(const TextModel &m)
{
    QVector<QPair<int, bool>> st(m.text.size(), {-1, false});
    for (const TextRun &r : m.runs) {
        int color = -1;
        if (r.style & (TextStyle::Separator | TextStyle::Injected))
            color = kBlue;
        else if (r.style & TextStyle::Comment)
            color = kGreen;
        else if (r.style & TextStyle::Erased)
            color = kRed;
        for (int p = r.start; p < r.start + r.length; ++p)
            st[p] = {color, bool(r.style & TextStyle::Marked)};
    }
    QStringList out;
    for (int p = 0; p < st.size(); ++p)
        out << (m.text[p] == QLatin1Char('\n') ? QStringLiteral("-1 0")
                                               : QStringLiteral("%1 %2").arg(st[p].first).arg(int(st[p].second)));
    return out;
}

QStringList origStyles(const QJsonArray &a)
{
    QStringList out;
    for (const QJsonValue &v : a) {
        const QJsonArray s = v.toArray();
        out << QStringLiteral("%1 %2").arg(s[0].isNull() ? -1 : s[0].toInt()).arg(int(s[1].toBool()));
    }
    return out;
}

QStringList lv1Rows(const QJsonArray &a)
{
    QStringList r;
    for (const QJsonValue &v : a) {
        QStringList cells;
        for (const QJsonValue &c : v.toArray())
            cells << c.toString();
        r << cells.join(QLatin1Char('|'));
    }
    return r;
}

QStringList portLv1(const TextModel &m, int selStart, int widthPx, float zoom, int maxRows)
{
    QStringList r;
    // What the main window of the original does (FUN_00437d98): the klavogram shows the drawing time
    // [start, start + width / zoom] with start = scroll − 10 µs, computed in single precision; zoom is
    // px per ms (0.25 by default, 0.04..300). The UI of the port has to pass the same span.
    const float start = KeyList::scrollForPosition(m, selStart) * 1000.0f - 10.0f;
    const float end = float(double(float(widthPx * 1000)) / double(zoom) + double(start));
    const auto rows = KeyList::rows(m.klav, QLocale(QLocale::Russian), double(start), double(end), maxRows);
    for (const KeyListRow &row : rows)
        r << row.pause + QLatin1Char('|') + row.duration + QLatin1Char('|') + row.key;
    return r;
}

// Float bits as the bench records them (exact comparison).
QStringList bits(const QVector<float> &v)
{
    QStringList r;
    for (float f : v) {
        quint32 u;
        std::memcpy(&u, &f, 4);
        r << QStringLiteral("%1").arg(u, 8, 16, QLatin1Char('0'));
    }
    return r;
}

QStringList bits(const QJsonArray &a)
{
    QStringList r;
    for (const QJsonValue &v : a)
        r << v.toString();
    return r;
}

// The original computes in 80-bit x87 precision and the core repeats that with long double. Where
// long double is narrower (MSVC, arm64) the last bits of a float may differ, so there the values
// are compared with a tolerance instead of bit by bit.
constexpr bool kExactFloats = LDBL_MANT_DIG == 64;

bool sameFloats(const QStringList &got, const QStringList &want)
{
    if (kExactFloats || got.size() != want.size())
        return got == want;
    for (qsizetype i = 0; i < got.size(); ++i) {
        const quint32 ua = got[i].toUInt(nullptr, 16), ub = want[i].toUInt(nullptr, 16);
        float a, b;
        std::memcpy(&a, &ua, 4);
        std::memcpy(&b, &ub, 4);
        if (std::fabs(a - b) > 1e-4f * std::max({std::fabs(a), std::fabs(b), 1.0f}))
            return false;
    }
    return true;
}

} // namespace

class TstOrig : public QObject
{
    Q_OBJECT
private slots:
    void matchesOriginal_data()
    {
        QTest::addColumn<QString>("tsf");
        QTest::addColumn<QJsonObject>("variant");
        const QDir dir(QStringLiteral(TS_GOLDEN_DIR "/orig"));
        for (const QString &f : dir.entryList({QStringLiteral("*.json")})) {
            QFile file(dir.filePath(f));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonArray variants = QJsonDocument::fromJson(file.readAll()).object()[QLatin1String("variants")].toArray();
            const QString tsf = QStringLiteral(TS_GOLDEN_DIR "/") + QFileInfo(f).completeBaseName() + QLatin1String(".tsf");
            for (const QJsonValue &v : variants) {
                const QJsonObject o = v[QLatin1String("options")].toObject();
                const QString tag = QStringLiteral("%1 pause=%2 textOnly=%3 byPauses=%4")
                                        .arg(f).arg(o[QLatin1String("Pause")].toInt())
                                        .arg(o[QLatin1String("TextOnly")].toBool())
                                        .arg(o[QLatin1String("SplitOnEnter")].toBool());
                QTest::newRow(qPrintable(tag)) << tsf << v.toObject();
            }
        }
    }

    void matchesOriginal()
    {
        QFETCH(QString, tsf);
        QFETCH(QJsonObject, variant);
        const QJsonObject o = variant[QLatin1String("options")].toObject();
        RecalcOptions opt;
        opt.splitMs = o[QLatin1String("Pause")].toInt();
        opt.onlyText = o[QLatin1String("TextOnly")].toBool();
        opt.byPauses = o[QLatin1String("SplitOnEnter")].toBool();
        TsfDocument d;
        QCOMPARE(Tsf::read(tsf, d), Tsf::ReadError::None);
        const TextModel m = Recalc::run(d.records, opt);

        // The original's RichEdit is ANSI: U+2588 (erased space) shows as '-' there.
        QString text = m.text;
        text.replace(QChar(0x2588), QLatin1Char('-'));
        QCOMPARE(text, variant[QLatin1String("text")].toString());
        QCOMPARE(portRows(m, opt, 0, 0), rows(variant[QLatin1String("stats")].toArray()));
        // Rows that fit in ListView1 (recorded by newer diffstand runs; else the longest list seen).
        int maxRows = variant[QLatin1String("lv1_rows")].toInt();
        if (maxRows == 0) {
            maxRows = variant[QLatin1String("lv1")].toArray().size();
            for (const QJsonValue &s : variant[QLatin1String("selections")].toArray())
                maxRows = std::max<int>(maxRows, s[QLatin1String("lv1")].toArray().size());
        }
        // Klavogram bitmap width and zoom (older recordings: 652 px and the default 0.25 px/ms).
        const int widthPx = variant[QLatin1String("klav_width")].toInt(652);
        const float zoom = float(variant[QLatin1String("klav_zoom")].toDouble(0.25));
        QCOMPARE(portLv1(m, 0, widthPx, zoom, maxRows), lv1Rows(variant[QLatin1String("lv1")].toArray()));
        if (variant.contains(QLatin1String("series"))) {
            const QJsonObject series = variant[QLatin1String("series")].toObject();
            // The original marks the first element of a fragment with -2^31 in every series.
            GraphSeries g = Graphs::compute(m);
            for (QVector<float> *s : {&g.pause, &g.curSpeed, &g.medSpeed, &g.classicSpeed, &g.privSpeed,
                                      &g.curRhythm, &g.medRhythm, &g.arrhythmia})
                for (int start : m.fragmentStarts)
                    (*s)[start] = -2147483648.0f;
            const QList<QPair<const char *, const QVector<float> *>> all = {
                {"pause", &g.pause}, {"curSpeed", &g.curSpeed}, {"medSpeed", &g.medSpeed},
                {"classicSpeed", &g.classicSpeed}, {"privSpeed", &g.privSpeed}, {"curRhythm", &g.curRhythm},
                {"medRhythm", &g.medRhythm}, {"arrhythmia", &g.arrhythmia}};
            for (const auto &[name, v] : all) {
                const QStringList want = bits(series[QLatin1String(name)].toArray());
                if (!sameFloats(bits(*v), want)) {
                    qWarning() << "series" << name;
                    QCOMPARE(bits(*v), want);
                }
            }
            // Finger of every element with the built-in scheme (the golden files store none).
            QVector<float> finger;
            for (quint8 f : fingerSeries(m, FingerZones::standard()))
                finger << f;
            QCOMPARE(bits(finger), bits(series[QLatin1String("finger")].toArray()));
        }
        // Form3: the row vector from the original's memory (exact float bits) and the list as shown.
        const QJsonArray extra = variant[QLatin1String("extra")].toArray();
        for (qsizetype xi = 0; xi < extra.size(); ++xi) {
            const QJsonObject x = extra[xi].toObject();
            const auto kind = ExtraStats::Kind(x[QLatin1String("kind")].toInt());
            const bool averages = x[QLatin1String("averages")].toBool();
            const QJsonObject fo = x[QLatin1String("filter")].toObject();
            ExtraStats::CharFilter filter;
            filter.onlyOn = fo.contains(QLatin1String("only"));
            filter.only = fo[QLatin1String("only")].toString();
            filter.anyOn = fo.contains(QLatin1String("any"));
            filter.any = fo[QLatin1String("any")].toString();
            filter.excludeOn = fo.contains(QLatin1String("exclude"));
            filter.exclude = fo[QLatin1String("exclude")].toString();
            const QJsonArray sel = x[QLatin1String("sel")].toArray();
            const auto [b, e] = Stats::range(m, sel[0].toInt(), sel[1].toInt(), opt.byPauses);
            const auto occ = ExtraStats::collect(m, fingerSeries(m, FingerZones::standard()), b, e, kind,
                                                 x[QLatin1String("pattern")].toString(), filter);
            ExtraStats::Sort sort{x[QLatin1String("sort")].toInt(), x[QLatin1String("desc")].toBool()};
            const QLocale ru(QLocale::Russian);
            QStringList got, want, gotLv, gotSpeed, wantSpeed;
            for (const ExtraStats::Row &r : ExtraStats::rows(occ, averages, sort.mode)) {
                gotSpeed << bits(QVector<float>{r.speed});
                got << QString::number(r.value) + QLatin1Char('|') + r.text;
            }
            for (const QJsonValue &v : x[QLatin1String("rows")].toArray()) {
                wantSpeed << v[0].toString();
                want << QString::number(v[1].toInt()) + QLatin1Char('|') + v[2].toString();
            }
            for (const ExtraStats::Row &r : ExtraStats::rows(occ, averages, sort.mode, sort.descending)) {
                gotLv << ExtraStats::formatSpeed(r.speed, ru) + QLatin1Char('|') + r.text
                             + (averages ? QLatin1Char('|') + QString::number(r.value) : QString());
            }
            if (got != want || !sameFloats(gotSpeed, wantSpeed) || gotLv != lv1Rows(x[QLatin1String("lv")].toArray()))
                qWarning() << "extra" << xi << x[QLatin1String("kind")].toInt() << x[QLatin1String("pattern")].toString();
            QCOMPARE(got, want);
            if (!sameFloats(gotSpeed, wantSpeed))
                QCOMPARE(gotSpeed, wantSpeed);
            QCOMPARE(gotLv, lv1Rows(x[QLatin1String("lv")].toArray()));
            QCOMPARE(sort.headers(averages), bits(x[QLatin1String("headers")].toArray()));
            if (x.contains(QLatin1String("occ"))) {
                const QJsonObject o = x[QLatin1String("occ")].toObject();
                QStringList lower;
                for (const ExtraStats::Occurrence &oc : ExtraStats::occurrences(occ, o[QLatin1String("text")].toString()))
                    lower << ExtraStats::formatSpeed(oc.speed, ru) + QLatin1Char('|') + oc.text;
                QCOMPARE(lower, lv1Rows(o[QLatin1String("lv")].toArray()));
            }
        }
        // Form4: bars of the page on top of the stack, read from the original's memory.
        const QJsonArray hist = variant[QLatin1String("hist")].toArray();
        for (qsizetype hi = 0; hi < hist.size(); ++hi) {
            const QJsonObject h = hist[hi].toObject();
            const QString kind = h[QLatin1String("kind")].toString();
            Histograms::Node node;
            node.key = quint8(h[QLatin1String("key")].toInt());
            node.prevKey = quint8(h[QLatin1String("prevKey")].toInt());
            node.finger = h[QLatin1String("finger")].toInt();
            if (kind.startsWith(QLatin1String("relation"))) {
                node.kind = Histograms::Node::FingerRelation;
                node.relation = kind.right(1).toInt();
            } else {
                node.kind = Histograms::Node::Kind(QStringList{QStringLiteral("allKeys"), QStringLiteral("key"),
                                                               QStringLiteral("pair"), QStringLiteral("allFingers"),
                                                               QStringLiteral("finger")}.indexOf(kind));
            }
            const QJsonArray sel = h[QLatin1String("sel")].toArray();
            const auto [b, e] = Stats::range(m, sel[0].toInt(), sel[1].toInt(), opt.byPauses);
            Histograms::Source src;
            src.model = &m;
            std::tie(src.recBegin, src.recEnd) = Histograms::recordRange(m, b, e);
            src.splitUs = quint32(opt.splitMs) * 1000u;
            src.zones = FingerZones::standard();
            // The original names keys by the keyboard layout of its window; the recordings were made
            // with the layout the text was typed in, so the names agree.
            src.label = Histograms::labelsFromRecords(m.records);
            const Histograms::Page page = Histograms::build(src, node);
            QVector<float> values;
            QStringList names, counts, keys, recs;
            for (const Histograms::Bar &bar : page.bars) {
                values << bar.value;
                names << bar.label;
                if (bar.count >= 0)
                    counts << QString::number(bar.count);
                keys << QString::number(bar.key);
                recs << QString::number(bar.rec);
            }
            auto ints = [](const QJsonValue &a) {
                QStringList r;
                for (const QJsonValue &v : a.toArray())
                    r << QString::number(v.toInt());
                return r;
            };
            if (!sameFloats(bits(values), bits(h[QLatin1String("values")].toArray()))) {
                qWarning() << "hist" << hi << kind << h[QLatin1String("path")].toArray();
                QCOMPARE(bits(values), bits(h[QLatin1String("values")].toArray()));
            }
            QCOMPARE(names, bits(h[QLatin1String("names")].toArray()));
            QCOMPARE(counts, ints(h[QLatin1String("counts")]));
            if (h.contains(QLatin1String("keys")))
                QCOMPARE(keys, ints(h[QLatin1String("keys")]));
            if (h.contains(QLatin1String("recs")))
                QCOMPARE(recs, ints(h[QLatin1String("recs")]));
            // The page the recorded double click led to.
            const QJsonArray path = h[QLatin1String("path")].toArray();
            if (hi + 1 < hist.size() && path.size() < 3) {
                const QJsonArray next = hist[hi + 1][QLatin1String("path")].toArray();
                if (next.size() == path.size() + 1) {
                    const auto to = Histograms::drill(node, page, next.last().toInt());
                    QVERIFY(to);
                    const QJsonObject nh = hist[hi + 1].toObject();
                    if (nh.contains(QLatin1String("key")))
                        QCOMPARE(int(to->key), nh[QLatin1String("key")].toInt());
                    if (nh.contains(QLatin1String("prevKey")))
                        QCOMPARE(int(to->prevKey), nh[QLatin1String("prevKey")].toInt());
                    if (nh.contains(QLatin1String("finger")))
                        QCOMPARE(to->finger, nh[QLatin1String("finger")].toInt());
                }
            }
        }
        if (variant.contains(QLatin1String("styles")))
            QCOMPARE(portStyles(m), origStyles(variant[QLatin1String("styles")].toArray()));
        for (const QJsonValue &s : variant[QLatin1String("selections")].toArray()) {
            const int start = s[QLatin1String("start")].toInt(), len = s[QLatin1String("length")].toInt();
            const QStringList got = portRows(m, opt, start, len);
            const QStringList want = rows(s[QLatin1String("stats")].toArray());
            if (got != want)
                qWarning() << "selection" << start << len;
            QCOMPARE(got, want);
            const QStringList lv1 = portLv1(m, start, widthPx, zoom, maxRows);
            if (lv1 != lv1Rows(s[QLatin1String("lv1")].toArray()))
                qWarning() << "ListView1, selection" << start << len;
            QCOMPARE(lv1, lv1Rows(s[QLatin1String("lv1")].toArray()));
        }
    }
};

QTEST_GUILESS_MAIN(TstOrig)
#include "tst_orig.moc"
