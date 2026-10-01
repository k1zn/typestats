// Histograms (Form4, re/histograms.md) on synthetic records.

#include "core/Ext80.h"
#include "core/Histograms.h"
#include "core/NumberFormat.h"

#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace Histograms;

namespace {

// US scan codes of a few keys: a (left little), s, d, f (left index), j (right index), k.
constexpr quint8 A = 0x1E, S = 0x1F, F = 0x21, J = 0x24, K = 0x25;

KeyRecord press(quint8 scan, quint32 dtMs, char16_t ch)
{
    KeyRecord r;
    r.dtUs = dtMs * 1000;
    r.flags = scan | KeyRecord::HasChar;
    r.ch = ch;
    return r;
}

KeyRecord release(quint8 scan, quint32 dtMs)
{
    KeyRecord r;
    r.dtUs = dtMs * 1000;
    r.flags = scan | KeyRecord::KeyUp;
    return r;
}

Source source(const TextModel &m)
{
    Source s;
    s.model = &m;
    s.recEnd = m.records.size();
    s.zones = FingerZones::standard();
    s.label = labelsFromRecords(m.records);
    return s;
}

QStringList str(const Page &p)
{
    QStringList r;
    for (const Bar &b : p.bars)
        r << QStringLiteral("%1=%2").arg(b.label).arg(b.value) + (b.count >= 0 ? QStringLiteral("x%1").arg(b.count) : QString());
    return r;
}

QStringList list(std::initializer_list<const char *> items)
{
    QStringList r;
    for (const char *s : items)
        r << QString::fromUtf8(s);
    return r;
}

} // namespace

class TstHist : public QObject
{
    Q_OBJECT
    // a(first) a j . f(erased) a | pause | k a ; releases add to the time of the next press
    TextModel recs;

private slots:
    void initTestCase()
    {
        recs.records = {press(A, 60000, u'a'), press(A, 100, u'A'), release(A, 20), press(J, 180, u'j'),
                        press(F, 300, u'f'), press(A, 100, u'a'), press(K, 5000, u'k'),
                        press(A, 400, u'a'), press(S, 50, u's')};
        recs.recErased.fill(false, recs.records.size());
        recs.recErased[4] = true;
    }

    void keys()
    {
        const Source src = source(recs);
        Node node;
        const Page all = build(src, node);
        QCOMPARE(all.title, QStringLiteral("Все клавиши"));
        // a: 100, 100, 400 ms (the first press and the one after the long pause do not count)
        QCOMPARE(str(all), list({"a=200x3", "j=200x1", "s=50x1"}));
        QCOMPARE(all.bars[0].key, A);

        node = *drill(node, all, 0);
        const Page a = build(src, node);
        QCOMPARE(a.title, QStringLiteral("Клавиша [a]"));
        // by the previous key: k→a 400, a→a 100, f→a 100 (an erased press still is "previous")
        QCOMPARE(str(a), list({"k=400x1", "a=100x1", "f=100x1"}));

        node = *drill(node, a, 0);
        const Page pair = build(src, node);
        QCOMPARE(pair.title, QStringLiteral("Длительности сочетаний [ka]"));
        QCOMPARE(str(pair), list({"1=400"}));
        QCOMPARE(pair.bars[0].rec, 6);
        QVERIFY(!drill(node, pair, 0));
    }

    void fingers()
    {
        const Source src = source(recs);
        Node node;
        node.kind = Node::AllFingers;
        const Page all = build(src, node);
        QCOMPARE(all.bars.size(), 9);
        QCOMPARE(str(all).mid(0, 5), list({"ЛМ=200x3", "ЛБ=50x1", "ЛС=0x0", "ЛУ=0x0", "ПУ=200x1"}));

        node = *drill(node, all, 0);
        QCOMPARE(node.finger, 0);
        const Page f = build(src, node);
        QCOMPARE(f.title, QStringLiteral("Левый мизинец"));
        // Every "a" here follows a press that reset the state (the first one, the erased f, the long
        // pause before k), so all three are "other".
        QCOMPARE(str(f), list({"Клавиша=0x0", "Палец=0x0", "Рука=0x0", "Прочее=200x3"}));

        node = *drill(node, f, 3);
        const Page other = build(src, node);
        QCOMPARE(other.title, QStringLiteral("Другая рука - Левый мизинец"));
        QCOMPARE(str(other), list({"ka=400"}));
        // The detail pages do not reset the state: here the erased f and the first a count as previous.
        node.relation = 2;
        QCOMPARE(str(build(src, node)), list({"fa=100"}));
        node.relation = 0;
        QCOMPARE(str(build(src, node)), list({"a=100"}));
    }

