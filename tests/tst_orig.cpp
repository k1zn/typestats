// The core against what the original TypeStats.exe showed for the golden files.
// tests/golden/orig/<name>.json is written by re/scripts/diffstand.py (one entry per option set:
// ListView2, text, character styles, and ListView2 for a number of selections).

#include "core/Graphs.h"
#include "core/KeyList.h"
#include "core/MainStats.h"
#include "core/TsfFile.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

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
    const auto rows = KeyList::rows(m.klav, KeyList::scrollForPosition(m, selStart), widthPx, zoom, maxRows,
                                    QLocale(QLocale::Russian));
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
            const GraphSeries g = Graphs::compute(m.pauses);
            const QList<QPair<const char *, const QVector<float> *>> all = {
                {"pause", &g.pause}, {"curSpeed", &g.curSpeed}, {"medSpeed", &g.medSpeed},
                {"classicSpeed", &g.classicSpeed}, {"privSpeed", &g.privSpeed}, {"curRhythm", &g.curRhythm},
                {"medRhythm", &g.medRhythm}, {"arrhythmia", &g.arrhythmia}};
            for (const auto &[name, v] : all) {
                const QStringList want = bits(series[QLatin1String(name)].toArray());
                if (bits(*v) != want)
                    qWarning() << "series" << name;
                QCOMPARE(bits(*v), want);
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
