// Graphs::compute computes in double where that is proven to give the float of the 80-bit arithmetic
// (re/perf.md, "Graphs in double"). Here it is compared, bit for bit, with the plain 80-bit computation
// (the code before that change): golden files and pauses made to hit the places where the proof's
// conditions do not hold and the 80-bit path is taken. With TS_SOFT_EXT80 the reference is Ext80.

#include "core/Ext80.h"
#include "core/Graphs.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"

#include <QDir>
#include <QRandomGenerator>
#include <QTest>

#include <bit>
#include <cmath>

namespace {

using ext = Ext;

// The computation all in Ext, as it was.
void refSmooth(const QVector<float> &src, int from, int to, QVector<float> &dst)
{
    const int n = to - from;
    if (n <= 0)
        return;
    const float alpha = 0.03f;
    const int head = std::min(n, 15);
    float prev = 0.0f;
    for (int i = 0; i < head; ++i)
        prev = float(ext(src[from + i]) + prev);
    prev = float(ext(prev) / head);
    for (int i = from; i < to; ++i) {
        prev = float((ext(1.0f) - alpha) * prev + ext(alpha) * src[i]);
        dst[i] = prev;
    }
}

void refFragment(GraphSeries &g, int from, int to)
{
    const QVector<float> &p = g.pause;
    float sum = 0.0f;
    for (int i = from; i < to; ++i) {
        g.curSpeed[i] = p[i] > 10.0f ? float(ext(60000.0f) / p[i]) : 0.0f;
        sum = float(ext(p[i]) + sum);
    }
    refSmooth(p, from, to, g.medSpeed);
    const float avg = float(ext(sum) / ext(to - from));
    float cum = 0.0f;
    for (int i = from; i < to; ++i) {
        cum = float(ext(p[i]) + cum);
        float r = avg > 0.01 ? float(std::fabs(double(ext(p[i]) - avg)) / ext(avg)) : 0.0f;
        if (r > 1.0f)
            r = 1.0f;
        g.curRhythm[i] = float((ext(1.0f) - r) * 100.0f);
        const float m = g.medSpeed[i];
        g.medSpeed[i] = m > 10.0f ? float(ext(60000.0f) / m) : 0.0f;
        g.arrhythmia[i] = avg > 0.01 ? float(ext(100.0f) * p[i] / avg - 100.0f) : 0.0f;
        const auto k = quint32(i - from);
        g.classicSpeed[i] = cum > 10.0f ? float(ext(qint32(k * 60000u + 120000u)) / cum) : 0.0f;
        g.privSpeed[i] = cum > 10.0f ? float(ext(qint32(k * 60000u + 60000u)) / cum) : 0.0f;
    }
    refSmooth(g.curRhythm, from, to, g.medRhythm);
}

GraphSeries reference(const TextModel &m)
{
    GraphSeries g;
    g.pause = m.pauses;
    const int n = m.size();
    for (QVector<float> *s : {&g.curSpeed, &g.medSpeed, &g.classicSpeed, &g.privSpeed, &g.curRhythm,
                              &g.medRhythm, &g.arrhythmia})
        s->fill(0.0f, n);
    int from = 0;
    for (int start : m.fragmentStarts) {
        refFragment(g, from, start);
        from = start + 1;
    }
    refFragment(g, from, n);
    return g;
}

// The first difference, or an empty string.
QString compare(const GraphSeries &a, const GraphSeries &b)
{
    const QList<QPair<const char *, QPair<const QVector<float> *, const QVector<float> *>>> all = {
        {"curSpeed", {&a.curSpeed, &b.curSpeed}}, {"medSpeed", {&a.medSpeed, &b.medSpeed}},
        {"classicSpeed", {&a.classicSpeed, &b.classicSpeed}}, {"privSpeed", {&a.privSpeed, &b.privSpeed}},
        {"curRhythm", {&a.curRhythm, &b.curRhythm}}, {"medRhythm", {&a.medRhythm, &b.medRhythm}},
        {"arrhythmia", {&a.arrhythmia, &b.arrhythmia}}};
    for (const auto &[name, v] : all) {
        if (v.first->size() != v.second->size())
            return QStringLiteral("%1: size").arg(QLatin1String(name));
        for (int i = 0; i < v.first->size(); ++i)
            if (std::bit_cast<quint32>(v.first->at(i)) != std::bit_cast<quint32>(v.second->at(i)))
                return QStringLiteral("%1[%2]: %3 != %4 (pause %5)")
                    .arg(QLatin1String(name)).arg(i)
                    .arg(double(v.first->at(i)), 0, 'g', 9).arg(double(v.second->at(i)), 0, 'g', 9)
                    .arg(double(a.pause.at(i)), 0, 'g', 9);
    }
    return {};
}

TextModel modelOf(const QVector<float> &pauses, const QVector<int> &fragmentStarts)
{
    TextModel m;
    m.names.resize(pauses.size());
    m.pauses = pauses;
    m.fragmentStarts = fragmentStarts;
    return m;
}

float randomPause(QRandomGenerator &rnd)
{
    switch (rnd.bounded(10)) {
    case 0: return 0.0f;
    case 1: return float(rnd.bounded(1, 100)) * 0.001f;                 // tens of microseconds: tiny next to the mean
    case 2: return float(rnd.bounded(1, 4000000)) * 0.001f;            // up to an hour (keys held)
    case 3: return float(std::ldexp(rnd.generateDouble() + 0.5, rnd.bounded(-40, 40))); // any scale
    case 4: return std::bit_cast<float>(quint32(rnd.generate() & 0x7FFFFFFF) % 0x7F000000u); // any finite bits
    default: return float(qint64(rnd.bounded(20000, 400000))) * 0.001f;   // typing
    }
}

} // namespace