    void rangeAndSplit()
    {
        Source src = source(recs);
        src.recBegin = 5;
        QCOMPARE(str(build(src, {})), list({"a=250x2", "s=50x1"}));
        src.recBegin = 0;
        src.splitUs = 150000;
        QCOMPARE(str(build(src, {})), list({"a=100x2", "s=50x1"}));
    }

    void hints()
    {
        Bar b;
        b.value = 12.3456f;
        b.label = QStringLiteral("\r");
        b.count = 7;
        QCOMPARE(hint(b, QLocale(QLocale::Russian)), QStringLiteral("12,346 (7) [Enter]"));
        b.count = -1;
        b.label = QStringLiteral("ab");
        QCOMPARE(hint(b, QLocale::c()), QStringLiteral("12.346 ab"));
        const Page p = fromExtra({{600.0f, 3, QStringLiteral("word")}});
        QCOMPARE(str(p), list({"word=600"}));
        QCOMPARE(p.title, QStringLiteral("Дополнительная статистика"));
    }

    void fixedFormat()
    {
        const QLocale c = QLocale::c();
        QCOMPARE(formatFixed(3.125, 2, c), QStringLiteral("3.13")); // QLocale gives 3.12
        QCOMPARE(formatFixed(-0.004, 2, c), QStringLiteral("0.00"));
        QCOMPARE(formatFixed(1234567.891, 3, QLocale(QLocale::Russian)), QStringLiteral("1234567,891"));
        QCOMPARE(formatFixed(2.5, 0, c), QStringLiteral("3"));
        QCOMPARE(formatFixed(std::nan(""), 2, c), QStringLiteral("NAN")); // as FloatToStrF
        QCOMPARE(formatFixed(-HUGE_VAL, 2, c), QStringLiteral("-INF"));
        QCOMPARE(formatFixed(3e19, 1, c), QStringLiteral("30000000000000000000.0"));

        // The same strings as the plain formula (the first implementation) for many numbers.
        auto plain = [](double v, int decimals, const QLocale &loc) {
            Ext scale = Ext(1);
            for (int i = 0; i < decimals; ++i)
                scale = scale * Ext(10);
            const Ext scaled = extFloor(extFabs(Ext(v)) * scale + Ext(0.5));
            const Ext whole = extFloor(scaled / scale);
            QString s = QString::number(qulonglong(whole));
            if (decimals > 0)
                s += loc.decimalPoint() + QString::number(qulonglong(scaled - whole * scale)).rightJustified(decimals, u'0');
            return v < 0 && scaled != 0 ? u'-' + s : s;
        };
        const QLocale ru(QLocale::Russian);
        QRandomGenerator rnd(7);
        QVector<double> values = {0.0, -0.0, 0.5, -0.5, 0.005, 0.0049999, 1.0, 9.995, 99.9995, 1e15, -1e15, 123456789.125};
        for (int i = 0; i < 40000; ++i) {
            const double magnitude = std::pow(10.0, rnd.bounded(-4, 12));
            values << (rnd.generateDouble() - 0.3) * magnitude << double(float(rnd.generateDouble() * magnitude))
                   << std::round(rnd.generateDouble() * magnitude * 1000.0) / 1000.0 + 0.0005;
        }
        for (double v : values)
            for (int d = 0; d <= 4; ++d) {
                const QLocale &loc = d % 2 ? ru : c;
                if (formatFixed(v, d, loc) != plain(v, d, loc))
                    QFAIL(qPrintable(QStringLiteral("%1 %2: %3 != %4").arg(v, 0, 'g', 17).arg(d)
                                         .arg(formatFixed(v, d, loc), plain(v, d, loc))));
            }
    }
};

QTEST_GUILESS_MAIN(TstHist)
#include "tst_hist.moc"