class TstGraphs : public QObject
{
    Q_OBJECT
private slots:
    void golden()
    {
        int files = 0;
        for (const QString &f : QDir(QStringLiteral(TS_GOLDEN_DIR)).entryList({QStringLiteral("*.tsf")})) {
            TsfDocument doc;
            QCOMPARE(Tsf::read(QStringLiteral(TS_GOLDEN_DIR "/") + f, doc), Tsf::ReadError::None);
            for (int v = 0; v < 4; ++v) {
                RecalcOptions opt;
                opt.onlyText = v & 1;
                opt.byPauses = v & 2;
                const TextModel m = Recalc::run(doc.records, opt);
                const QString diff = compare(Graphs::compute(m), reference(m));
                if (!diff.isEmpty())
                    QFAIL(qPrintable(f + QLatin1Char(' ') + diff));
            }
            ++files;
        }
        QVERIFY(files >= 4);
    }

    // Random pauses: mean values made equal to pauses, tiny and huge pauses next to the mean, zeros.
    void random()
    {
        QRandomGenerator rnd(1243);
        for (int round = 0; round < 400; ++round) {
            const int n = rnd.bounded(1, 3000);
            QVector<float> p(n);
            for (float &v : p)
                v = randomPause(rnd);
            // Some fragments where every pause is the same (p == avg) or nearly.
            if (round % 5 == 0) {
                const float same = randomPause(rnd);
                for (int i = 0; i < n; ++i)
                    p[i] = rnd.bounded(4) ? same : std::nextafter(same, 1e30f);
            }
            QVector<int> starts;
            for (int i = rnd.bounded(1, 400); i < n; i += rnd.bounded(1, 400))
                starts << i;
            const TextModel m = modelOf(p, starts);
            const QString diff = compare(Graphs::compute(m), reference(m));
            if (!diff.isEmpty())
                QFAIL(qPrintable(QStringLiteral("round %1: ").arg(round) + diff));
        }
    }

    // Long fragments: the 32-bit products of classicSpeed wrap after 35791 elements, as the original's.
    void longFragment()
    {
        QRandomGenerator rnd(7);
        QVector<float> p(80000);
        for (float &v : p)
            v = float(rnd.bounded(30000, 300000)) * 0.001f;
        const TextModel m = modelOf(p, {});
        QCOMPARE(compare(Graphs::compute(m), reference(m)), QString());
    }

    // smooth() alone on series that take its 80-bit path: a value far below or above the running mean.
    void smoothFallback()
    {
        QRandomGenerator rnd(3);
        for (int round = 0; round < 200; ++round) {
            QVector<float> src(500);
            for (float &v : src)
                v = rnd.bounded(3) ? float(std::ldexp(rnd.generateDouble() + 0.5, rnd.bounded(-30, 30)))
                                   : randomPause(rnd);
            QVector<float> a(src.size()), b(src.size());
            Graphs::smooth(src, 0, int(src.size()), a);
            refSmooth(src, 0, int(src.size()), b);
            for (int i = 0; i < src.size(); ++i)
                if (std::bit_cast<quint32>(a[i]) != std::bit_cast<quint32>(b[i]))
                    QFAIL(qPrintable(QStringLiteral("round %1 [%2]: %3 != %4").arg(round).arg(i)
                                         .arg(double(a[i]), 0, 'g', 9).arg(double(b[i]), 0, 'g', 9)));
        }
    }
};

QTEST_APPLESS_MAIN(TstGraphs)
#include "tst_graphs.moc"
